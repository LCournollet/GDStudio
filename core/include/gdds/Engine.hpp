#pragma once

// Common infrastructure for the generation engines.

#include "Analysis.hpp"
#include "Composition.hpp"
#include "DecoCatalog.hpp"
#include "Layer.hpp"
#include "Mask.hpp"
#include "Placement.hpp"
#include "Random.hpp"

#include <atomic>
#include <functional>
#include <initializer_list>
#include <vector>

namespace gdds {

struct EngineContext {
    LevelAnalysis const& analysis;
    DecoCatalog const& catalog;
    Layer const& layer;
    CompositionPlan const& plan;
    MaskSet const& masks;
    uint64_t seed = 1;          // layer seed mixed with the global seed
    ZSlot z;                    // base draw slot of the layer
    double density = 0.5;       // effective density (layer x global)
    std::atomic<bool> const* cancel = nullptr;
    std::vector<Placement>* out = nullptr;

    bool cancelled() const { return cancel && cancel->load(std::memory_order_relaxed); }
    Rng rng(uint64_t a, uint64_t b = 0, uint64_t c = 0) const { return Rng(hashOf(seed, a, b, c)); }

    struct Pick {
        DecoDef const* def = nullptr;
        ObjectRule const* rule = nullptr;
        explicit operator bool() const { return def != nullptr; }
    };
    // Picks an object whose shape is in `shapes` and whose rule allows the
    // anchor. Uses the layer's object rules when it has any; otherwise falls
    // back to catalog entries tagged "default". Deterministic for a given key.
    Pick pick(std::initializer_list<DecoShape> shapes, uint32_t anchors, uint64_t key,
              std::function<bool(DecoDef const&)> const& filter = {}) const;
    // All candidates (for engines that need to choose by size themselves).
    std::vector<Pick> candidates(std::initializer_list<DecoShape> shapes, uint32_t anchors,
                                 std::function<bool(DecoDef const&)> const& filter = {}) const;

    // Builds a placement whose VISIBLE center lands on `visualCenter`.
    Placement place(Pick const& p, Vec2 visualCenter, double rotation, double scaleX, double scaleY, Vec2 anchorPoint,
                    ColorRole role, int zOffset, PlacementTier tier, uint64_t key, char const* source) const;
    // Visible bounds of a def placed with this transform.
    static Rect visualBounds(DecoDef const& def, Vec2 visualCenter, double rotation, double scaleX, double scaleY);

    void emit(Placement p) const { out->push_back(std::move(p)); }
};

class Engine {
public:
    virtual ~Engine() = default;
    // Generates the placements of one layer (already filtered by the masks).
    virtual void generate(EngineContext& ctx) = 0;
};

// Factories (one engine per family; dispatch on the layer kind inside).
std::unique_ptr<Engine> makeStructuralEngine();
std::unique_ptr<Engine> makeLightingEngine();
std::unique_ptr<Engine> makeArtisticEngine();

// Cell-set helpers shared by engines -----------------------------------------

struct CellRect {
    int x0, y0, x1, y1;  // inclusive
    Rect world() const { return {x0 * kCell, y0 * kCell, (x1 + 1) * kCell, (y1 + 1) * kCell}; }
    int area() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
};
// Greedy decomposition of a cell set into maximal rectangles (row runs merged
// vertically). Deterministic. Used to cover shapes with few stretched objects.
std::vector<CellRect> decomposeRects(std::vector<Cell> cells, int maxW = 1 << 20, int maxH = 1 << 20);

struct TracedSegment {
    Vec2 p0, p1;
    Face face;
    bool convexStart = true, convexEnd = true;
};
struct TracedLoop {
    bool hole = false;
    std::vector<TracedSegment> segments;
};
// Traces the boundary loops of an arbitrary cell set (solid on the left).
std::vector<TracedLoop> traceLoops(std::vector<Cell> const& cells);

}  // namespace gdds
