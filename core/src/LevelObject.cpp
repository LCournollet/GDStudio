#include <gdds/LevelObject.hpp>
#include <gdds/Random.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <map>

namespace gdds {

namespace zlayer {
int rank(int z) {
    switch (z) {
        case B5: return 0;
        case B4: return 1;
        case B3: return 2;
        case B2: return 3;
        case B1: return 4;
        case Default: return 4;  // unknown default: treat like B1 (conservative)
        case T1: return 5;
        case T2: return 6;
        case T3: return 7;
        case T4: return 8;
        default: return z < 0 ? 1 : 6;
    }
}
char const* name(int z) {
    switch (z) {
        case B5: return "B5";
        case B4: return "B4";
        case B3: return "B3";
        case B2: return "B2";
        case B1: return "B1";
        case T1: return "T1";
        case T2: return "T2";
        case T3: return "T3";
        case T4: return "T4";
        default: return "Default";
    }
}
int fromName(std::string_view n) {
    if (n == "B5") return B5;
    if (n == "B4") return B4;
    if (n == "B3") return B3;
    if (n == "B2") return B2;
    if (n == "B1") return B1;
    if (n == "T1") return T1;
    if (n == "T2") return T2;
    if (n == "T3") return T3;
    if (n == "T4") return T4;
    return Default;
}
}  // namespace zlayer

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

namespace {

std::optional<double> toDouble(std::string_view s) {
    if (s.empty()) return std::nullopt;
    double d = 0;
    char const* b = s.data();
    if (*b == '+') ++b;
    auto res = std::from_chars(b, s.data() + s.size(), d);
    if (res.ec != std::errc()) return std::nullopt;
    return d;
}

int toInt(std::string_view s, int def = 0) {
    auto d = toDouble(s);
    return d ? int(*d) : def;
}

}  // namespace

std::string formatNumber(double v) {
    if (std::abs(v - std::round(v)) < 1e-9) {
        long long i = (long long)std::llround(v);
        return std::to_string(i);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

// ---------------------------------------------------------------------------
// HSV

std::string HSV::toString() const {
    return formatNumber(h) + "a" + formatNumber(s) + "a" + formatNumber(v) + "a" + (sAdd ? "1" : "0") + "a" +
           (vAdd ? "1" : "0");
}

std::optional<HSV> HSV::parse(std::string_view str) {
    auto parts = split(str, 'a');
    if (parts.size() < 3) return std::nullopt;
    HSV out;
    out.h = toDouble(parts[0]).value_or(0);
    out.s = toDouble(parts[1]).value_or(1);
    out.v = toDouble(parts[2]).value_or(1);
    if (parts.size() > 3) out.sAdd = toInt(parts[3]) != 0;
    if (parts.size() > 4) out.vAdd = toInt(parts[4]) != 0;
    return out;
}

// ---------------------------------------------------------------------------
// LevelObject

std::string const* LevelObject::extraValue(int k) const {
    for (auto& [ek, v] : extra) if (ek == k) return &v;
    return nullptr;
}

void LevelObject::setExtra(int k, std::string value) {
    for (auto& [ek, v] : extra) if (ek == k) { v = std::move(value); return; }
    extra.emplace_back(k, std::move(value));
}

int LevelObject::extraInt(int k, int def) const {
    auto v = extraValue(k);
    return v ? toInt(*v, def) : def;
}

bool LevelObject::hasGroup(int g) const { return std::find(groups.begin(), groups.end(), g) != groups.end(); }

void LevelObject::addGroup(int g) {
    if (!hasGroup(g)) groups.push_back(g);
}

void LevelObject::removeGroup(int g) { groups.erase(std::remove(groups.begin(), groups.end(), g), groups.end()); }

std::optional<LevelObject> LevelObject::parse(std::string_view str) {
    // Some strings end with a trailing comma.
    while (!str.empty() && (str.back() == ',' || str.back() == ';')) str.remove_suffix(1);
    if (str.empty()) return std::nullopt;
    auto parts = split(str, ',');
    if (parts.size() < 2) return std::nullopt;

    LevelObject obj;
    bool hasID = false;
    std::optional<double> legacyScale;
    bool hasScaleX = false, hasScaleY = false;

    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        int k = toInt(parts[i], -1);
        std::string_view v = parts[i + 1];
        switch (k) {
            case key::ID: obj.id = toInt(v); hasID = true; break;
            case key::X: obj.pos.x = toDouble(v).value_or(0); break;
            case key::Y: obj.pos.y = toDouble(v).value_or(0); break;
            case key::FlipX: obj.flipX = toInt(v) != 0; break;
            case key::FlipY: obj.flipY = toInt(v) != 0; break;
            case key::Rotation: obj.rotation = toDouble(v).value_or(0); break;
            case key::EditorLayer: obj.editorLayer = toInt(v); break;
            case key::MainColor: obj.mainColor = toInt(v); break;
            case key::DetailColor: obj.detailColor = toInt(v); break;
            case key::ZLayer: obj.zLayer = toInt(v); break;
            case key::ZOrder: obj.zOrder = toInt(v); obj.hasZOrder = true; break;
            case key::Scale: legacyScale = toDouble(v); break;
            case key::ScaleX: obj.scaleX = toDouble(v).value_or(1); hasScaleX = true; break;
            case key::ScaleY: obj.scaleY = toDouble(v).value_or(1); hasScaleY = true; break;
            case key::EditorLayer2: obj.editorLayer2 = toInt(v); break;
            case key::MainHSV: obj.mainHSV = HSV::parse(v); break;
            case key::DetailHSV: obj.detailHSV = HSV::parse(v); break;
            case key::MainHSVEnabled:
            case key::DetailHSVEnabled:
                // Derived from the presence of the HSV value; rewritten on save.
                break;
            case key::Groups:
                for (auto g : split(v, '.')) {
                    int gi = toInt(g, 0);
                    if (gi > 0) obj.addGroup(gi);
                }
                break;
            default:
                if (k > 0) obj.extra.emplace_back(k, std::string(v));
                break;
        }
    }
    if (!hasID) return std::nullopt;
    if (legacyScale && !hasScaleX && !hasScaleY) {
        obj.scaleX = obj.scaleY = *legacyScale;
    }
    return obj;
}

std::string LevelObject::serialize() const {
    std::string out;
    out.reserve(64 + extra.size() * 8);
    auto put = [&](int k, std::string const& v) {
        if (!out.empty()) out += ',';
        out += std::to_string(k);
        out += ',';
        out += v;
    };
    put(key::ID, std::to_string(id));
    put(key::X, formatNumber(pos.x));
    put(key::Y, formatNumber(pos.y));
    if (flipX) put(key::FlipX, "1");
    if (flipY) put(key::FlipY, "1");
    if (rotation != 0) put(key::Rotation, formatNumber(rotation));
    if (editorLayer != 0) put(key::EditorLayer, std::to_string(editorLayer));
    if (mainColor != 0) put(key::MainColor, std::to_string(mainColor));
    if (detailColor != 0) put(key::DetailColor, std::to_string(detailColor));
    if (zLayer != 0) put(key::ZLayer, std::to_string(zLayer));
    if (zOrder != 0 || hasZOrder) put(key::ZOrder, std::to_string(zOrder));
    if (mainHSV && !mainHSV->isIdentity()) {
        put(key::MainHSVEnabled, "1");
        put(key::MainHSV, mainHSV->toString());
    }
    if (detailHSV && !detailHSV->isIdentity()) {
        put(key::DetailHSVEnabled, "1");
        put(key::DetailHSV, detailHSV->toString());
    }
    if (!groups.empty()) {
        std::string g;
        for (size_t i = 0; i < groups.size(); ++i) {
            if (i) g += '.';
            g += std::to_string(groups[i]);
        }
        put(key::Groups, g);
    }
    if (editorLayer2 != 0) put(key::EditorLayer2, std::to_string(editorLayer2));
    if (scaleX != 1) put(key::ScaleX, formatNumber(scaleX));
    if (scaleY != 1) put(key::ScaleY, formatNumber(scaleY));
    for (auto& [k, v] : extra) put(k, v);
    return out;
}

std::string LevelObject::canonical(bool ignoreGroups) const {
    std::map<int, std::string> kv;
    kv[key::ID] = std::to_string(id);
    kv[key::X] = formatNumber(pos.x);
    kv[key::Y] = formatNumber(pos.y);
    if (flipX) kv[key::FlipX] = "1";
    if (flipY) kv[key::FlipY] = "1";
    if (rotation != 0) kv[key::Rotation] = formatNumber(rotation);
    if (editorLayer) kv[key::EditorLayer] = std::to_string(editorLayer);
    if (mainColor) kv[key::MainColor] = std::to_string(mainColor);
    if (detailColor) kv[key::DetailColor] = std::to_string(detailColor);
    if (zLayer) kv[key::ZLayer] = std::to_string(zLayer);
    if (zOrder) kv[key::ZOrder] = std::to_string(zOrder);
    if (mainHSV && !mainHSV->isIdentity()) kv[key::MainHSV] = mainHSV->toString();
    if (detailHSV && !detailHSV->isIdentity()) kv[key::DetailHSV] = detailHSV->toString();
    if (!ignoreGroups && !groups.empty()) {
        auto sorted = groups;
        std::sort(sorted.begin(), sorted.end());
        std::string g;
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i) g += '.';
            g += std::to_string(sorted[i]);
        }
        kv[key::Groups] = g;
    }
    if (editorLayer2) kv[key::EditorLayer2] = std::to_string(editorLayer2);
    if (scaleX != 1) kv[key::ScaleX] = formatNumber(scaleX);
    if (scaleY != 1) kv[key::ScaleY] = formatNumber(scaleY);
    for (auto& [k, v] : extra) {
        // Numeric values are normalized so "1.50" and "1.5" compare equal.
        auto d = toDouble(v);
        bool numeric = d && v.find_first_not_of("0123456789.-+eE") == std::string::npos;
        kv[k] = numeric ? formatNumber(*d) : v;
    }
    std::string out;
    for (auto& [k, v] : kv) {
        if (!out.empty()) out += ',';
        out += std::to_string(k);
        out += ',';
        out += v;
    }
    return out;
}

uint64_t LevelObject::fingerprint(bool ignoreGroups) const { return hashString(canonical(ignoreGroups)); }

}  // namespace gdds
