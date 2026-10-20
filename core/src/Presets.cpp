#include <gdds/Presets.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace gdds {

namespace {

struct Obj {
    int id;
    double weight = 1;
    std::vector<int> rotations = {0};
    double minScale = 1, maxScale = 1;
    std::vector<ColorRole> colors = {};
};

struct Spec {
    LayerKind kind;
    char const* name;
    char const* group;  // "Structural", "Lighting", "Artistic"
    double density = 0.5;
    double complexity = 0.5;
    double intensity = 1.0;
    double scale = 1.0;
    double opacity = 1.0;
    char const* style = "";
    std::vector<std::pair<char const*, double>> knobs = {};
    std::vector<Obj> objects = {};
    bool enabled = true;
    ColorRole role = ColorRole::Primary;
};

RoleColor rc(char const* hex, double opacity = 1, bool blending = false) {
    return {Color3::fromHex(hex).value_or(Color3{}), opacity, blending};
}

Palette palette(char const* name, char const* primary, char const* secondary, char const* accent, RoleColor glow,
                RoleColor detail, char const* background, char const* foreground, double variation = 0.0) {
    Palette p;
    p.name = name;
    p.role(ColorRole::Primary) = rc(primary);
    p.role(ColorRole::Secondary) = rc(secondary);
    p.role(ColorRole::Accent) = rc(accent);
    p.role(ColorRole::Glow) = glow;
    p.role(ColorRole::Detail) = detail;
    p.role(ColorRole::Background) = rc(background);
    p.role(ColorRole::Foreground) = rc(foreground);
    p.adjust.variation = variation;
    return p;
}

json::Value buildConfig(Palette const& pal, std::vector<Spec> const& specs, uint64_t seed) {
    LayerStack stack;
    std::map<std::string, std::string> groups;
    for (char const* g : {"Structural", "Lighting", "Artistic"}) {
        Layer group;
        group.name = g;
        group.kind = LayerKind::Group;
        group.id = std::string("group-") + g;
        for (auto& c : group.id) c = char(std::tolower(static_cast<unsigned char>(c)));
        groups[g] = stack.add(group).id;
    }
    int index = 0;
    for (auto& s : specs) {
        Layer l;
        l.name = s.name;
        l.kind = s.kind;
        l.parentId = groups[s.group];
        l.id = std::string(layerKindName(s.kind));
        l.enabled = s.enabled;
        l.defaultRole = s.role;
        l.params.density = s.density;
        l.params.complexity = s.complexity;
        l.params.intensity = s.intensity;
        l.params.scale = s.scale;
        l.params.opacity = s.opacity;
        l.params.style = s.style;
        l.params.seed = 1000 + uint64_t(index++) * 7919 + seed;
        for (auto& [k, v] : s.knobs) l.params.composition[k] = v;
        for (auto& o : s.objects) {
            ObjectRule r;
            r.id = o.id;
            r.weight = o.weight;
            r.rotations = o.rotations;
            r.minScale = o.minScale;
            r.maxScale = o.maxScale;
            r.colors = o.colors;
            l.objects.push_back(r);
        }
        stack.add(l);
    }
    json::Value cfg = json::Value::object();
    cfg["palette"] = pal.toJson();
    cfg["layers"] = stack.toJson();
    GenerationSettings settings;
    settings.globalSeed = seed;
    cfg["settings"] = settings.toJson();
    return cfg;
}

Preset makePreset(char const* name, char const* description, Palette const& pal, std::vector<Spec> const& specs,
                  uint64_t seed) {
    Preset p;
    p.name = name;
    p.description = description;
    p.config = buildConfig(pal, specs, seed);
    p.config["settings"]["presetName"] = name;
    return p;
}

using K = LayerKind;
using R = ColorRole;

std::vector<Preset> makeBuiltins() {
    std::vector<Preset> out;

    // ---------------------------------------------------------------- Modern Glow
    out.push_back(makePreset(
        "Modern Glow",
        "Deep shaded blocks, crisp pink contours, inset lines and soft cyan light. Pillars and rings in the background.",
        palette("Modern Glow", "#1B1033", "#FF4FD8", "#FFFFFF", rc("#00E5FF", 0.6, true), rc("#8C6CFF", 0.75),
                "#2A1A4A", "#0A0614", 0.25),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "depth", {{"bands", 3}, {"shade", 0.13}}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 3}}, {{1753}}},
            {K::Corners, "Corners & Connections", "Structural", 0.55, 0.5, 1, 1, 1, "brackets", {{"size", 12}, {"inset", 6}},
             {{2857, 3}, {1764, 1}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.6, 0.5, 1, 1, 1, "inset",
             {{"inset", 2}, {"thickness", 1.5}, {"opacity", 0.7}}, {{1753}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.35, 0.5, 1, 1, 1, "rivets", {{"faces", 1}, {"offset", 9}},
             {{1764}}},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.6, 0.5, 1, 1, 1, "", {{"opacity", 0.45}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.8, 0.5, 1, 1, 1, "exposed", {{"size", 1.0}}, {{1011}, {3958}}},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.5, 0.5, 1, 1, 1, "", {{"opacity", 0.25}}, {{3959}}},
            {K::Highlights, "Highlights", "Lighting", 0.35, 0.5, 1, 1, 1, "corners", {{"size", 9}}, {{3966}, {3958}}},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.55, 0.6, 1, 1, 0.9, "pillars", {{"width", 2}},
             {{211}, {1753}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.35, 0.5, 1, 1, 1, "scatter", {{"spacing", 3}},
             {{3807, 2}, {3802, 1}, {3805, 1, {0, 45}}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.3, 0.5, 1, 1, 1, "particles", {}, {{1764, 2}, {3883, 1}}},
            {K::Foreground, "Foreground", "Artistic", 0.2, 0.5, 1, 1, 1, "", {{"opacity", 0.95}}, {{211}}, false},
        },
        101));

    // ---------------------------------------------------------------- Minimal
    out.push_back(makePreset(
        "Minimal",
        "Flat dark blocks with a single thin white contour and discreet corner squares. Almost nothing else: maximum readability.",
        palette("Minimal", "#0B0B0B", "#F2F2F2", "#F2F2F2", rc("#FFFFFF", 0.2, true), rc("#7A7A7A", 0.6), "#3A3A3A", "#050505"),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.2, 1, 1, 1, "solid", {}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.2, 1, 1, 1, "", {{"thickness", 2}}, {{1753}}},
            {K::Corners, "Corners & Connections", "Structural", 0.3, 0.2, 1, 1, 1, "squares", {{"size", 10}, {"inset", 5}, {"concave", 0}},
             {{917}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.0, 0.2, 1, 1, 1, "inset", {}, {{1753}}, false},
            {K::StructuralDetails, "Structural Details", "Structural", 0.0, 0.2, 1, 1, 1, "rivets", {}, {{1764}}, false},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.0, 0.2, 1, 1, 1, "", {}, {{1011}}, false},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.0, 0.2, 1, 1, 1, "top", {}, {{1011}}, false},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.0, 0.2, 1, 1, 1, "", {}, {{3959}}, false},
            {K::Highlights, "Highlights", "Lighting", 0.0, 0.2, 1, 1, 1, "", {}, {{3966}}, false},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.0, 0.2, 1, 1, 1, "pillars", {}, {{211}}, false},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.15, 0.2, 1, 1, 1, "lines", {{"spacing", 5}}, {{1753}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.0, 0.2, 1, 1, 1, "particles", {}, {{1764}}, false},
            {K::Foreground, "Foreground", "Artistic", 0.0, 0.2, 1, 1, 1, "", {}, {{211}}, false},
        },
        202));

    // ---------------------------------------------------------------- Dark Tech
    out.push_back(makePreset(
        "Dark Tech",
        "Near-black panels split by seams, blue 2-unit contours, small status lights and a dotted grid behind the gameplay.",
        palette("Dark Tech", "#0D1117", "#2F81F7", "#58A6FF", rc("#1F6FEB", 0.45, true), rc("#30363D", 1.0), "#111823", "#05070A",
                0.1),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "depth", {{"bands", 2}, {"shade", 0.22}}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 2.5}}, {{507}}},
            {K::Corners, "Corners & Connections", "Structural", 0.6, 0.5, 1, 1, 1, "brackets", {{"size", 10}, {"inset", 5}},
             {{2857}, {917}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.6, 0.5, 1, 1, 1, "panels",
             {{"period", 4}, {"inset", 1}, {"thickness", 1.5}, {"opacity", 0.9}}, {{1753}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.45, 0.5, 1, 1, 1, "lights",
             {{"faces", 3}, {"size", 4}, {"offset", 8}}, {{917}, {3825}}},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.4, 0.5, 1, 1, 1, "", {{"opacity", 0.4}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.7, 0.5, 0.8, 0.8, 1, "top", {{"size", 0.8}}, {{1011}, {3958}}},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.0, 0.5, 1, 1, 1, "", {}, {{3959}}, false},
            {K::Highlights, "Highlights", "Lighting", 0.0, 0.5, 1, 1, 1, "", {}, {{3966}}, false},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.5, 0.7, 1, 1, 1, "pillars", {{"width", 2}},
             {{211}, {1753}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.45, 0.5, 1, 1, 1, "grid-dots", {{"spacing", 2}},
             {{1764}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.0, 0.5, 1, 1, 1, "particles", {}, {{1764}}, false},
            {K::Foreground, "Foreground", "Artistic", 0.0, 0.5, 1, 1, 1, "", {}, {{211}}, false},
        },
        303));

    // ---------------------------------------------------------------- Neon
    out.push_back(makePreset(
        "Neon",
        "Black blocks with a double neon contour (magenta outside, cyan inside), strong additive glow, neon rings and sparks.",
        palette("Neon", "#07010F", "#FF2BD6", "#00F0FF", rc("#FF2BD6", 0.7, true), rc("#00F0FF", 0.95, true), "#14002B",
                "#000000"),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "solid", {}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 3.5}}, {{1753}}},
            {K::Corners, "Corners & Connections", "Structural", 0.0, 0.5, 1, 1, 1, "", {}, {{2857}}, false},
            {K::InternalTexture, "Internal Textures", "Structural", 1, 0.5, 1, 1, 1, "inset",
             {{"inset", 1}, {"thickness", 1.5}, {"opacity", 1.0}}, {{1753}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.0, 0.5, 1, 1, 1, "", {}, {{1764}}, false},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.7, 0.5, 1, 1, 1, "", {{"opacity", 0.6}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 1, 0.5, 1.2, 1.3, 1, "exposed", {{"size", 1.3}, {"variation", 0.15}},
             {{1011}, {3958}}},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.6, 0.5, 1, 1.2, 1, "", {{"opacity", 0.3}}, {{3959}}},
            {K::Highlights, "Highlights", "Lighting", 0.5, 0.5, 1, 1, 1, "corners", {{"size", 11}}, {{3966}, {3958}}},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.55, 0.6, 1, 1, 1, "geometric", {{"opacity", 0.4}, {"maxSize", 6}},
             {{236, 2}, {3807, 1}, {2975, 1, {45}}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.3, 0.5, 1, 1, 1, "scatter", {{"spacing", 3.5}},
             {{3807}, {3883}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.45, 0.5, 1, 1, 1, "particles", {}, {{1764, 2}, {3883, 1}}},
            {K::Foreground, "Foreground", "Artistic", 0.0, 0.5, 1, 1, 1, "", {}, {{211}}, false},
        },
        404));

    // ---------------------------------------------------------------- Industrial
    out.push_back(makePreset(
        "Industrial",
        "Heavy grey plates with thick amber edges, hazard stripes, rivets, steel girders and hanging chains.",
        palette("Industrial", "#2B2B2B", "#F5A623", "#D9D9D9", rc("#F5A623", 0.3, true), rc("#5A5A5A", 0.8), "#1C1C1C",
                "#0D0D0D", 0.15),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "depth", {{"bands", 2}, {"shade", 0.15}}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 5}}, {{1191}, {507}}},
            {K::Corners, "Corners & Connections", "Structural", 0.6, 0.5, 1, 1, 1, "brackets", {{"size", 14}, {"inset", 7}},
             {{1192}, {1764}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.7, 0.5, 1, 1, 1, "stripes",
             {{"inset", 2}, {"every", 2}, {"thickness", 4}, {"opacity", 0.8}}, {{507}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.6, 0.5, 1, 1, 1, "rivets",
             {{"faces", 5}, {"size", 5}, {"offset", 11}}, {{1764}}},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.4, 0.5, 1, 1, 1, "", {{"opacity", 0.35}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.0, 0.5, 1, 1, 1, "top", {}, {{1011}}, false},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.3, 0.5, 1, 1, 1, "", {{"opacity", 0.18}}, {{3959}}},
            {K::Highlights, "Highlights", "Lighting", 0.0, 0.5, 1, 1, 1, "", {}, {{3966}}, false},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.6, 0.5, 1, 1, 1, "girders", {{"thickness", 4}},
             {{507}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.2, 0.5, 1, 1, 1, "scatter", {{"spacing", 4}},
             {{3805, 1, {0}}, {917}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.6, 0.5, 1, 1, 1, "chains", {}, {{41}, {1766}}},
            {K::Foreground, "Foreground", "Artistic", 0.3, 0.5, 1, 1, 1, "", {{"opacity", 1}}, {{211}}},
        },
        505));

    // ---------------------------------------------------------------- Cyberpunk
    out.push_back(makePreset(
        "Cyberpunk",
        "Dark violet blocks with circuit traces and yellow pads, pink contours, cyan glow, a lit skyline and data streams.",
        palette("Cyberpunk", "#140A1F", "#FF3CAC", "#F9F871", rc("#2BD2FF", 0.55, true), rc("#784BA0", 0.9), "#1E0D35",
                "#08040F", 0.2),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "solid", {}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 2.5}}, {{1753}}},
            {K::Corners, "Corners & Connections", "Structural", 0.5, 0.5, 1, 1, 1, "brackets", {{"size", 11}, {"inset", 5}},
             {{2857}, {917}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.8, 0.7, 1, 1, 1, "circuit",
             {{"inset", 2}, {"thickness", 1.5}, {"opacity", 0.9}, {"stub", 1}}, {{1753}, {917}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.5, 0.5, 1, 1, 1, "lights",
             {{"faces", 2}, {"size", 4}, {"offset", 8}}, {{917}, {3825}}},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.6, 0.5, 1, 1, 1, "", {{"opacity", 0.5}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.8, 0.5, 1, 1.1, 1, "exposed", {{"size", 1.1}}, {{1011}, {3958}}},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.3, 0.5, 1, 1, 1, "", {{"opacity", 0.22}}, {{3959}}},
            {K::Highlights, "Highlights", "Lighting", 0.4, 0.5, 1, 1, 1, "segment-ends", {{"size", 9}}, {{3966}, {3958}}},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.7, 0.7, 1, 1, 1, "skyline", {}, {{211}, {917}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.35, 0.5, 1, 1, 1, "lines", {{"spacing", 3}}, {{1753}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.35, 0.5, 1, 1, 1, "particles", {}, {{1764, 2}, {3883, 1}}},
            {K::Foreground, "Foreground", "Artistic", 0.25, 0.5, 1, 1, 1, "", {{"opacity", 1}}, {{211}}},
        },
        606));

    // ---------------------------------------------------------------- Abstract
    out.push_back(makePreset(
        "Abstract",
        "Navy blocks shaded by depth with a dotted interior, and large geometric compositions (circles, hexagons, "
        "diamonds) floating in the open space.",
        palette("Abstract", "#2B4C7E", "#F1FAEE", "#E63946", rc("#A8DADC", 0.4, true), rc("#457B9D", 0.85), "#16253F",
                "#0B132B", 0.3),
        {
            {K::BlockFill, "Base Block Design", "Structural", 1, 0.5, 1, 1, 1, "depth", {{"bands", 4}, {"shade", 0.1}}, {{211}}},
            {K::Outline, "Outlines", "Structural", 1, 0.5, 1, 1, 1, "", {{"thickness", 2}}, {{1753}}},
            {K::Corners, "Corners & Connections", "Structural", 0.4, 0.5, 1, 1, 1, "squares", {{"size", 10}, {"inset", 6}},
             {{917}, {1764}}},
            {K::InternalTexture, "Internal Textures", "Structural", 0.7, 0.5, 1, 1, 1, "grid",
             {{"inset", 2}, {"period", 2}, {"dotSize", 5}, {"opacity", 0.8}}, {{3802}}},
            {K::StructuralDetails, "Structural Details", "Structural", 0.0, 0.5, 1, 1, 1, "", {}, {{1764}}, false},
            {K::ObstacleAccents, "Obstacle Accents", "Structural", 0.4, 0.5, 1, 1, 1, "", {{"opacity", 0.35}}, {{1011}, {3958}}},
            {K::StructuralGlow, "Structural Glow", "Lighting", 0.0, 0.5, 1, 1, 1, "", {}, {{1011}}, false},
            {K::AmbientGlow, "Ambient Glow", "Lighting", 0.5, 0.5, 1, 1.3, 1, "", {{"opacity", 0.22}}, {{3959}}},
            {K::Highlights, "Highlights", "Lighting", 0.0, 0.5, 1, 1, 1, "", {}, {{3966}}, false},
            {K::BackgroundStructures, "Background Structures", "Artistic", 0.65, 0.8, 1, 1.1, 1, "geometric", {{"opacity", 0.45}, {"maxSize", 7}},
             {{497, 2}, {236, 2}, {3801, 1}, {2975, 1, {45}}, {3803, 1}}},
            {K::BackgroundDetails, "Background Details", "Artistic", 0.3, 0.5, 1, 1, 1, "scatter", {{"spacing", 3}},
             {{3802}, {3807}, {3803}, {3805, 1, {0, 45}}}},
            {K::Atmospheric, "Atmospheric Details", "Artistic", 0.35, 0.5, 1, 1, 1, "dust", {}, {{3980}, {3981}}},
            {K::Foreground, "Foreground", "Artistic", 0.0, 0.5, 1, 1, 1, "", {}, {{211}}, false},
        },
        707));
    return out;
}

}  // namespace

