#pragma once

// Intermediate representation of a Geometry Dash object, based on the native
// object save-string format ("1,<id>,2,<x>,3,<y>,..."). Working on that format
// keeps the engine independent from in-memory GameObject layouts, which change
// between GD versions: the mod converts editor objects with
// GameObject::getSaveString() and creates new ones with the editor's own
// "create objects from string" routine, exactly like copy/paste does.

#include "Geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gdds {

// Object string keys handled explicitly. Everything else is preserved verbatim.
namespace key {
constexpr int ID = 1;
constexpr int X = 2;
constexpr int Y = 3;
constexpr int FlipX = 4;
constexpr int FlipY = 5;
constexpr int Rotation = 6;
constexpr int EditorLayer = 20;
constexpr int MainColor = 21;
constexpr int DetailColor = 22;
constexpr int TargetColor = 23;   // color trigger target channel
constexpr int ZLayer = 24;
constexpr int ZOrder = 25;
constexpr int Scale = 32;         // legacy uniform scale
constexpr int MainHSVEnabled = 41;
constexpr int DetailHSVEnabled = 42;
constexpr int MainHSV = 43;
constexpr int DetailHSV = 44;
constexpr int TargetGroup = 51;
constexpr int Groups = 57;
constexpr int EditorLayer2 = 61;
constexpr int CenterGroup = 71;
constexpr int ScaleX = 128;
constexpr int ScaleY = 129;
}  // namespace key

// GD Z layer values as stored in the object string (key 24).
namespace zlayer {
constexpr int Default = 0;
constexpr int B5 = -5;
constexpr int B4 = -3;
constexpr int B3 = -1;
constexpr int B2 = 1;
constexpr int B1 = 3;
constexpr int T1 = 5;
constexpr int T2 = 7;
constexpr int T3 = 9;
constexpr int T4 = 11;
// Rank used to compare draw order (higher = drawn on top).
int rank(int z);
char const* name(int z);
int fromName(std::string_view n);
}  // namespace zlayer

struct HSV {
    double h = 0;       // hue shift in degrees [-180, 180]
    double s = 1;       // saturation (multiplier, or offset when sAdd)
    double v = 1;       // brightness (multiplier, or offset when vAdd)
    bool sAdd = false;
    bool vAdd = false;

    bool isIdentity() const { return h == 0 && s == (sAdd ? 0 : 1) && v == (vAdd ? 0 : 1); }
    std::string toString() const;  // "h a s a v a sAdd a vAdd"
    static std::optional<HSV> parse(std::string_view s);
    bool operator==(HSV const&) const = default;
};

struct LevelObject {
    int id = 0;
    Vec2 pos;
    double rotation = 0;     // degrees, clockwise
    double scaleX = 1;
    double scaleY = 1;
    bool flipX = false;
    bool flipY = false;
    int mainColor = 0;       // 0 = object default
    int detailColor = 0;
    int zLayer = 0;          // 0 = object default
    int zOrder = 0;
    bool hasZOrder = false;  // zOrder 0 written explicitly?
    int editorLayer = 0;
    int editorLayer2 = 0;
    std::vector<int> groups;
    std::optional<HSV> mainHSV;
    std::optional<HSV> detailHSV;
    // Keys not modelled above, preserved verbatim in their original order.
    std::vector<std::pair<int, std::string>> extra;

    std::string const* extraValue(int k) const;
    void setExtra(int k, std::string value);
    int extraInt(int k, int def = 0) const;

    bool hasGroup(int g) const;
    void addGroup(int g);
    void removeGroup(int g);

    // Native GD object string.
    std::string serialize() const;
    static std::optional<LevelObject> parse(std::string_view str);

    // Order-independent canonical form, used to fingerprint objects and to
    // compare gameplay objects before/after an operation. `ignoreGroups`
    // lets callers exclude group membership (e.g. the studio's tag group).
    std::string canonical(bool ignoreGroups = false) const;
    uint64_t fingerprint(bool ignoreGroups = false) const;
};

// Formats a number the way GD does: integers without decimals, otherwise
// up to 4 decimals with trailing zeros removed.
std::string formatNumber(double v);

// Splits on a single-character separator (no allocation of the pieces).
std::vector<std::string_view> split(std::string_view s, char sep);

}  // namespace gdds
