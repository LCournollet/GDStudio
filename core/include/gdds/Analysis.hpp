#pragma once

// Level analysis: the intermediate representation shared by every engine.
//
// The analysis rasterizes the collision footprints of gameplay objects on a
// half-block grid (15 units), then extracts:
//   * structures   - 4-connected components of solid cells (a 30-block
//                    platform made of 30 objects is ONE structure)
//   * contours     - closed boundary loops (outer and holes), split into
//                    straight segments with an outward face (top/bottom/left/right)
//   * corners      - convex (outer) and concave (inner) vertices of contours
//   * depth        - Chebyshev distance of each solid cell to the boundary,
//                    used for inset outlines and internal textures
//   * free space   - distance of empty cells to the nearest gameplay cell
//   * clearance    - vertical gap between floor and ceiling (corridors)
//   * staircases, hazard clusters, repeated shapes
// plus the gameplay timeline (game modes, speed, gravity, size sections).

#include "Geometry.hpp"
#include "LevelObject.hpp"
#include "ObjectInfo.hpp"
#include "SparseGrid.hpp"
#include "Level.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gdds {

constexpr double kCell = kHalfBlock;  // analysis grid resolution (units)

namespace cellflag {
constexpr uint8_t Solid = 1 << 0;
constexpr uint8_t Hazard = 1 << 1;
constexpr uint8_t Slope = 1 << 2;
constexpr uint8_t Deco = 1 << 3;       // existing (user) decoration
constexpr uint8_t Special = 1 << 4;    // portals, orbs, pads, triggers with a footprint
constexpr uint8_t HazardHalo = 1 << 5; // within the readability margin of a hazard
}  // namespace cellflag

enum class Dir : uint8_t { East = 0, North = 1, West = 2, South = 3 };
enum class Face : uint8_t { Top, Bottom, Left, Right };  // outward normal: up, down, left, right

char const* faceName(Face f);
Vec2 faceNormal(Face f);

// Input record for the analyzer.
struct AnalysisInput {
    LevelObject object;
    ObjectInfo info;
    bool ignore = false;  // studio-generated decoration: not part of the gameplay
};

struct ContourSegment {
    int id = -1;
    int contour = -1;
    int structure = -1;
    Cell a, b;              // grid vertices
    Vec2 p0, p1;            // world coordinates (p0 -> p1 follows the contour direction)
    Dir travel = Dir::East;
    Face face = Face::Top;
    double length = 0;      // units
    bool hole = false;      // part of an inner contour (enclosed cavity)
    bool onGround = false;  // bottom face resting on the ground line (hidden)
    int cornerStart = -1;   // corner at p0
    int cornerEnd = -1;     // corner at p1
    double exposure = 0;    // free distance along the normal (units, capped)
    double hazardCoverage = 0; // fraction of the face directly touching hazards

    Vec2 midpoint() const { return (p0 + p1) * 0.5; }
    bool horizontal() const { return face == Face::Top || face == Face::Bottom; }
};

struct Corner {
    int id = -1;
    int contour = -1;
    int structure = -1;
    Cell vertex;
    Vec2 pos;
    bool convex = true;     // outer corner (solid occupies one quadrant) vs inner corner
    int segIn = -1, segOut = -1;
    Face faceIn = Face::Top, faceOut = Face::Top;
    // Quadrant around the vertex that is "special": for convex corners the
    // quadrant occupied by solid, for concave corners the quadrant left empty.
    // 0 = NE (+x,+y), 1 = NW, 2 = SW, 3 = SE.
    int quadrant = 0;
    bool onGround = false;
};

struct Contour {
    int id = -1;
    int structure = -1;
    bool hole = false;
    std::vector<int> segments;  // in traversal order
    std::vector<int> corners;
    double perimeter = 0;
};

enum class StructureClass : uint8_t { Block, Platform, Wall, Pillar, Mass, Thin, Staircase };
char const* structureClassName(StructureClass c);

struct Structure {
    int id = -1;
    std::vector<Cell> cells;       // sorted (y, x)
    IRect cellBounds;
    Rect bounds;
    int cellCount = 0;
    std::vector<uint32_t> objects; // indices into the analyzer input
    std::vector<int> contours;
    StructureClass cls = StructureClass::Block;
    uint64_t signature = 0;        // identity: hash of absolute cells
    uint64_t shapeSignature = 0;   // translation invariant shape hash (repeated structures)
    int repeatCount = 1;           // structures sharing the same shape
    int maxDepth = 0;
    bool hasSlopes = false;
    bool touchesGround = false;
    double topLength = 0, bottomLength = 0, sideLength = 0;  // exposed face lengths
};

struct Staircase {
    int structure = -1;
    Vec2 start, end;               // outer step corners (lowest, highest)
    int steps = 0;
    double stepW = 0, stepH = 0;
    bool ascendingRight = true;
};

struct HazardCluster {
    int id = -1;
    Rect bounds;
    std::vector<uint32_t> objects;
    int spikes = 0;
    int saws = 0;
    int others = 0;
    int supportStructure = -1;     // structure the hazards sit on (if any)
};

