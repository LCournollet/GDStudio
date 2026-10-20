// Presets tab: built-in styles, user presets, save / export / import.

#include "Prompt.hpp"
#include "StudioPopup.hpp"

#include "../Files.hpp"
#include <gdds/Presets.hpp>

using namespace geode::prelude;

namespace {

std::string fileNameFor(std::string const& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        else if (c == ' ') out += '_';
    }
    return (out.empty() ? std::string("preset") : out) + ".json";
}

void applyPreset(StudioPopup* popup, gdds::Preset const& preset) {
    auto s = popup->session();
    auto name = preset.name;
    auto copy = preset;
    geode::createQuickPopup("Apply preset",
                            fmt::format("Apply <cy>{}</c>?\nPalette, layers and their settings are replaced. Masks and the "
                                        "objects already generated are kept (regenerate to see the new style).",
                                        name),
                            "Cancel", "Apply", [popup, s, copy](auto, bool yes) {
                                if (!yes) return;
                                s->beginConfigEdit();
                                gdds::PresetManager::apply(s->project(), copy);
                                s->commitConfigEdit("apply preset " + copy.name);
                                s->studio().applyColors();
                                s->status("Preset applied: " + copy.name);
                                if (StudioPopup::current()) popup->refresh();
                            });
}

}  // namespace

void tabs::buildPresets(StudioPopup* popup, ui::Column& col) {
    auto s = popup->session();
    col.title("Built-in styles");
    col.text("Current: " + (s->project().settings.presetName.empty() ? std::string("custom") : s->project().settings.presetName),
             0.3f, ui::kAccent);
    for (auto& p : gdds::PresetManager::builtins()) {
        auto menu = col.row(34, ui::kRow);
        auto name = ui::label(p.name, 0.38f, ui::kText, "goldFont.fnt");
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({6, 6});
        menu->addChild(name);
        auto desc = ui::label(p.description, 0.22f, ui::kMuted, "chatFont.fnt");
        desc->setAnchorPoint({0, 0.5f});
        desc->setPosition({6, -8});
        ui::fitLabel(desc, col.width() - 70);
        menu->addChild(desc);
        auto preset = p;
        auto btn = ui::button("Apply", [popup, preset] { applyPreset(popup, preset); }, 0.4f, "GJ_button_01.png");
        btn->setPosition({col.width() - 30, 0});
        menu->addChild(btn);
    }

    col.title("My presets");
    col.buttons({{"Save current as preset", [s] {
                     TextPrompt::create("Preset name", "My style", [s](std::string name) {
                         auto preset = gdds::PresetManager::capture(s->project(), name, "Saved from " + s->project().levelName);
                         auto path = s->presetsDir() / fileNameFor(name);
                         bool ok = files::writeTextSafe(path, preset.toJson().dump());
                         s->status(ok ? "Preset saved: " + files::fileName(path) : "Could not save the preset");
                         if (auto p = StudioPopup::current()) p->refresh();
                     })->show();
                 }},
                 {"Open presets folder", [s] { files::openFolder(s->presetsDir()); }}},
                0.38f);
    col.text("Export = the preset files in that folder. Import = copy a preset .json file into it, then reopen this tab.", 0.26f);
    auto user = s->userPresets();
    if (user.empty()) col.text("No user preset yet.", 0.28f);
    for (auto& [preset, path] : user) {
        auto menu = col.row(26, ui::kRow);
        auto name = ui::label(preset.name, 0.33f);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({6, 0});
        ui::fitLabel(name, col.width() - 130);
        menu->addChild(name);
        auto p = preset;
        auto apply = ui::button("Apply", [popup, p] { applyPreset(popup, p); }, 0.36f, "GJ_button_01.png");
        apply->setPosition({col.width() - 80, 0});
        menu->addChild(apply);
        auto file = path;
        auto del = ui::button("Delete", [popup, file] {
            std::error_code ec;
            std::filesystem::remove(file, ec);
            popup->refresh();
        }, 0.36f, "GJ_button_06.png");
        del->setPosition({col.width() - 28, 0});
        menu->addChild(del);
    }
}
