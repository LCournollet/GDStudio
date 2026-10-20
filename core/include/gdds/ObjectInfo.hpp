#pragma once

// What the analysis needs to know about any object of the level: its
// gameplay role and its collision footprint. In the mod this comes from the
// live GameObject (type + hitbox reported by the game itself); offline it
// comes from the static catalog.

#include "Geometry.hpp"
#include "LevelObject.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gdds {

enum class ObjectKind : uint8_t {
    Unknown,      // treated as gameplay (never touched, never decorated over)
    Solid,
    Hazard,
    Decoration,
    Trigger,
    Portal,
    Pad,
    Orb,
    Collectible,
    StartPos,
    Special,      // text, item displays, collision blocks, ...
};

enum class PortalType : uint8_t { None, GameMode, Gravity, Speed, Size, Mirror, Dual, Teleport };

enum class HitboxShape : uint8_t { None, Box, Slope, Circle };

struct ObjectInfo {
    ObjectKind kind = ObjectKind::Unknown;
    PortalType portal = PortalType::None;
    int portalValue = 0;            // game mode / speed index / gravity (0 normal, 1 flipped, 2 toggle) ...
    HitboxShape shape = HitboxShape::None;
    double hitW = 0, hitH = 0;      // hitbox size at scale 1, rotation 0
    double hitOffX = 0, hitOffY = 0;
    double visW = 30, visH = 30;    // approximate visible size
    int defaultZLayer = 0;          // as in object strings (0 = unknown)
    int defaultZOrder = 0;
    char const* name = "";

    bool isGameplay() const {
        return kind != ObjectKind::Decoration;  // triggers, portals... all count as gameplay
    }
    bool collides() const { return kind == ObjectKind::Solid || kind == ObjectKind::Hazard; }
};

// Geometry of one analysed object, in level units.
struct ObjectFootprint {
    OrientedRect hitbox;            // collision box (for Box / Circle shapes)
    std::optional<Triangle> slope;  // collision triangle for slopes
    Rect visual;                    // conservative visible bounds
};

ObjectFootprint computeFootprint(LevelObject const& obj, ObjectInfo const& info);

// Static knowledge about object IDs (gameplay table + decoration safety list).
// The tables are generated from data extracted from the installed game; see
// tools/catalog/ and docs/CATALOG.md.
class StaticObjectTable {
public:
    static StaticObjectTable const& get();

    // Returns the known info for an id; unknown ids come back as Unknown
    // (which the analysis treats conservatively as gameplay).
    ObjectInfo info(int id) const;
    bool isKnown(int id) const;
    // True only for ids that are verified decoration-only objects.
    bool isSafeDecoration(int id) const;
    char const* frameName(int id) const;

    struct Entry;

private:
    StaticObjectTable();
    std::vector<Entry> const* m_entries;
};

}  // namespace gdds
