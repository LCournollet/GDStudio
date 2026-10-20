#pragma once

// Palettes, color roles and color channel allocation.
//
// Every generated object uses a ROLE (primary, accent, glow...). Roles are
// resolved per layer (global palette + optional layer override) and mapped to
// dedicated GD color channels, allocated from channels the level does not
// use. Changing a layer's glow intensity or color therefore only updates its
// channels: no object needs to be regenerated, and other layers are untouched.

#include "Json.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gdds {

struct Color3 {
    int r = 255, g = 255, b = 255;

    static std::optional<Color3> fromHex(std::string const& hex);
    std::string toHex() const;
    double luminance() const;  // relative luminance (WCAG)
    Color3 mix(Color3 o, double t) const;

    void toHSV(double& h, double& s, double& v) const;
    static Color3 fromHSV(double h, double s, double v);

    bool operator==(Color3 const&) const = default;
};

double contrastRatio(Color3 a, Color3 b);

enum class ColorRole : uint8_t { Primary, Secondary, Accent, Glow, Detail, Background, Foreground };
constexpr int kColorRoleCount = 7;
char const* colorRoleName(ColorRole r);
std::optional<ColorRole> colorRoleFromName(std::string const& n);

struct ColorAdjust {
    double hue = 0;           // degrees added to every role
    double saturation = 1;    // multiplier
    double brightness = 1;    // multiplier
    double contrast = 1;      // pushes luminance away from mid grey
    double opacity = 1;       // multiplier on every role's opacity
    double glowIntensity = 1; // multiplier on glow / accent opacity (blending roles)
    double variation = 0.0;   // per-object hue/brightness jitter amplitude [0, 1]

    json::Value toJson() const;
    static ColorAdjust fromJson(json::Value const& v);
};

struct RoleColor {
    Color3 color;
    double opacity = 1;
    bool blending = false;  // additive blending (glows, lights)
};

struct Palette {
    std::string name = "Custom";
    std::array<RoleColor, kColorRoleCount> roles;
    ColorAdjust adjust;
    // How colors are distributed: weight of secondary vs primary on internal
    // details, accent frequency, etc. (0..1).
    double accentShare = 0.25;
    double secondaryShare = 0.35;

    RoleColor const& role(ColorRole r) const { return roles[size_t(r)]; }
    RoleColor& role(ColorRole r) { return roles[size_t(r)]; }

    json::Value toJson() const;
    static Palette fromJson(json::Value const& v);
    static Palette defaultPalette();
};

// Optional per-layer override of the global palette.
struct PaletteOverride {
    std::array<std::optional<RoleColor>, kColorRoleCount> roles;
    std::optional<ColorAdjust> adjust;

    bool empty() const;
    json::Value toJson() const;
    static PaletteOverride fromJson(json::Value const& v);
};

// Final color of a role for a layer, after override + adjustments.
RoleColor resolveRole(Palette const& global, PaletteOverride const& layerOverride, ColorRole role);
ColorAdjust resolveAdjust(Palette const& global, PaletteOverride const& layerOverride);

// Maps (layer, role, opacity bucket) to GD color channels.
class ChannelAllocator {
public:
    static constexpr int kMinChannel = 1;
    static constexpr int kMaxChannel = 999;

    // Channels used by the level (objects, color triggers, copy sources...).
    void setUsedByLevel(std::set<int> used) { m_levelUsed = std::move(used); }
    std::set<int> const& usedByLevel() const { return m_levelUsed; }

    // Returns the channel for this key, allocating a free one (from the top
    // of the range, where levels rarely put their own channels). Returns 0 if
    // the channel space is exhausted.
    int channelFor(std::string const& layerId, ColorRole role, int opacityBucket = 10);
    // Releases every channel of a layer (layer deleted).
    void releaseLayer(std::string const& layerId);
    // Channels allocated to a layer.
    std::vector<int> channelsOf(std::string const& layerId) const;

    struct Allocation {
        std::string layer;
        ColorRole role;
        int opacityBucket;
        int channel;
    };
    std::vector<Allocation> allocations() const;

    json::Value toJson() const;
    void loadJson(json::Value const& v);

private:
    std::map<std::string, int> m_map;  // "layer|role|bucket" -> channel
    std::set<int> m_levelUsed;
    static std::string keyOf(std::string const& layerId, ColorRole role, int bucket);
};

}  // namespace gdds
