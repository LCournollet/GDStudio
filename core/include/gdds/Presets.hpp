#pragma once

// Presets: complete decoration configurations (palette, layers, engine
// styles, object rules, seeds, composition knobs). Masks are level-specific
// and are NOT part of presets (they are saved with the project).

#include "Json.hpp"
#include "Project.hpp"

#include <string>
#include <vector>

namespace gdds {

struct Preset {
    std::string name;
    std::string description;
    json::Value config;  // { palette, layers, settings } (no masks, no ids)

    json::Value toJson() const;  // exportable file
    static std::optional<Preset> fromJson(json::Value const& v, std::string* error = nullptr);
};

class PresetManager {
public:
    // The built-in styles: Modern Glow, Minimal, Dark Tech, Neon, Industrial, Cyberpunk, Abstract.
    static std::vector<Preset> const& builtins();
    static Preset const* builtin(std::string const& name);

    // Captures the current configuration of a project.
    static Preset capture(Project const& project, std::string name, std::string description);

    // Applies a preset. Existing layers are matched by kind so their ids,
    // tag groups and generated objects stay attached; unmatched layers that
    // still own objects are kept, empty ones are removed. Masks are kept.
    static void apply(Project& project, Preset const& preset);
};

}  // namespace gdds
