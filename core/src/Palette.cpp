#include <gdds/Palette.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gdds {

// ---------------------------------------------------------------------------
// Color3

std::optional<Color3> Color3::fromHex(std::string const& hexIn) {
    std::string hex = hexIn;
    if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
    if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = nib(hex[i]);
        if (v[i] < 0) return std::nullopt;
    }
    return Color3{v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]};
}

std::string Color3::toHex() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    return buf;
}

double Color3::luminance() const {
    auto lin = [](int c) {
        double s = c / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

Color3 Color3::mix(Color3 o, double t) const {
    return {int(std::lround(r + (o.r - r) * t)), int(std::lround(g + (o.g - g) * t)), int(std::lround(b + (o.b - b) * t))};
}

void Color3::toHSV(double& h, double& s, double& v) const {
    double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf});
    double d = mx - mn;
    v = mx;
    s = mx <= 0 ? 0 : d / mx;
    if (d <= 1e-12) { h = 0; return; }
    if (mx == rf) h = 60 * std::fmod((gf - bf) / d, 6.0);
    else if (mx == gf) h = 60 * ((bf - rf) / d + 2);
    else h = 60 * ((rf - gf) / d + 4);
    if (h < 0) h += 360;
}

Color3 Color3::fromHSV(double h, double s, double v) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
    double rf = 0, gf = 0, bf = 0;
    if (h < 60) { rf = c; gf = x; }
    else if (h < 120) { rf = x; gf = c; }
    else if (h < 180) { gf = c; bf = x; }
    else if (h < 240) { gf = x; bf = c; }
    else if (h < 300) { rf = x; bf = c; }
    else { rf = c; bf = x; }
    return {int(std::lround((rf + m) * 255)), int(std::lround((gf + m) * 255)), int(std::lround((bf + m) * 255))};
}

