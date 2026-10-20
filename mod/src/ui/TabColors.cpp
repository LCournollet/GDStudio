// Colors tab: global palette (or a layer override), adjustments and channels.

#include "StudioPopup.hpp"

#include <Geode/ui/ColorPickPopup.hpp>

using namespace geode::prelude;

namespace {

char const* const kRoleLabels[] = {"Primary", "Secondary", "Accent", "Glow", "Details", "Background", "Foreground"};

// Role color currently shown (layer override when editing a layer).
gdds::RoleColor shownRole(StudioSession* s, gdds::Layer const* layer, gdds::ColorRole role) {
    if (layer && s->editLayerPalette && layer->palette.roles[size_t(role)]) return *layer->palette.roles[size_t(role)];
    return s->project().palette.role(role);
}

void setRole(StudioSession* s, std::string const& layerId, gdds::ColorRole role, std::function<void(gdds::RoleColor&)> fn) {
    s->beginConfigEdit();
    auto layer = s->project().layers.find(layerId);
    if (layer && s->editLayerPalette) {
        auto& slot = layer->palette.roles[size_t(role)];
        if (!slot) slot = s->project().palette.role(role);
        fn(*slot);
    }
    else {
        fn(s->project().palette.role(role));
    }
    s->studio().applyColors();
}

void applyLevelBackground(StudioSession* s) {
    auto& pal = s->project().palette;
    auto bg = gdds::resolveRole(pal, {}, gdds::ColorRole::Background).color.mix({0, 0, 0}, 0.35);
    auto ground = gdds::resolveRole(pal, {}, gdds::ColorRole::Foreground).color;
    auto line = gdds::resolveRole(pal, {}, gdds::ColorRole::Secondary).color;
    auto channels = s->backend().readChannels();
    auto set = [&](int id, gdds::Color3 c) {
        for (auto& ch : channels)
            if (ch.id == id) {
                ch.r = c.r; ch.g = c.g; ch.b = c.b; ch.copyId = 0; ch.playerColor = -1;
                return;
            }
        gdds::ColorChannel ch;
        ch.id = id; ch.r = c.r; ch.g = c.g; ch.b = c.b;
        channels.push_back(ch);
    };
    set(1000, bg);
    set(1001, ground);
    set(1009, ground);
    set(1002, line);
    s->backend().writeChannels(channels);
}

}  // namespace

