// Objects tab: visual library of decoration objects and per-object rules of
// the selected layer.

#include "Prompt.hpp"
#include "StudioPopup.hpp"

#include <gdds/DecoCatalog.hpp>
#include <gdds/Layer.hpp>

using namespace geode::prelude;

namespace {

std::string g_query;
std::string g_category = "In layer";
int g_page = 0;
constexpr int kPerRow = 9;
constexpr int kPerPage = 54;

gdds::ObjectRule* ruleOf(gdds::Layer* layer, int id) {
    if (!layer) return nullptr;
    for (auto& r : layer->objects)
        if (r.id == id) return &r;
    return nullptr;
}

std::vector<gdds::DecoDef const*> listFor(StudioSession* s, gdds::Layer* layer) {
    auto& cat = s->catalog();
    std::vector<gdds::DecoDef const*> out;
    auto favs = s->favorites();
    for (auto d : cat.search(g_query)) {
        if (g_category == "In layer") {
            if (!ruleOf(layer, d->id)) continue;
        }
        else if (g_category == "Favorites") {
            if (std::find(favs.begin(), favs.end(), d->id) == favs.end()) continue;
        }
        else if (g_category == "Curated") {
            if (d->hasTag("library")) continue;
        }
        else if (d->category != g_category) continue;
        out.push_back(d);
    }
    return out;
}

CCNode* objectSprite(gdds::DecoDef const& d, float size) {
    auto spr = CCSprite::createWithSpriteFrameName(d.frame.c_str());
    if (!spr) {
        auto l = ui::label(std::to_string(d.id), 0.3f);
        return l;
    }
    float m = std::max(spr->getContentSize().width, spr->getContentSize().height);
    spr->setScale(size / std::max(1.f, m));
    spr->setRotation(float(d.baseRotation));
    return spr;
}

}  // namespace

