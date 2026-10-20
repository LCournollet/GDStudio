#include <gdds/Layer.hpp>
#include <gdds/Random.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>

namespace gdds {

namespace {
struct KindName { LayerKind kind; char const* name; };
constexpr KindName kKindNames[] = {
    {LayerKind::Group, "group"},
    {LayerKind::BlockFill, "block-fill"},
    {LayerKind::Outline, "outline"},
    {LayerKind::Corners, "corners"},
    {LayerKind::InternalTexture, "internal-texture"},
    {LayerKind::StructuralDetails, "structural-details"},
    {LayerKind::StructuralGlow, "structural-glow"},
    {LayerKind::AmbientGlow, "ambient-glow"},
    {LayerKind::Highlights, "highlights"},
    {LayerKind::ObstacleAccents, "obstacle-accents"},
    {LayerKind::BackgroundStructures, "background-structures"},
    {LayerKind::BackgroundDetails, "background-details"},
    {LayerKind::Atmospheric, "atmospheric"},
    {LayerKind::Foreground, "foreground"},
};
}  // namespace

char const* layerKindName(LayerKind k) {
    for (auto& kn : kKindNames) if (kn.kind == k) return kn.name;
    return "group";
}

std::optional<LayerKind> layerKindFromName(std::string const& n) {
    for (auto& kn : kKindNames) if (n == kn.name) return kn.kind;
    return std::nullopt;
}

char const* layerKindLabel(LayerKind k) {
    switch (k) {
        case LayerKind::Group: return "Group";
        case LayerKind::BlockFill: return "Base Block Design";
        case LayerKind::Outline: return "Outlines";
        case LayerKind::Corners: return "Corners & Connections";
        case LayerKind::InternalTexture: return "Internal Textures";
        case LayerKind::StructuralDetails: return "Structural Details";
        case LayerKind::StructuralGlow: return "Structural Glow";
        case LayerKind::AmbientGlow: return "Ambient Glow";
        case LayerKind::Highlights: return "Highlights";
        case LayerKind::ObstacleAccents: return "Obstacle Accents";
        case LayerKind::BackgroundStructures: return "Background Structures";
        case LayerKind::BackgroundDetails: return "Background Details";
        case LayerKind::Atmospheric: return "Atmospheric Details";
        case LayerKind::Foreground: return "Foreground";
    }
    return "Layer";
}

std::vector<std::string> stylesFor(LayerKind k) {
    switch (k) {
        case LayerKind::BlockFill: return {"solid", "depth"};
        case LayerKind::Corners: return {"brackets", "squares"};
        case LayerKind::InternalTexture: return {"inset", "grid", "stripes", "circuit", "panels"};
        case LayerKind::StructuralDetails: return {"rivets", "lights", "studs"};
        case LayerKind::StructuralGlow: return {"exposed", "top"};
        case LayerKind::Highlights: return {"corners", "segment-ends"};
        case LayerKind::BackgroundStructures: return {"pillars", "skyline", "geometric", "girders"};
        case LayerKind::BackgroundDetails: return {"scatter", "lines", "grid-dots"};
        case LayerKind::Atmospheric: return {"particles", "chains", "dust"};
        default: return {"default"};
    }
}

EngineKind engineOf(LayerKind k) {
    switch (k) {
        case LayerKind::Group: return EngineKind::None;
        case LayerKind::BlockFill:
        case LayerKind::Outline:
        case LayerKind::Corners:
        case LayerKind::InternalTexture:
        case LayerKind::StructuralDetails:
        case LayerKind::ObstacleAccents: return EngineKind::Structural;
        case LayerKind::StructuralGlow:
        case LayerKind::AmbientGlow:
        case LayerKind::Highlights: return EngineKind::Lighting;
        case LayerKind::BackgroundStructures:
        case LayerKind::BackgroundDetails:
        case LayerKind::Atmospheric:
        case LayerKind::Foreground: return EngineKind::Artistic;
    }
    return EngineKind::None;
}

namespace anchor {
namespace {
struct AnchorName { uint32_t bit; char const* name; };
constexpr AnchorName kAnchorNames[] = {
    {TopFace, "top"}, {BottomFace, "bottom"}, {SideFace, "side"}, {ConvexCorner, "convex"},
    {ConcaveCorner, "concave"}, {Interior, "interior"}, {FreeSpace, "free"}, {Hazard, "hazard"},
};
}  // namespace
uint32_t fromNames(std::vector<std::string> const& names) {
    if (names.empty()) return Any;
    uint32_t m = 0;
    for (auto& n : names) {
        if (n == "any") return Any;
        for (auto& an : kAnchorNames) if (n == an.name) m |= an.bit;
    }
    return m ? m : Any;
}
std::vector<std::string> toNames(uint32_t mask) {
    if (mask == Any) return {"any"};
    std::vector<std::string> out;
    for (auto& an : kAnchorNames) if (mask & an.bit) out.push_back(an.name);
    return out;
}
}  // namespace anchor

// ---------------------------------------------------------------------------
// JSON

json::Value ObjectRule::toJson() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["enabled"] = enabled;
    v["weight"] = weight;
    v["probability"] = probability;
    v["minScale"] = minScale;
    v["maxScale"] = maxScale;
    json::Value rot = json::Value::array();
    for (int r : rotations) rot.push(r);
    v["rotations"] = rot;
    v["opacity"] = opacity;
    json::Value cols = json::Value::array();
    for (auto c : colors) cols.push(colorRoleName(c));
    v["colors"] = cols;
    json::Value anc = json::Value::array();
    for (auto& n : anchor::toNames(anchors)) anc.push(n);
    v["anchors"] = anc;
    v["spacing"] = spacing;
    v["minSegment"] = minSegment;
    v["minFreeSpace"] = minFreeSpace;
    v["allowFlip"] = allowFlip;
    return v;
}

