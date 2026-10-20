#pragma once

// Catalog of the decoration objects the engines may use.
//
// Each entry describes a REAL Geometry Dash object id with what the engines
// need to place it precisely: the visible part of its sprite relative to the
// object position (measured from the game's sprite sheets), its canonical
// orientation, whether it can be stretched, and its usage category.
// Only decoration-only objects (no collision, not a trigger/portal/orb/pad)
// are allowed in the catalog; the mod re-verifies every id against the live
// game at startup and disables any id that does not behave as a decoration.

#include "Geometry.hpp"
#include "Json.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace gdds {

enum class DecoShape : uint8_t {
    Square,        // filled square / rectangle (stretchable fills)
    Line,          // thin straight line, canonical orientation horizontal
    Corner,        // corner piece, canonical solid quadrant = NE
    Dot,           // small round or square detail
    Circle,        // filled circle
    Ring,          // circle outline
    Triangle,
    Diamond,
    Polygon,       // hexagon, octagon, other regular shapes
    OutlineShape,  // square/shape outline
    GlowEdge,      // gradient strip: canonical bright side DOWN (fades upward)
    GlowCorner,    // corner gradient, canonical bright corner at the SW of the sprite
    GlowRound,     // radial glow
    Chain,         // vertical hanging element
    Ornament,      // other detail sprites
    Large,         // large background art
};
constexpr int kDecoShapeCount = 16;
char const* decoShapeName(DecoShape s);
std::optional<DecoShape> decoShapeFromName(std::string const& n);

struct DecoDef {
    int id = 0;
    std::string name;
    std::string frame;            // main sprite frame name
    std::string category;         // UI category: Blocks, Outlines, Corners, Glow, Details, Background, ...
    DecoShape shape = DecoShape::Ornament;
    // Visible bounds of the sprite relative to the object position, at scale 1
    // and rotation 0, in the sprite's NATIVE orientation (GD units).
    double visX0 = -15, visY0 = -15, visX1 = 15, visY1 = 15;
    // Clockwise rotation that brings the native sprite to the shape's
    // canonical orientation (multiple of 90). Engines think in canonical
    // orientation; place() adds this rotation.
    double baseRotation = 0;
    double thickness = 0;         // lines / outlines: stroke width (units)
    bool colorable = true;        // takes its main color channel
    bool hasDetail = false;       // has a second (detail) color part
    bool stretchable = false;     // can be scaled non-uniformly without artifacts
    bool blending = false;        // looks right with additive blending (glows)
    double maxScale = 16;         // GD editor limit is larger, keep sane values
    std::vector<std::string> tags;

    double visWidth() const { return visX1 - visX0; }
    double visHeight() const { return visY1 - visY0; }
    Vec2 visCenter() const { return {(visX0 + visX1) / 2, (visY0 + visY1) / 2}; }
    bool quarterTurned() const { return std::fmod(std::abs(baseRotation), 180.0) > 45.0; }
    // Size in the canonical orientation.
    double canonW() const { return quarterTurned() ? visHeight() : visWidth(); }
    double canonH() const { return quarterTurned() ? visWidth() : visHeight(); }
    bool hasTag(std::string const& t) const;

    json::Value toJson() const;
    static DecoDef fromJson(json::Value const& v);
};

class DecoCatalog {
public:
    // Built-in catalog (generated from the game's data, see tools/catalog).
    static DecoCatalog const& builtin();

    void add(DecoDef def);
    DecoDef const* find(int id) const;
    std::vector<DecoDef> const& all() const { return m_defs; }
    std::vector<DecoDef const*> byShape(DecoShape s) const;
    std::vector<DecoDef const*> byCategory(std::string const& c) const;
    std::vector<std::string> categories() const;
    // Text search on name / id / tags.
    std::vector<DecoDef const*> search(std::string const& query) const;

    // Ids disabled at runtime (failed live verification in the game).
    void disable(int id);
    bool isDisabled(int id) const;

    json::Value toJson() const;
    void loadJson(json::Value const& v);

private:
    std::vector<DecoDef> m_defs;
    std::vector<int> m_disabled;
};

}  // namespace gdds
