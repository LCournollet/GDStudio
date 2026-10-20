#pragma once

// The studio's logical layers. They are NOT GD's Z layers or editor layers:
// a studio layer is a generation unit with its own objects, palette, density,
// seed, masks and composition parameters. Every generated object is tied to
// exactly one layer and one generation operation (see Project.hpp).

#include "Json.hpp"
#include "Mask.hpp"
#include "Palette.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gdds {

enum class LayerKind : uint8_t {
    Group,               // folder for sub-layers, generates nothing itself
    BlockFill,           // base block design: fills covering the structures
    Outline,             // contour lines
    Corners,             // corner and junction pieces
    InternalTexture,     // inset outlines, patterns, depth gradients inside structures
    StructuralDetails,   // rhythmic details along faces, panel seams
    StructuralGlow,      // glow following exposed contours
    AmbientGlow,         // large soft glows behind structures
    Highlights,          // small light accents (corners, segment ends)
    ObstacleAccents,     // subtle accents behind hazards (never covering them)
    BackgroundStructures,
    BackgroundDetails,
    Atmospheric,
    Foreground,
};
constexpr int kLayerKindCount = 14;

enum class EngineKind : uint8_t { None, Structural, Lighting, Artistic };

char const* layerKindName(LayerKind k);
std::optional<LayerKind> layerKindFromName(std::string const& n);
EngineKind engineOf(LayerKind k);
// Human readable name ("Base Block Design"...).
char const* layerKindLabel(LayerKind k);
// Styles understood by the engine for this kind (first = default).
std::vector<std::string> stylesFor(LayerKind k);

// Placement anchors an object rule can be used on.
namespace anchor {
constexpr uint32_t TopFace = 1 << 0;
constexpr uint32_t BottomFace = 1 << 1;
constexpr uint32_t SideFace = 1 << 2;
constexpr uint32_t ConvexCorner = 1 << 3;
constexpr uint32_t ConcaveCorner = 1 << 4;
constexpr uint32_t Interior = 1 << 5;
constexpr uint32_t FreeSpace = 1 << 6;
constexpr uint32_t Hazard = 1 << 7;
constexpr uint32_t Any = 0xffffffff;
uint32_t fromNames(std::vector<std::string> const& names);
std::vector<std::string> toNames(uint32_t mask);
}  // namespace anchor

// Per-object usage rules inside a layer.
struct ObjectRule {
    int id = 0;
    bool enabled = true;
    double weight = 1;        // relative selection weight
    double probability = 1;   // chance to actually place when selected
    double minScale = 1;
    double maxScale = 1;
    std::vector<int> rotations{0};       // allowed rotations (degrees)
    double opacity = 1;                  // multiplies the role opacity
    std::vector<ColorRole> colors;       // allowed roles (empty = layer default)
    uint32_t anchors = anchor::Any;      // where it may be placed
    double spacing = 0;                  // min distance between two instances (units)
    double minSegment = 0;               // only on faces at least this long
    double minFreeSpace = 0;             // only where at least this much free space
    bool allowFlip = true;

    json::Value toJson() const;
    static ObjectRule fromJson(json::Value const& v);
};

struct LayerParams {
    double density = 0.5;      // 0..1 amount of decoration
    double complexity = 0.5;   // 0..1 richness of motifs
    double intensity = 1.0;    // glow/light strength (multiplies blending opacity)
    double scale = 1.0;        // size multiplier for artistic elements
    double opacity = 1.0;      // multiplies every role opacity of the layer
    uint64_t seed = 1;
    int zLayer = 0;            // GD z layer value (0 = engine default)
    int zOrder = 0;            // base z order
    bool autoZ = true;         // let the engine pick z relative to the gameplay
    BoundaryPolicy boundary = BoundaryPolicy::Extend;
    std::string style;         // engine specific style key
    std::map<std::string, double> composition;  // engine specific knobs

    double knob(std::string const& key, double def) const {
        auto it = composition.find(key);
        return it == composition.end() ? def : it->second;
    }

    json::Value toJson() const;
    static LayerParams fromJson(json::Value const& v);
};

struct Layer {
    std::string id;
    std::string name;
    std::string parentId;     // empty = top level
    LayerKind kind = LayerKind::Group;
    bool visible = true;
    bool locked = false;
    bool enabled = true;      // included in "Generate all"
    LayerParams params;
    std::vector<ObjectRule> objects;
    PaletteOverride palette;
    ColorRole defaultRole = ColorRole::Primary;
    int tagGroup = 0;         // GD group tagging the layer's objects (0 = none)

    json::Value toJson() const;
    static Layer fromJson(json::Value const& v);
};

// Ordered layer tree (order = draw/generation order inside a parent).
class LayerStack {
public:
    std::vector<Layer> const& all() const { return m_layers; }

    Layer* find(std::string const& id);
    Layer const* find(std::string const& id) const;

    // Adds a layer; generates an id if empty. Returns the stored layer.
    Layer& add(Layer layer, std::string const& afterId = "");
    // Deep duplicate (children included), with fresh ids. Returns new root id.
    std::string duplicate(std::string const& id);
    // Removes a layer and its children; returns the removed ids.
    std::vector<std::string> remove(std::string const& id);
    bool rename(std::string const& id, std::string const& name);
    // Moves a layer by `delta` positions among its siblings.
    bool move(std::string const& id, int delta);
    // Re-parents a layer (empty parent = top level). Refuses cycles.
    bool reparent(std::string const& id, std::string const& parentId);

    std::vector<std::string> childrenOf(std::string const& parentId) const;
    // Layer + all descendants, in order.
    std::vector<std::string> subtree(std::string const& id) const;
    // Effective lock/visibility (a locked parent locks its children).
    bool isLocked(std::string const& id) const;
    bool isVisible(std::string const& id) const;
    int depthOf(std::string const& id) const;
    // Depth-first ordered list for display.
    std::vector<std::string> displayOrder() const;

    json::Value toJson() const;
    void loadJson(json::Value const& v);

    static std::string newId(std::string const& hint);

private:
    std::vector<Layer> m_layers;
};

}  // namespace gdds