void tabs::buildColors(StudioPopup* popup, ui::Column& col) {
    auto s = popup->session();
    auto layer = s->project().layers.find(s->selectedLayer);
    std::string layerId = layer ? layer->id : "";
    if (!layer) s->editLayerPalette = false;

    col.title(s->editLayerPalette ? "Colors of layer: " + layer->name : "Global palette: " + s->project().palette.name);
    if (layer)
        col.toggleRow("Edit the selected layer only (override)", s->editLayerPalette, [popup, s](bool on) {
            s->editLayerPalette = on;
            popup->refresh();
        });
    for (int i = 0; i < gdds::kColorRoleCount; ++i) {
        auto role = gdds::ColorRole(i);
        auto rc = shownRole(s, layer, role);
        bool overridden = layer && s->editLayerPalette && layer->palette.roles[size_t(i)].has_value();
        auto menu = col.row(26, ui::kRow);
        auto name = ui::label(std::string(kRoleLabels[i]) + (overridden ? " *" : ""), 0.33f, overridden ? ui::kAccent : ui::kText);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({4, 0});
        menu->addChild(name);
        auto sw = ui::swatch({GLubyte(rc.color.r), GLubyte(rc.color.g), GLubyte(rc.color.b)}, [popup, s, layerId, role, rc] {
            auto picker = ColorPickPopup::create(ccColor4B{GLubyte(rc.color.r), GLubyte(rc.color.g), GLubyte(rc.color.b),
                                                           GLubyte(std::round(rc.opacity * 255))});
            picker->setCallback([s, layerId, role](ccColor4B const& c) {
                setRole(s, layerId, role, [c](gdds::RoleColor& r) {
                    r.color = {c.r, c.g, c.b};
                    r.opacity = c.a / 255.0;
                });
                if (auto p = StudioPopup::current()) p->refresh();
            });
            picker->show();
        });
        sw->setPosition({100, 0});
        menu->addChild(sw);
        auto hex = TextInput::create(70.f, "#RRGGBB");
        hex->setString(rc.color.toHex());
        hex->setFilter("#0123456789abcdefABCDEF");
        hex->setScale(0.6f);
        hex->setCallback([s, layerId, role](std::string const& text) {
            if (auto c = gdds::Color3::fromHex(text))
                setRole(s, layerId, role, [c](gdds::RoleColor& r) { r.color = *c; });
        });
        hex->setPosition({150, 0});
        menu->addChild(hex);
        auto op = ui::label("a " + ui::fmtNumber(rc.opacity, 2), 0.28f, ui::kMuted);
        op->setPosition({200, 0});
        menu->addChild(op);
        auto blend = ui::toggle(rc.blending, [s, layerId, role](bool on) {
            setRole(s, layerId, role, [on](gdds::RoleColor& r) { r.blending = on; });
        }, 0.38f);
        blend->setPosition({230, 0});
        menu->addChild(blend);
        auto bl = ui::label("additive", 0.25f, ui::kMuted);
        bl->setAnchorPoint({0, 0.5f});
        bl->setPosition({240, 0});
        menu->addChild(bl);
        if (overridden) {
            auto reset = ui::button("Reset", [popup, s, layerId, i] {
                s->beginConfigEdit();
                if (auto l = s->project().layers.find(layerId)) l->palette.roles[size_t(i)].reset();
                s->studio().applyColors();
                popup->refresh();
            }, 0.32f);
            reset->setPosition({col.width() - 22, 0});
            menu->addChild(reset);
        }
    }

    // Adjustments (global, or layer override)
    gdds::ColorAdjust adj = (layer && s->editLayerPalette && layer->palette.adjust) ? *layer->palette.adjust : s->project().palette.adjust;
    auto setAdj = [s, layerId](std::function<void(gdds::ColorAdjust&)> fn) {
        s->beginConfigEdit();
        auto l = s->project().layers.find(layerId);
        if (l && s->editLayerPalette) {
            if (!l->palette.adjust) {
                gdds::ColorAdjust neutral;
                l->palette.adjust = neutral;
            }
            fn(*l->palette.adjust);
        }
        else fn(s->project().palette.adjust);
        s->studio().applyColors();
    };
    col.title("Adjustments");
    col.slider("Hue shift", float(adj.hue), -180, 180, [setAdj](float v) { setAdj([v](auto& a) { a.hue = v; }); }, 0);
    col.slider("Saturation", float(adj.saturation), 0, 2, [setAdj](float v) { setAdj([v](auto& a) { a.saturation = v; }); });
    col.slider("Brightness", float(adj.brightness), 0, 2, [setAdj](float v) { setAdj([v](auto& a) { a.brightness = v; }); });
    col.slider("Contrast", float(adj.contrast), 0.3f, 2, [setAdj](float v) { setAdj([v](auto& a) { a.contrast = v; }); });
    col.slider("Opacity", float(adj.opacity), 0, 1, [setAdj](float v) { setAdj([v](auto& a) { a.opacity = v; }); });
    col.slider("Glow intensity", float(adj.glowIntensity), 0, 2, [setAdj](float v) { setAdj([v](auto& a) { a.glowIntensity = v; }); });
    col.slider("Per-object variation", float(adj.variation), 0, 1, [setAdj](float v) { setAdj([v](auto& a) { a.variation = v; }); });
    col.text("Hue/brightness variation is baked into objects at generation time (HSV); the other adjustments apply live.", 0.27f);

    if (!s->editLayerPalette) {
        col.title("Distribution");
        col.slider("Accent share", float(s->project().palette.accentShare), 0, 1, [s](float v) {
            s->beginConfigEdit();
            s->project().palette.accentShare = v;
        });
        col.slider("Secondary share", float(s->project().palette.secondaryShare), 0, 1, [s](float v) {
            s->beginConfigEdit();
            s->project().palette.secondaryShare = v;
        });
    }

    col.title("Level colors");
    auto allocs = s->project().channels.allocations();
    col.text(fmt::format("The studio uses {} color channels of its own (allocated from 999 downwards, never one used by the level).",
                         allocs.size()),
             0.28f);
    col.buttons({{"Apply palette to BG / ground", [popup, s] {
                      applyLevelBackground(s);
                      popup->setStatus("Level background, ground and line colors set from the palette");
                  }},
                 {"Refresh channels", [popup, s] {
                      s->studio().applyColors();
                      popup->setStatus("Studio channels updated");
                  }}},
                0.38f);
}