ObjectRule ObjectRule::fromJson(json::Value const& v) {
    ObjectRule r;
    r.id = v.integer("id");
    r.enabled = v.boolean("enabled", true);
    r.weight = v.num("weight", 1);
    r.probability = v.num("probability", 1);
    r.minScale = v.num("minScale", 1);
    r.maxScale = v.num("maxScale", r.minScale);
    if (v.contains("rotations")) {
        r.rotations.clear();
        for (auto& x : v.get("rotations").items()) r.rotations.push_back(x.asInt());
        if (r.rotations.empty()) r.rotations.push_back(0);
    }
    r.opacity = v.num("opacity", 1);
    for (auto& c : v.get("colors").items())
        if (auto role = colorRoleFromName(c.asString())) r.colors.push_back(*role);
    std::vector<std::string> anc;
    for (auto& a : v.get("anchors").items()) anc.push_back(a.asString());
    r.anchors = anchor::fromNames(anc);
    r.spacing = v.num("spacing", 0);
    r.minSegment = v.num("minSegment", 0);
    r.minFreeSpace = v.num("minFreeSpace", 0);
    r.allowFlip = v.boolean("allowFlip", true);
    return r;
}

json::Value LayerParams::toJson() const {
    json::Value v = json::Value::object();
    v["density"] = density;
    v["complexity"] = complexity;
    v["intensity"] = intensity;
    v["scale"] = scale;
    v["opacity"] = opacity;
    v["seed"] = std::to_string(seed);
    v["zLayer"] = zLayer;
    v["zOrder"] = zOrder;
    v["autoZ"] = autoZ;
    v["boundary"] = boundaryPolicyName(boundary);
    v["style"] = style;
    json::Value comp = json::Value::object();
    for (auto& [k, x] : composition) comp[k] = x;
    v["composition"] = comp;
    return v;
}

LayerParams LayerParams::fromJson(json::Value const& v) {
    LayerParams p;
    p.density = v.num("density", p.density);
    p.complexity = v.num("complexity", p.complexity);
    p.intensity = v.num("intensity", p.intensity);
    p.scale = v.num("scale", p.scale);
    p.opacity = v.num("opacity", p.opacity);
    p.seed = v.get("seed").isString() ? v.get("seed").asUInt64(1) : uint64_t(v.num("seed", 1));
    p.zLayer = v.integer("zLayer", 0);
    p.zOrder = v.integer("zOrder", 0);
    p.autoZ = v.boolean("autoZ", true);
    p.boundary = boundaryPolicyFromName(v.str("boundary", "extend"));
    p.style = v.str("style");
    for (auto& [k, x] : v.get("composition").members()) p.composition[k] = x.asDouble();
    return p;
}