// One stretch of the level with a constant game mode / speed / gravity / size.
struct GameplaySection {
    double x0 = 0, x1 = 0;
    GameMode mode = GameMode::Cube;
    int speed = 0;                 // 0 = 1x, 1 = 0.5x, 2 = 2x, 3 = 3x, 4 = 4x
    bool flipped = false;
    bool mini = false;
    bool dual = false;
    double minClearance = 0;       // tightest vertical corridor in the section (units)
    double hazardDensity = 0;      // hazards per block of length
    double solidDensity = 0;       // solid cells per block of length
    // 0 = relaxed .. 1 = extremely tight; drives readability rules.
    double tightness = 0;
};

struct PortalMarker {
    double x = 0, y = 0;
    PortalType type = PortalType::None;
    int value = 0;
    int objectId = 0;
};

struct AnalysisStats {
    int objectsTotal = 0;
    int gameplayObjects = 0;
    int solidObjects = 0;
    int hazardObjects = 0;
    int decorationObjects = 0;
    int ignoredObjects = 0;
    int unknownObjects = 0;
    int structures = 0;
    int contours = 0;
    int segments = 0;
    int corners = 0;
    int staircases = 0;
    int hazardClusters = 0;
    int sections = 0;
    double analysisMs = 0;
};

class LevelAnalysis {
public:
    // Input objects (kept so engines can look at object ids, colors, z layers).
    std::vector<AnalysisInput> inputs;
    std::vector<ObjectFootprint> footprints;

    SparseGrid<uint8_t> flags;
    SparseGrid<int32_t> structureOf{-1};
    SparseGrid<uint8_t> depth;          // solid cells: 1 = boundary cell, 2 = one inside...
    SparseGrid<uint8_t> freeDist;       // empty cells: distance (cells) to gameplay, 0 = unknown/far

    std::vector<Structure> structures;
    std::vector<Contour> contours;
    std::vector<ContourSegment> segments;
    std::vector<Corner> corners;
    std::vector<Staircase> staircases;
    std::vector<HazardCluster> hazardClusters;
    std::vector<GameplaySection> sections;
    std::vector<PortalMarker> portals;

    Rect levelBounds = Rect::empty();     // bounds of all gameplay footprints
    Rect gameplayBounds = Rect::empty();  // same, solids/hazards/portals only
    IRect cellBounds;

    // Highest draw rank used by gameplay solids / hazards (zlayer rank * 10000 + zorder).
    int solidTopRank = 0;
    int hazardBottomRank = 0;
    int hazardTopRank = 0;

    AnalysisStats stats;

    static Cell cellAt(Vec2 p) { return {int(std::floor(p.x / kCell)), int(std::floor(p.y / kCell))}; }
    static Vec2 cellCenter(Cell c) { return {(c.x + 0.5) * kCell, (c.y + 0.5) * kCell}; }
    static Vec2 vertexPos(Cell v) { return {v.x * kCell, v.y * kCell}; }

    bool isSolid(Cell c) const { return flags.get(c) & cellflag::Solid; }
    bool isHazard(Cell c) const { return flags.get(c) & cellflag::Hazard; }
    bool isBlocked(Cell c) const { return flags.get(c) & (cellflag::Solid | cellflag::Hazard | cellflag::Special); }
    int structureAt(Cell c) const { return structureOf.get(c); }

    // Free distance in units from a point to the nearest gameplay cell
    // (capped at kFreeDistCap cells). Solid/hazard cells return 0.
    double freeDistanceAt(Vec2 p) const;
    // Vertical gap (units) around an empty point: distance to the solid above
    // plus distance to the solid (or ground) below, capped at `cap`.
    double verticalClearance(Vec2 p, double cap = 600) const;
    // Section containing x (or the nearest one).
    GameplaySection const* sectionAt(double x) const;
    // Is this rectangle free of solids/hazards/specials (with margin)?
    bool isAreaFree(Rect const& r, double margin = 0) const;
    // Fraction of the rect's cells inside solids.
    double solidCoverage(Rect const& r) const;
    // Distance from p to the nearest hazard footprint (units, capped).
    double hazardDistance(Vec2 p, double cap = 120) const;

    static constexpr int kFreeDistCap = 40;  // cells (20 blocks)

    std::string summary() const;
};

struct AnalyzerOptions {
    double hazardHaloMargin = 12;  // units kept around hazards for readability
    bool groundIsSolid = true;     // treat y < 0 as the ground (hides bottom faces)
    int freeDistanceMargin = 24;   // cells around the gameplay where free distance is computed
};

class Analyzer {
public:
    explicit Analyzer(AnalyzerOptions opts = {}) : m_opts(opts) {}

    std::unique_ptr<LevelAnalysis> analyze(std::vector<AnalysisInput> inputs, LevelHeader const* header = nullptr) const;

    // Convenience for tools/tests: classify every object with the static table.
    static std::vector<AnalysisInput> inputsFromLevel(Level const& level);

    // Applies per-object properties on top of the id-based info:
    //  * no-touch objects (key 121) do not collide: solids become decoration,
    //    hazards become "special" (still kept clear for readability);
    //  * hidden objects (key 135) and objects moved/toggled by triggers
    //    (a group of theirs is a trigger target) are never decorated: they
    //    are kept as "special" footprints. Decorating them would reveal
    //    hidden paths or leave static decoration behind moving platforms.
    static void refineInputs(std::vector<AnalysisInput>& inputs);

private:
    AnalyzerOptions m_opts;
};

}  // namespace gdds
