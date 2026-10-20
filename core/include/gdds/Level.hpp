#pragma once

// A whole level: the start-object header ("kS38,...,kA2,...") followed by the
// objects, separated by ';'. Used by the command line tools and the tests; the
// mod reads objects straight from the editor instead.

#include "LevelObject.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdds {

enum class GameMode { Cube = 0, Ship = 1, Ball = 2, Ufo = 3, Wave = 4, Robot = 5, Spider = 6, Swing = 7 };
char const* gameModeName(GameMode m);

// One entry of the level's color channel table (header key kS38).
struct ColorChannel {
    int id = 0;
    int r = 255, g = 255, b = 255;
    double opacity = 1.0;
    bool blending = false;
    int playerColor = -1;    // -1 none, 1 = P1, 2 = P2
    int copyId = 0;          // copy color source channel (0 = none)
    std::string copyHSV;     // raw HSV string when copying
    std::vector<std::pair<int, std::string>> extra;

    std::string serialize() const;  // "1_r_2_g_3_b_..."
    static std::optional<ColorChannel> parse(std::string_view s);
};

struct LevelHeader {
    std::vector<std::pair<std::string, std::string>> entries;

    std::string const* get(std::string_view key) const;
    void set(std::string_view key, std::string value);
    int getInt(std::string_view key, int def = 0) const;

    std::vector<ColorChannel> colors() const;
    void setColors(std::vector<ColorChannel> const& channels);

    GameMode startMode() const { return GameMode(getInt("kA2", 0)); }
    bool startMini() const { return getInt("kA3", 0) != 0; }
    int startSpeed() const { return getInt("kA4", 0); }  // 0 = 1x, 1 = 0.5x, 2 = 2x, 3 = 3x, 4 = 4x
    bool startDual() const { return getInt("kA8", 0) != 0; }
    bool startFlipped() const { return getInt("kA11", 0) != 0; }

    std::string serialize() const;
    static LevelHeader parse(std::string_view s);
};

struct Level {
    LevelHeader header;
    std::vector<LevelObject> objects;

    // Parses a decompressed level string. Malformed objects are skipped.
    static Level parse(std::string_view levelString);
    std::string serialize() const;
};

// Default header for generated test levels (cube, normal speed, default colors).
LevelHeader defaultHeader();

}  // namespace gdds
