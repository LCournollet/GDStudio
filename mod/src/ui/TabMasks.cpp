// Masks tab: create / edit masks in the editor and run local operations.

#include "Prompt.hpp"
#include "StudioPopup.hpp"
#include "Toolbar.hpp"

using namespace geode::prelude;

namespace {

bool g_newExclusion = false;
bool g_onlySelectedLayer = true;

void startTool(StudioPopup* popup, MaskToolMode mode, std::string maskId = "") {
    auto s = popup->session();
    s->maskTool().begin(mode, g_newExclusion ? gdds::MaskMode::Exclude : gdds::MaskMode::Include, std::move(maskId));
    popup->hideForEditing();
    Toolbar::refreshCurrent();
}

void localOp(StudioPopup* popup, gdds::OperationMode mode, bool preview) {
    auto s = popup->session();
    if (s->selectedMask.empty()) {
        popup->setStatus("Select a mask first");
        return;
    }
    s->commitConfigEdit("mask settings");
    gdds::GenerationRequest req;
    req.mode = mode;
    req.masks = {s->selectedMask};
    if (g_onlySelectedLayer && !s->selectedLayer.empty()) req.layers = {s->selectedLayer};
    if (preview) {
        if (s->preview(req)) popup->hideForEditing();
        Toolbar::refreshCurrent();
        return;
    }
    s->run(req);
    popup->setStatus(s->lastStatus());
    Toolbar::refreshCurrent();
}

}  // namespace

