#pragma once

// A placement proposal produced by an engine. Proposals are previewed,
// validated and only then turned into real objects in the level.

#include "Geometry.hpp"
#include "LevelObject.hpp"
#include "Palette.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gdds {

enum class PlacementTier : uint8_t {
    Primary,    // main forms (fills, outlines, large compositions)
    Secondary,  // ornaments, corners, insets
    Micro,      // small details, particles
};

struct Placement {
    LevelObject object;        // position / rotation / scale / z set by the engine
    std::string layerId;
    ColorRole role = ColorRole::Primary;
    std::optional<ColorRole> detailRole;
    double opacity = 1;        // multiplies the role opacity (quantized into channel buckets)
    Vec2 anchor;               // point used for mask membership
    Rect footprint;            // visible bounds (overlap / density checks)
    PlacementTier tier = PlacementTier::Primary;
    int priority = 0;          // harmonizer: higher wins conflicts
    uint64_t key = 0;          // deterministic identity of the decision
    std::string source;        // which generator/rule produced it (debug, stats)
    bool nearHazard = false;   // flagged by the engine for readability checks
};

// Opacity bucket used to share color channels (0..10 -> 0%, 10%, ... 100%).
inline int opacityBucket(double opacity) {
    int b = int(opacity * 10 + 0.5);
    return b < 1 ? 1 : (b > 10 ? 10 : b);
}

}  // namespace gdds