json::Value Preset::toJson() const {
    json::Value v = json::Value::object();
    v["format"] = "gd-decoration-studio-preset";
    v["version"] = 1;
    v["name"] = name;
    v["description"] = description;
    v["config"] = config;
    return v;
}

std::optional<Preset> Preset::fromJson(json::Value const& v, std::string* error) {
    if (v.str("format") != "gd-decoration-studio-preset") {
        if (error) *error = "not a GD Decoration Studio preset file";
        return std::nullopt;
    }
    if (v.integer("version") > 1) {
        if (error) *error = "preset made by a newer version of the mod";
        return std::nullopt;
    }
    Preset p;
    p.name = v.str("name", "Imported preset");
    p.description = v.str("description");
    p.config = v.get("config").clone();
    if (!p.config.contains("layers") || !p.config.get("layers").isArray()) {
        if (error) *error = "preset has no layers";
        return std::nullopt;
    }
    return p;
}

std::vector<Preset> const& PresetManager::builtins() {
    static std::vector<Preset> const presets = makeBuiltins();
    return presets;
}

Preset const* PresetManager::builtin(std::string const& name) {
    for (auto& p : builtins())
        if (p.name == name) return &p;
    return nullptr;
}

Preset PresetManager::capture(Project const& project, std::string name, std::string description) {
    Preset p;
    p.name = std::move(name);
    p.description = std::move(description);
    json::Value cfg = json::Value::object();
    cfg["palette"] = project.palette.toJson();
    // Layers without project-specific identity (tag groups are per level).
    json::Value layers = json::Value::array();
    for (auto& l : project.layers.all()) {
        auto lj = l.toJson();
        lj["tagGroup"] = 0;
        layers.push(lj);
    }
    cfg["layers"] = layers;
    cfg["settings"] = project.settings.toJson();
    cfg["settings"]["presetName"] = p.name;
    p.config = cfg;
    return p;
}