json::Value Layer::toJson() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["name"] = name;
    v["parent"] = parentId;
    v["kind"] = layerKindName(kind);
    v["visible"] = visible;
    v["locked"] = locked;
    v["enabled"] = enabled;
    v["params"] = params.toJson();
    json::Value objs = json::Value::array();
    for (auto& o : objects) objs.push(o.toJson());
    v["objects"] = objs;
    v["palette"] = palette.toJson();
    v["defaultRole"] = colorRoleName(defaultRole);
    v["tagGroup"] = tagGroup;
    return v;
}

Layer Layer::fromJson(json::Value const& v) {
    Layer l;
    l.id = v.str("id");
    l.name = v.str("name");
    l.parentId = v.str("parent");
    l.kind = layerKindFromName(v.str("kind")).value_or(LayerKind::Group);
    l.visible = v.boolean("visible", true);
    l.locked = v.boolean("locked", false);
    l.enabled = v.boolean("enabled", true);
    l.params = LayerParams::fromJson(v.get("params"));
    for (auto& o : v.get("objects").items()) l.objects.push_back(ObjectRule::fromJson(o));
    l.palette = PaletteOverride::fromJson(v.get("palette"));
    l.defaultRole = colorRoleFromName(v.str("defaultRole", "primary")).value_or(ColorRole::Primary);
    l.tagGroup = v.integer("tagGroup", 0);
    return l;
}

// ---------------------------------------------------------------------------
// LayerStack

std::string LayerStack::newId(std::string const& hint) {
    static std::atomic<uint64_t> counter{0};
    uint64_t t = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t h = hashOf(t, counter.fetch_add(1), hashString(hint));
    static char const* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    for (int i = 0; i < 10; ++i) { s += digits[h % 36]; h /= 36; }
    std::string base;
    for (char c : hint) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) base += c;
        else if (c >= 'A' && c <= 'Z') base += char(c - 'A' + 'a');
        else if (!base.empty() && base.back() != '-') base += '-';
        if (base.size() >= 16) break;
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    return (base.empty() ? "layer" : base) + "-" + s;
}

Layer* LayerStack::find(std::string const& id) {
    for (auto& l : m_layers) if (l.id == id) return &l;
    return nullptr;
}

Layer const* LayerStack::find(std::string const& id) const {
    for (auto& l : m_layers) if (l.id == id) return &l;
    return nullptr;
}

Layer& LayerStack::add(Layer layer, std::string const& afterId) {
    if (layer.id.empty() || find(layer.id)) layer.id = newId(layer.name);
    if (!layer.parentId.empty() && !find(layer.parentId)) layer.parentId.clear();
    if (!afterId.empty()) {
        auto it = std::find_if(m_layers.begin(), m_layers.end(), [&](Layer const& l) { return l.id == afterId; });
        if (it != m_layers.end()) {
            // Insert after the whole subtree of afterId.
            auto sub = subtree(afterId);
            auto last = std::find_if(m_layers.begin(), m_layers.end(), [&](Layer const& l) { return l.id == sub.back(); });
            return *m_layers.insert(last + 1, std::move(layer));
        }
    }
    m_layers.push_back(std::move(layer));
    return m_layers.back();
}

std::string LayerStack::duplicate(std::string const& id) {
    auto ids = subtree(id);
    if (ids.empty()) return "";
    std::map<std::string, std::string> remap;
    std::vector<Layer> copies;
    for (auto& oldId : ids) {
        Layer copy = *find(oldId);
        std::string fresh = newId(copy.name);
        remap[oldId] = fresh;
        copy.id = fresh;
        if (oldId == id) copy.name += " copy";
        copy.tagGroup = 0;  // allocated again on first generation
        copy.params.seed = hashOf(copy.params.seed, hashString(fresh)) % 1000000;
        copies.push_back(std::move(copy));
    }
    for (auto& c : copies) {
        auto it = remap.find(c.parentId);
        if (it != remap.end()) c.parentId = it->second;
    }
    std::string after = ids.back();
    auto pos = std::find_if(m_layers.begin(), m_layers.end(), [&](Layer const& l) { return l.id == after; });
    m_layers.insert(pos + 1, copies.begin(), copies.end());
    return remap[id];
}