double contrastRatio(Color3 a, Color3 b) {
    double la = a.luminance(), lb = b.luminance();
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

char const* colorRoleName(ColorRole r) {
    switch (r) {
        case ColorRole::Primary: return "primary";
        case ColorRole::Secondary: return "secondary";
        case ColorRole::Accent: return "accent";
        case ColorRole::Glow: return "glow";
        case ColorRole::Detail: return "detail";
        case ColorRole::Background: return "background";
        case ColorRole::Foreground: return "foreground";
    }
    return "primary";
}

std::optional<ColorRole> colorRoleFromName(std::string const& n) {
    for (int i = 0; i < kColorRoleCount; ++i)
        if (n == colorRoleName(ColorRole(i))) return ColorRole(i);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ColorAdjust / Palette

json::Value ColorAdjust::toJson() const {
    json::Value v = json::Value::object();
    v["hue"] = hue;
    v["saturation"] = saturation;
    v["brightness"] = brightness;
    v["contrast"] = contrast;
    v["opacity"] = opacity;
    v["glowIntensity"] = glowIntensity;
    v["variation"] = variation;
    return v;
}

ColorAdjust ColorAdjust::fromJson(json::Value const& v) {
    ColorAdjust a;
    a.hue = v.num("hue", 0);
    a.saturation = v.num("saturation", 1);
    a.brightness = v.num("brightness", 1);
    a.contrast = v.num("contrast", 1);
    a.opacity = v.num("opacity", 1);
    a.glowIntensity = v.num("glowIntensity", 1);
    a.variation = v.num("variation", 0);
    return a;
}

namespace {
json::Value roleColorJson(RoleColor const& rc) {
    json::Value v = json::Value::object();
    v["color"] = rc.color.toHex();
    v["opacity"] = rc.opacity;
    v["blending"] = rc.blending;
    return v;
}
RoleColor roleColorFromJson(json::Value const& v, RoleColor def = {}) {
    RoleColor rc = def;
    if (auto c = Color3::fromHex(v.str("color"))) rc.color = *c;
    rc.opacity = v.num("opacity", def.opacity);
    rc.blending = v.boolean("blending", def.blending);
    return rc;
}
}  // namespace

json::Value Palette::toJson() const {
    json::Value v = json::Value::object();
    v["name"] = name;
    json::Value roleObj = json::Value::object();
    for (int i = 0; i < kColorRoleCount; ++i) roleObj[colorRoleName(ColorRole(i))] = roleColorJson(roles[i]);
    v["roles"] = roleObj;
    v["adjust"] = adjust.toJson();
    v["accentShare"] = accentShare;
    v["secondaryShare"] = secondaryShare;
    return v;
}

Palette Palette::fromJson(json::Value const& v) {
    Palette p = defaultPalette();
    p.name = v.str("name", p.name);
    auto& roleObj = v.get("roles");
    for (int i = 0; i < kColorRoleCount; ++i) {
        if (auto r = roleObj.find(colorRoleName(ColorRole(i)))) p.roles[i] = roleColorFromJson(*r, p.roles[i]);
    }
    if (v.contains("adjust")) p.adjust = ColorAdjust::fromJson(v.get("adjust"));
    p.accentShare = v.num("accentShare", p.accentShare);
    p.secondaryShare = v.num("secondaryShare", p.secondaryShare);
    return p;
}

Palette Palette::defaultPalette() {
    Palette p;
    p.name = "Default";
    p.role(ColorRole::Primary) = {{40, 16, 72}, 1, false};       // deep purple block fill
    p.role(ColorRole::Secondary) = {{255, 90, 200}, 1, false};   // pink outlines
    p.role(ColorRole::Accent) = {{255, 255, 255}, 1, false};     // white details
    p.role(ColorRole::Glow) = {{0, 220, 255}, 0.55, true};       // cyan glow
    p.role(ColorRole::Detail) = {{150, 110, 255}, 0.8, false};
    p.role(ColorRole::Background) = {{24, 12, 44}, 1, false};
    p.role(ColorRole::Foreground) = {{8, 4, 16}, 1, false};
    return p;
}

bool PaletteOverride::empty() const {
    for (auto& r : roles) if (r) return false;
    return !adjust;
}

json::Value PaletteOverride::toJson() const {
    json::Value v = json::Value::object();
    json::Value roleObj = json::Value::object();
    for (int i = 0; i < kColorRoleCount; ++i)
        if (roles[i]) roleObj[colorRoleName(ColorRole(i))] = roleColorJson(*roles[i]);
    v["roles"] = roleObj;
    if (adjust) v["adjust"] = adjust->toJson();
    return v;
}

PaletteOverride PaletteOverride::fromJson(json::Value const& v) {
    PaletteOverride o;
    auto& roleObj = v.get("roles");
    for (int i = 0; i < kColorRoleCount; ++i)
        if (auto r = roleObj.find(colorRoleName(ColorRole(i)))) o.roles[i] = roleColorFromJson(*r);
    if (v.contains("adjust")) o.adjust = ColorAdjust::fromJson(v.get("adjust"));
    return o;
}

ColorAdjust resolveAdjust(Palette const& global, PaletteOverride const& ov) {
    ColorAdjust a = global.adjust;
    if (ov.adjust) {
        // Layer adjustments compose with the global ones.
        a.hue += ov.adjust->hue;
        a.saturation *= ov.adjust->saturation;
        a.brightness *= ov.adjust->brightness;
        a.contrast *= ov.adjust->contrast;
        a.opacity *= ov.adjust->opacity;
        a.glowIntensity *= ov.adjust->glowIntensity;
        a.variation = ov.adjust->variation;
    }
    return a;
}

RoleColor resolveRole(Palette const& global, PaletteOverride const& ov, ColorRole role) {
    RoleColor rc = ov.roles[size_t(role)] ? *ov.roles[size_t(role)] : global.role(role);
    ColorAdjust a = resolveAdjust(global, ov);

    double h, s, v;
    rc.color.toHSV(h, s, v);
    h += a.hue;
    s *= a.saturation;
    v *= a.brightness;
    Color3 c = Color3::fromHSV(h, s, v);
    if (a.contrast != 1) {
        auto con = [&](int ch) { return int(std::lround(std::clamp((ch - 127.5) * a.contrast + 127.5, 0.0, 255.0))); };
        c = {con(c.r), con(c.g), con(c.b)};
    }
    rc.color = c;
    rc.opacity = std::clamp(rc.opacity * a.opacity * (rc.blending ? a.glowIntensity : 1.0), 0.0, 1.0);
    return rc;
}

// ---------------------------------------------------------------------------
// ChannelAllocator

std::string ChannelAllocator::keyOf(std::string const& layerId, ColorRole role, int bucket) {
    return layerId + "|" + colorRoleName(role) + "|" + std::to_string(bucket);
}

int ChannelAllocator::channelFor(std::string const& layerId, ColorRole role, int bucket) {
    auto key = keyOf(layerId, role, bucket);
    if (auto it = m_map.find(key); it != m_map.end()) {
        // A channel previously allocated may since have been used by the level
        // (user placed objects on it). Keep it only while it is still ours.
        if (!m_levelUsed.count(it->second)) return it->second;
        m_map.erase(it);
    }
    std::set<int> taken;
    for (auto& [k, ch] : m_map) taken.insert(ch);
    for (int ch = kMaxChannel; ch >= kMinChannel; --ch) {
        if (m_levelUsed.count(ch) || taken.count(ch)) continue;
        m_map[key] = ch;
        return ch;
    }
    return 0;
}

void ChannelAllocator::releaseLayer(std::string const& layerId) {
    std::string prefix = layerId + "|";
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->first.rfind(prefix, 0) == 0) it = m_map.erase(it);
        else ++it;
    }
}

std::vector<int> ChannelAllocator::channelsOf(std::string const& layerId) const {
    std::vector<int> out;
    std::string prefix = layerId + "|";
    for (auto& [k, ch] : m_map)
        if (k.rfind(prefix, 0) == 0) out.push_back(ch);
    return out;
}

std::vector<ChannelAllocator::Allocation> ChannelAllocator::allocations() const {
    std::vector<Allocation> out;
    for (auto& [k, ch] : m_map) {
        auto p1 = k.find('|');
        auto p2 = k.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        auto role = colorRoleFromName(k.substr(p1 + 1, p2 - p1 - 1));
        if (!role) continue;
        out.push_back({k.substr(0, p1), *role, std::stoi(k.substr(p2 + 1)), ch});
    }
    return out;
}

json::Value ChannelAllocator::toJson() const {
    json::Value v = json::Value::object();
    for (auto& [k, ch] : m_map) v[k] = ch;
    return v;
}

void ChannelAllocator::loadJson(json::Value const& v) {
    m_map.clear();
    for (auto& [k, ch] : v.members()) m_map[k] = ch.asInt();
}

}  // namespace gdds
