#include <gdds/Level.hpp>

#include <charconv>

namespace gdds {

char const* gameModeName(GameMode m) {
    switch (m) {
        case GameMode::Cube: return "Cube";
        case GameMode::Ship: return "Ship";
        case GameMode::Ball: return "Ball";
        case GameMode::Ufo: return "UFO";
        case GameMode::Wave: return "Wave";
        case GameMode::Robot: return "Robot";
        case GameMode::Spider: return "Spider";
        case GameMode::Swing: return "Swing";
    }
    return "?";
}

namespace {
int parseInt(std::string_view s, int def = 0) {
    int v = def;
    if (!s.empty() && s[0] == '+') s.remove_prefix(1);
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc()) {
        double d = 0;
        auto r2 = std::from_chars(s.data(), s.data() + s.size(), d);
        return r2.ec == std::errc() ? int(d) : def;
    }
    return v;
}
double parseDouble(std::string_view s, double def = 0) {
    double d = def;
    if (!s.empty() && s[0] == '+') s.remove_prefix(1);
    auto res = std::from_chars(s.data(), s.data() + s.size(), d);
    return res.ec == std::errc() ? d : def;
}
}  // namespace

// ---------------------------------------------------------------------------
// ColorChannel: "1_255_2_0_3_0_11_255_12_255_13_255_4_-1_6_1000_7_1_15_1_18_0_8_1"

std::string ColorChannel::serialize() const {
    std::string out;
    auto put = [&](int k, std::string const& v) {
        if (!out.empty()) out += '_';
        out += std::to_string(k);
        out += '_';
        out += v;
    };
    put(1, std::to_string(r));
    put(2, std::to_string(g));
    put(3, std::to_string(b));
    put(4, std::to_string(playerColor));
    if (blending) put(5, "1");
    put(6, std::to_string(id));
    put(7, formatNumber(opacity));
    if (copyId) put(9, std::to_string(copyId));
    if (!copyHSV.empty()) put(10, copyHSV);
    for (auto& [k, v] : extra) put(k, v);
    return out;
}

std::optional<ColorChannel> ColorChannel::parse(std::string_view s) {
    auto parts = split(s, '_');
    ColorChannel c;
    bool hasId = false;
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        int k = parseInt(parts[i], -1);
        auto v = parts[i + 1];
        switch (k) {
            case 1: c.r = parseInt(v, 255); break;
            case 2: c.g = parseInt(v, 255); break;
            case 3: c.b = parseInt(v, 255); break;
            case 4: c.playerColor = parseInt(v, -1); break;
            case 5: c.blending = parseInt(v) != 0; break;
            case 6: c.id = parseInt(v); hasId = true; break;
            case 7: c.opacity = parseDouble(v, 1); break;
            case 9: c.copyId = parseInt(v); break;
            case 10: c.copyHSV = std::string(v); break;
            default: if (k > 0) c.extra.emplace_back(k, std::string(v)); break;
        }
    }
    if (!hasId) return std::nullopt;
    return c;
}

// ---------------------------------------------------------------------------
// LevelHeader

std::string const* LevelHeader::get(std::string_view key) const {
    for (auto& [k, v] : entries) if (k == key) return &v;
    return nullptr;
}

void LevelHeader::set(std::string_view key, std::string value) {
    for (auto& [k, v] : entries) if (k == key) { v = std::move(value); return; }
    entries.emplace_back(std::string(key), std::move(value));
}

int LevelHeader::getInt(std::string_view key, int def) const {
    auto v = get(key);
    return v ? parseInt(*v, def) : def;
}

std::vector<ColorChannel> LevelHeader::colors() const {
    std::vector<ColorChannel> out;
    auto raw = get("kS38");
    if (!raw) return out;
    for (auto piece : split(*raw, '|')) {
        if (piece.empty()) continue;
        if (auto c = ColorChannel::parse(piece)) out.push_back(std::move(*c));
    }
    return out;
}

void LevelHeader::setColors(std::vector<ColorChannel> const& channels) {
    std::string s;
    for (auto& c : channels) {
        s += c.serialize();
        s += '|';
    }
    set("kS38", s);
}

std::string LevelHeader::serialize() const {
    std::string out;
    for (auto& [k, v] : entries) {
        if (!out.empty()) out += ',';
        out += k;
        out += ',';
        out += v;
    }
    return out;
}

LevelHeader LevelHeader::parse(std::string_view s) {
    LevelHeader h;
    auto parts = split(s, ',');
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        h.entries.emplace_back(std::string(parts[i]), std::string(parts[i + 1]));
    }
    return h;
}

// ---------------------------------------------------------------------------
// Level

Level Level::parse(std::string_view levelString) {
    Level level;
    auto pieces = split(levelString, ';');
    bool first = true;
    for (auto piece : pieces) {
        if (piece.empty()) { first = false; continue; }
        if (first) {
            first = false;
            // The header starts with "kS" / "kA" keys; a bare object list has none.
            if (piece.size() > 1 && piece[0] == 'k') {
                level.header = LevelHeader::parse(piece);
                continue;
            }
        }
        if (auto obj = LevelObject::parse(piece)) level.objects.push_back(std::move(*obj));
    }
    return level;
}

std::string Level::serialize() const {
    std::string out = header.serialize();
    out += ';';
    for (auto& o : objects) {
        out += o.serialize();
        out += ';';
    }
    return out;
}

LevelHeader defaultHeader() {
    LevelHeader h;
    std::vector<ColorChannel> colors;
    auto add = [&](int id, int r, int g, int b) {
        ColorChannel c;
        c.id = id; c.r = r; c.g = g; c.b = b;
        colors.push_back(c);
    };
    add(1000, 40, 125, 255);  // BG
    add(1001, 0, 102, 255);   // Ground
    add(1002, 255, 255, 255); // Line
    add(1004, 255, 255, 255); // Obj
    h.setColors(colors);
    h.set("kA13", "0");
    h.set("kA15", "0");
    h.set("kA16", "0");
    h.set("kA14", "");
    h.set("kA6", "0");
    h.set("kA7", "0");
    h.set("kA25", "0");
    h.set("kA17", "0");
    h.set("kA18", "0");
    h.set("kS39", "0");
    h.set("kA2", "0");
    h.set("kA3", "0");
    h.set("kA8", "0");
    h.set("kA4", "0");
    h.set("kA9", "0");
    h.set("kA10", "0");
    h.set("kA11", "0");
    return h;
}

}  // namespace gdds