std::vector<std::string> LayerStack::remove(std::string const& id) {
    auto ids = subtree(id);
    m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
                                  [&](Layer const& l) { return std::find(ids.begin(), ids.end(), l.id) != ids.end(); }),
                   m_layers.end());
    return ids;
}

bool LayerStack::rename(std::string const& id, std::string const& name) {
    auto l = find(id);
    if (!l || name.empty()) return false;
    l->name = name;
    return true;
}

bool LayerStack::move(std::string const& id, int delta) {
    auto l = find(id);
    if (!l || delta == 0) return false;
    auto siblings = childrenOf(l->parentId);
    auto it = std::find(siblings.begin(), siblings.end(), id);
    int idx = int(it - siblings.begin());
    int target = std::clamp(idx + delta, 0, int(siblings.size()) - 1);
    if (target == idx) return false;
    // Extract this subtree and reinsert it before/after the target sibling's subtree.
    auto mine = subtree(id);
    std::vector<Layer> block;
    for (auto& mid : mine) block.push_back(*find(mid));
    m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
                                  [&](Layer const& x) { return std::find(mine.begin(), mine.end(), x.id) != mine.end(); }),
                   m_layers.end());
    auto targetSub = subtree(siblings[target]);
    std::string anchorId = delta > 0 ? targetSub.back() : targetSub.front();
    auto pos = std::find_if(m_layers.begin(), m_layers.end(), [&](Layer const& x) { return x.id == anchorId; });
    if (delta > 0) ++pos;
    m_layers.insert(pos, block.begin(), block.end());
    return true;
}

bool LayerStack::reparent(std::string const& id, std::string const& parentId) {
    auto l = find(id);
    if (!l) return false;
    if (!parentId.empty()) {
        auto p = find(parentId);
        if (!p) return false;
        auto sub = subtree(id);
        if (std::find(sub.begin(), sub.end(), parentId) != sub.end()) return false;  // cycle
    }
    l->parentId = parentId;
    return true;
}

std::vector<std::string> LayerStack::childrenOf(std::string const& parentId) const {
    std::vector<std::string> out;
    for (auto& l : m_layers) if (l.parentId == parentId) out.push_back(l.id);
    return out;
}

std::vector<std::string> LayerStack::subtree(std::string const& id) const {
    std::vector<std::string> out;
    if (!find(id)) return out;
    std::function<void(std::string const&)> walk = [&](std::string const& cur) {
        out.push_back(cur);
        for (auto& c : childrenOf(cur)) walk(c);
    };
    walk(id);
    return out;
}

bool LayerStack::isLocked(std::string const& id) const {
    for (auto l = find(id); l; l = l->parentId.empty() ? nullptr : find(l->parentId))
        if (l->locked) return true;
    return false;
}

bool LayerStack::isVisible(std::string const& id) const {
    for (auto l = find(id); l; l = l->parentId.empty() ? nullptr : find(l->parentId))
        if (!l->visible) return false;
    return true;
}

int LayerStack::depthOf(std::string const& id) const {
    int d = 0;
    for (auto l = find(id); l && !l->parentId.empty(); l = find(l->parentId)) ++d;
    return d;
}

std::vector<std::string> LayerStack::displayOrder() const {
    std::vector<std::string> out;
    for (auto& root : childrenOf("")) {
        auto sub = subtree(root);
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

json::Value LayerStack::toJson() const {
    json::Value arr = json::Value::array();
    for (auto& l : m_layers) arr.push(l.toJson());
    return arr;
}

void LayerStack::loadJson(json::Value const& v) {
    m_layers.clear();
    for (auto& l : v.items()) m_layers.push_back(Layer::fromJson(l));
}

}  // namespace gdds