void tabs::buildObjects(StudioPopup* popup, ui::Column& col) {
    auto s = popup->session();
    auto layer = s->project().layers.find(s->selectedLayer);
    auto& cat = s->catalog();

    col.title(layer ? "Objects of layer: " + layer->name : "Object library");
    if (layer && layer->objects.empty())
        col.text("This layer has no object selected: the engine uses the studio's default objects for each shape.", 0.28f,
                 ui::kAccent);
    col.input("Search", g_query, [](std::string v) { g_query = v; g_page = 0; });
    col.buttons({{"Apply search", [popup] { popup->refresh(); }}}, 0.38f);

    // Categories
    std::vector<std::string> categories = {"In layer", "Favorites", "Curated"};
    for (auto& c : cat.categories()) categories.push_back(c);
    std::vector<std::pair<std::string, std::function<void()>>> row;
    for (auto& c : categories) {
        std::string label = (c == g_category ? "> " : "") + c;
        row.push_back({label, [popup, c] { g_category = c; g_page = 0; popup->refresh(); }});
        if (row.size() == 4) {
            col.buttons(row, 0.3f);
            row.clear();
        }
    }
    if (!row.empty()) col.buttons(row, 0.3f);

    // Grid
    auto items = listFor(s, layer);
    int pages = std::max(1, int((items.size() + kPerPage - 1) / kPerPage));
    g_page = std::clamp(g_page, 0, pages - 1);
    col.text(fmt::format("{} objects - page {}/{}", items.size(), g_page + 1, pages), 0.28f);
    float cell = col.width() / kPerRow;
    for (int i = g_page * kPerPage; i < std::min<int>(int(items.size()), (g_page + 1) * kPerPage); i += kPerRow) {
        auto menu = col.row(cell);
        for (int j = 0; j < kPerRow && i + j < int(items.size()); ++j) {
            auto d = items[size_t(i + j)];
            auto node = CCNode::create();
            node->setContentSize({cell - 2, cell - 2});
            bool inLayer = ruleOf(layer, d->id) != nullptr;
            bool selected = s->selectedCatalogId == d->id;
            auto bg = CCLayerColor::create(selected ? ui::kRowSelected : (inLayer ? ccColor4B{30, 90, 60, 220} : ui::kRow),
                                           cell - 2, cell - 2);
            node->addChild(bg);
            auto spr = objectSprite(*d, cell - 8);
            spr->setPosition({(cell - 2) / 2, (cell - 2) / 2});
            node->addChild(spr);
            int id = d->id;
            auto btn = CCMenuItemExt::createSpriteExtra(node, [popup, s, id](auto) {
                s->selectedCatalogId = id;
                popup->refresh();
            });
            btn->setPosition({cell * (j + 0.5f), 0});
            menu->addChild(btn);
        }
    }
    col.buttons({{"< Prev", [popup] { g_page--; popup->refresh(); }}, {"Next >", [popup] { g_page++; popup->refresh(); }}}, 0.38f);

    // Selected object
    auto def = cat.find(s->selectedCatalogId);
    if (!def) {
        col.text("Tap an object to see it and edit its rules.");
        return;
    }
    col.title(fmt::format("#{} {}", def->id, def->name));
    col.text(fmt::format("{} - shape {} - {:.1f} x {:.1f} units{}{}{}", def->category, gdds::decoShapeName(def->shape),
                         def->visWidth(), def->visHeight(), def->stretchable ? " - stretchable" : "",
                         def->blending ? " - glow" : "", cat.isDisabled(def->id) ? " - DISABLED (failed verification)" : ""),
             0.28f);
    int id = def->id;
    auto favs = s->favorites();
    bool fav = std::find(favs.begin(), favs.end(), id) != favs.end();
    col.toggleRow("Favorite", fav, [s, id](bool) { s->toggleFavorite(id); });
    if (!layer) return;
    auto layerId = layer->id;
    auto rule = ruleOf(layer, id);
    col.toggleRow("Use this object in the layer", rule != nullptr, [popup, s, layerId, id](bool on) {
        s->beginConfigEdit();
        auto l = s->project().layers.find(layerId);
        if (!l) return;
        if (on && !ruleOf(l, id)) {
            gdds::ObjectRule r;
            r.id = id;
            l->objects.push_back(r);
        }
        if (!on) l->objects.erase(std::remove_if(l->objects.begin(), l->objects.end(), [id](auto& r) { return r.id == id; }),
                                  l->objects.end());
        popup->refresh();
    });
    if (!rule) return;
    auto edit = [s, layerId, id](std::function<void(gdds::ObjectRule&)> fn) {
        s->beginConfigEdit();
        if (auto r = ruleOf(s->project().layers.find(layerId), id)) fn(*r);
    };
    col.slider("Weight", float(rule->weight), 0, 5, [edit](float v) { edit([v](auto& r) { r.weight = v; }); });
    col.slider("Probability", float(rule->probability), 0, 1, [edit](float v) { edit([v](auto& r) { r.probability = v; }); });
    col.slider("Min scale", float(rule->minScale), 0.2f, 4, [edit](float v) { edit([v](auto& r) { r.minScale = v; }); });
    col.slider("Max scale", float(rule->maxScale), 0.2f, 4, [edit](float v) { edit([v](auto& r) { r.maxScale = v; }); });
    col.slider("Opacity", float(rule->opacity), 0.1f, 1, [edit](float v) { edit([v](auto& r) { r.opacity = v; }); });
    col.slider("Min spacing (blocks)", float(rule->spacing / 30.0), 0, 10,
               [edit](float v) { edit([v](auto& r) { r.spacing = v * 30.0; }); }, 1);
    // Rotations
    {
        auto menu = col.row(24);
        auto l = ui::label("Rotations", 0.3f);
        l->setAnchorPoint({0, 0.5f});
        l->setPosition({4, 0});
        menu->addChild(l);
        int k = 0;
        for (int rot : {0, 45, 90, 180, 270}) {
            bool on = std::find(rule->rotations.begin(), rule->rotations.end(), rot) != rule->rotations.end();
            auto t = ui::toggle(on, [edit, rot](bool v) {
                edit([rot, v](auto& r) {
                    auto& rs = r.rotations;
                    rs.erase(std::remove(rs.begin(), rs.end(), rot), rs.end());
                    if (v) rs.push_back(rot);
                    if (rs.empty()) rs.push_back(0);
                });
            }, 0.35f);
            t->setPosition({90.f + k * 52.f, 0});
            menu->addChild(t);
            auto tl = ui::label(std::to_string(rot), 0.26f, ui::kMuted);
            tl->setAnchorPoint({0, 0.5f});
            tl->setPosition({100.f + k * 52.f, 0});
            menu->addChild(tl);
            ++k;
        }
    }
    // Allowed colors
    {
        static char const* roles[] = {"auto", "primary", "secondary", "accent", "glow", "detail", "background", "foreground"};
        std::string current = rule->colors.empty() ? "auto" : gdds::colorRoleName(rule->colors.front());
        auto menu = col.row(26);
        auto l = ui::label("Color role", 0.3f);
        l->setAnchorPoint({0, 0.5f});
        l->setPosition({4, 0});
        menu->addChild(l);
        auto btn = ui::button(current, [popup, edit, current] {
            int idx = 0;
            for (int i = 0; i < 8; ++i)
                if (current == roles[i]) idx = i;
            int next = (idx + 1) % 8;
            edit([next](auto& r) {
                r.colors.clear();
                if (next > 0) r.colors.push_back(gdds::ColorRole(next - 1));
            });
            popup->refresh();
        }, 0.42f);
        btn->setPosition({col.width() * 0.6f, 0});
        menu->addChild(btn);
    }
    // Placement anchors
    {
        static std::pair<char const*, uint32_t> anchors[] = {
            {"top", gdds::anchor::TopFace}, {"bottom", gdds::anchor::BottomFace}, {"side", gdds::anchor::SideFace},
            {"convex", gdds::anchor::ConvexCorner}, {"concave", gdds::anchor::ConcaveCorner},
            {"interior", gdds::anchor::Interior}, {"free", gdds::anchor::FreeSpace}, {"hazard", gdds::anchor::Hazard}};
        col.text("Allowed placements:", 0.28f);
        for (int half = 0; half < 2; ++half) {
            auto menu = col.row(22);
            for (int k = 0; k < 4; ++k) {
                auto [name, bit] = anchors[half * 4 + k];
                bool on = (rule->anchors & bit) != 0;
                auto t = ui::toggle(on, [edit, bit](bool v) {
                    edit([bit, v](auto& r) {
                        if (r.anchors == gdds::anchor::Any) r.anchors = 0xff;
                        r.anchors = v ? (r.anchors | bit) : (r.anchors & ~bit);
                        if ((r.anchors & 0xff) == 0xff) r.anchors = gdds::anchor::Any;
                    });
                }, 0.35f);
                t->setPosition({14.f + k * 90.f, 0});
                menu->addChild(t);
                auto tl = ui::label(name, 0.27f, ui::kMuted);
                tl->setAnchorPoint({0, 0.5f});
                tl->setPosition({24.f + k * 90.f, 0});
                menu->addChild(tl);
            }
        }
    }

    // Collections
    col.title("Collections");
    col.buttons({{"Save layer objects as collection", [s, layerId] {
                     TextPrompt::create("Collection name", "My collection", [s, layerId](std::string name) {
                         std::vector<int> ids;
                         if (auto l = s->project().layers.find(layerId))
                             for (auto& r : l->objects) ids.push_back(r.id);
                         s->saveCollection(name, ids);
                         s->status("Collection saved: " + name);
                     })->show();
                 }}},
                0.38f);
    for (auto& [name, ids] : s->collections()) {
        auto n = name;
        auto list = ids;
        col.buttons({{fmt::format("Load '{}' ({} objects)", n, list.size()), [popup, s, layerId, list] {
                         s->beginConfigEdit();
                         if (auto l = s->project().layers.find(layerId)) {
                             for (int oid : list)
                                 if (!ruleOf(l, oid)) {
                                     gdds::ObjectRule r;
                                     r.id = oid;
                                     l->objects.push_back(r);
                                 }
                         }
                         popup->refresh();
                     }}},
                    0.34f);
    }
}