void PresetManager::apply(Project& project, Preset const& preset) {
    LayerStack incoming;
    incoming.loadJson(preset.config.get("layers"));
    auto counts = project.objectCountByLayer();

    // Match incoming layers to existing ones by kind (and group name for folders).
    std::set<std::string> used;
    std::map<std::string, std::string> idMap;  // incoming id -> final id
    for (auto& inc : incoming.all()) {
        std::string match;
        for (auto& ex : project.layers.all()) {
            if (used.count(ex.id) || ex.kind != inc.kind) continue;
            if (inc.kind == LayerKind::Group && ex.name != inc.name) continue;
            match = ex.id;
            break;
        }
        if (!match.empty()) used.insert(match);
        idMap[inc.id] = match.empty() ? LayerStack::newId(inc.name) : match;
    }
    LayerStack result;
    for (auto& inc : incoming.all()) {
        Layer l = inc;
        std::string newId = idMap[inc.id];
        if (auto ex = project.layers.find(newId)) {
            l.tagGroup = ex->tagGroup;  // keep the association with existing objects
            l.locked = ex->locked;
            l.visible = ex->visible;
        }
        else {
            l.tagGroup = 0;
        }
        l.id = newId;
        l.parentId = inc.parentId.empty() ? "" : idMap[inc.parentId];
        result.add(l);
    }
    // Keep unmatched layers that still own generated objects.
    for (auto& ex : project.layers.all()) {
        if (used.count(ex.id)) continue;
        if (counts[ex.id] == 0) {
            project.channels.releaseLayer(ex.id);
            continue;
        }
        Layer keep = ex;
        if (!keep.parentId.empty() && !result.find(keep.parentId)) keep.parentId.clear();
        result.add(keep);
    }
    project.layers = std::move(result);
    if (preset.config.contains("palette")) project.palette = Palette::fromJson(preset.config.get("palette"));
    if (preset.config.contains("settings")) {
        auto s = GenerationSettings::fromJson(preset.config.get("settings"));
        // Safety / behaviour settings stay the user's choice.
        s.modifiedPolicy = project.settings.modifiedPolicy;
        s.useTagGroups = project.settings.useTagGroups;
        s.maxObjectsPerOperation = project.settings.maxObjectsPerOperation;
        project.settings = s;
    }
}

Project Project::withDefaultLayers() {
    Project p;
    PresetManager::apply(p, PresetManager::builtins().front());
    return p;
}

}  // namespace gdds