void tabs::buildMasks(StudioPopup* popup, ui::Column& col) {
    auto s = popup->session();
    auto& masks = s->project().masks;
    auto& tool = s->maskTool();
    tool.redraw();

    col.title("New mask");
    col.toggleRow("New masks are exclusion masks (no decoration inside)", g_newExclusion, [](bool on) { g_newExclusion = on; });
    col.buttons({{"Rectangle", [popup] { startTool(popup, MaskToolMode::Rectangle); }},
                 {"Polygon", [popup] { startTool(popup, MaskToolMode::Polygon); }},
                 {"Brush", [popup] { startTool(popup, MaskToolMode::Brush); }},
                 {"Section (X)", [popup] { startTool(popup, MaskToolMode::Section); }}},
                0.4f);
    col.buttons({{"Structures from editor selection", [popup, s] {
                     auto id = s->maskTool().structureMaskFromSelection(g_newExclusion ? gdds::MaskMode::Exclude
                                                                                      : gdds::MaskMode::Include);
                     popup->setStatus(id.empty() ? "Select gameplay blocks in the editor first" : "Structure mask created");
                     popup->refresh();
                 }}},
                0.38f);
    col.slider("Brush radius (units)", float(tool.brushRadius), 10, 150, [s](float v) { s->maskTool().brushRadius = v; }, 0);
    col.toggleRow("Show masks in the editor", tool.showMasks, [s](bool on) {
        s->maskTool().showMasks = on;
        s->maskTool().redraw();
    });

    col.title(fmt::format("Masks ({})", masks.size()));
    if (masks.empty()) col.text("No mask: generation applies to the whole level.", 0.3f);
    else col.text("Checkbox = the mask also constrains GLOBAL generation (exclusions are on by default; include masks are used by local operations until checked).", 0.26f);
    for (auto& m : masks) {
        auto id = m.id;
        bool sel = id == s->selectedMask;
        auto menu = col.row(24, sel ? ui::kRowSelected : ui::kRow);
        auto en = ui::toggle(m.enabled, [s, id](bool on) {
            s->beginConfigEdit();
            for (auto& mm : s->project().masks)
                if (mm.id == id) mm.enabled = on;
            s->maskTool().redraw();
        }, 0.38f);
        en->setPosition({14, 0});
        menu->addChild(en);
        auto name = ui::label(fmt::format("{} [{}{}]", m.name, gdds::maskShapeName(m.shape),
                                          m.mode == gdds::MaskMode::Exclude ? ", exclude" : ""),
                              0.3f, sel ? ui::kAccent : ui::kText);
        ui::fitLabel(name, col.width() * 0.55f);
        auto nameBtn = CCMenuItemExt::createSpriteExtra(name, [popup, s, id](auto) {
            s->selectedMask = id;
            popup->refresh();
        });
        nameBtn->setAnchorPoint({0, 0.5f});
        nameBtn->setPosition({30, 0});
        menu->addChild(nameBtn);
        std::string scope = m.layers.empty() ? "all layers" : fmt::format("{} layer(s)", m.layers.size());
        auto sc = ui::label(scope, 0.26f, ui::kMuted);
        sc->setAnchorPoint({1, 0.5f});
        sc->setPosition({col.width() - 6, 0});
        menu->addChild(sc);
    }

    auto it = std::find_if(masks.begin(), masks.end(), [&](auto& m) { return m.id == s->selectedMask; });
    if (it == masks.end()) return;
    auto id = it->id;
    col.title("Selected mask: " + it->name);
    if (it->shape == gdds::MaskShape::Rectangle)
        col.text(fmt::format("X {:.0f} .. {:.0f}   Y {:.0f} .. {:.0f}", it->rect.x0, it->rect.x1, it->rect.y0, it->rect.y1), 0.28f);
    if (it->shape == gdds::MaskShape::Section)
        col.text(fmt::format("X {:.0f} .. {:.0f}", it->sectionX0, it->sectionX1), 0.28f);
    col.buttons({{it->mode == gdds::MaskMode::Exclude ? "Make include" : "Make exclude", [popup, s, id] {
                      s->beginConfigEdit();
                      for (auto& m : s->project().masks)
                          if (m.id == id) m.mode = m.mode == gdds::MaskMode::Exclude ? gdds::MaskMode::Include : gdds::MaskMode::Exclude;
                      s->commitConfigEdit("mask mode");
                      popup->refresh();
                  }},
                 {"Move", [popup, id] { startTool(popup, MaskToolMode::Move, id); }},
                 {"Resize", [popup, id] { startTool(popup, MaskToolMode::Resize, id); }},
                 {"Rename", [popup, s, id] {
                      TextPrompt::create("Mask name", "Mask", [popup, s, id](std::string name) {
                          s->beginConfigEdit();
                          for (auto& m : s->project().masks)
                              if (m.id == id) m.name = name;
                          s->commitConfigEdit("rename mask");
                          if (StudioPopup::current()) popup->refresh();
                      })->show();
                  }},
                 {"Delete", [popup, s, id] {
                      s->beginConfigEdit();
                      auto& ms = s->project().masks;
                      ms.erase(std::remove_if(ms.begin(), ms.end(), [&](auto& m) { return m.id == id; }), ms.end());
                      s->selectedMask.clear();
                      s->commitConfigEdit("delete mask");
                      s->maskTool().redraw();
                      popup->refresh();
                  }}},
                0.36f);
    auto layer = s->project().layers.find(s->selectedLayer);
    if (layer) {
        bool assigned = std::find(it->layers.begin(), it->layers.end(), layer->id) != it->layers.end();
        col.toggleRow(fmt::format("Restrict this mask to layer '{}' (else: all layers)", layer->name), assigned,
                      [popup, s, id, lid = layer->id](bool on) {
                          s->beginConfigEdit();
                          for (auto& m : s->project().masks) {
                              if (m.id != id) continue;
                              m.layers.erase(std::remove(m.layers.begin(), m.layers.end(), lid), m.layers.end());
                              if (on) m.layers.push_back(lid);
                          }
                          popup->refresh();
                      });
    }

    col.title("Local operations");
    col.toggleRow(layer ? fmt::format("Only the selected layer ({})", layer->name) : std::string("Only the selected layer"),
                  g_onlySelectedLayer, [](bool on) { g_onlySelectedLayer = on; });
    col.buttons({{"Preview in mask", [popup] { localOp(popup, gdds::OperationMode::Regenerate, true); }},
                 {"Generate in mask", [popup] { localOp(popup, gdds::OperationMode::Generate, false); }}},
                0.4f);
    col.buttons({{"Regenerate in mask", [popup] { localOp(popup, gdds::OperationMode::Regenerate, false); }},
                 {"Remove in mask", [popup] { localOp(popup, gdds::OperationMode::Remove, false); }}},
                0.4f);
    col.text("Local operations only touch objects whose anchor lies in the mask; with the 'extend' border policy a structure "
             "touched by the mask is processed entirely, 'clip' cuts decorations at the border.",
             0.27f);
}
