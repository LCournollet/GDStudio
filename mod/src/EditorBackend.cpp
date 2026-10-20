#include "EditorBackend.hpp"

#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

EditorBackend::EditorBackend(LevelEditorLayer* lel, EditorUI* ui) : m_lel(lel), m_ui(ui) {}

EditorBackend::~EditorBackend() {
    // The editor may already be gone; never touch it from the destructor.
    m_previewObjects.clear();
    m_previewHidden.clear();
    m_hiddenObjects.clear();
}

std::string EditorBackend::saveStringOf(GameObject* obj, LevelEditorLayer* lel) {
    return std::string(obj->getSaveString(lel));
}

void EditorBackend::detectOffset(GameObject* obj, gdds::LevelObject const& parsed) {
    // The object layer is offset from level coordinates (ground line); measure
    // it instead of hard-coding it.
    auto p = obj->getPosition();
    m_offsetX = p.x - parsed.pos.x;
    m_offsetY = p.y - parsed.pos.y;
    if (std::abs(m_offsetX) > 0.5) m_offsetX = 0;  // rotation pivots etc.: only trust the vertical offset
    m_offsetKnown = true;
}

void EditorBackend::refreshCache() {
    m_cache.clear();
    m_cacheParsed.clear();
    if (!m_lel || !m_lel->m_objects) return;
    std::unordered_set<GameObject*> preview;
    for (auto& r : m_previewObjects) preview.insert(r.data());
    m_cache.reserve(m_lel->m_objects->count());
    m_cacheParsed.reserve(m_lel->m_objects->count());
    for (auto obj : CCArrayExt<GameObject*>(m_lel->m_objects)) {
        if (!obj || preview.count(obj)) continue;
        auto parsed = gdds::LevelObject::parse(saveStringOf(obj, m_lel));
        if (!parsed) continue;
        if (!m_offsetKnown) detectOffset(obj, *parsed);
        m_cache.push_back({obj, parsed->fingerprint()});
        m_cacheParsed.push_back(std::move(*parsed));
    }
}

std::vector<gdds::LevelObject> EditorBackend::readObjects() {
    refreshCache();
    return m_cacheParsed;
}

gdds::LevelHeader EditorBackend::header() {
    if (!m_lel || !m_lel->m_levelSettings) return gdds::defaultHeader();
    return gdds::LevelHeader::parse(std::string(m_lel->m_levelSettings->getSaveString()));
}

std::string EditorBackend::levelName() {
    if (!m_lel || !m_lel->m_level) return "Unnamed";
    return std::string(m_lel->m_level->m_levelName);
}

std::vector<std::string> EditorBackend::createObjects(std::vector<gdds::LevelObject> const& objects) {
    std::vector<std::string> out(objects.size());
    if (!m_lel || objects.empty()) return out;
    std::string str;
    str.reserve(objects.size() * 64);
    for (auto& o : objects) {
        str += o.serialize();
        str += ';';
    }
    // noUndo: the studio keeps its own history (see docs/LIMITATIONS.md).
    auto arr = m_lel->createObjectsFromString(str, true, true);
    if (!arr) return out;
    std::vector<GameObject*> created;
    for (auto obj : CCArrayExt<GameObject*>(arr)) created.push_back(obj);
    std::vector<std::string> createdStr(created.size());
    std::vector<std::optional<gdds::LevelObject>> createdParsed(created.size());
    for (size_t j = 0; j < created.size(); ++j) {
        createdStr[j] = saveStringOf(created[j], m_lel);
        createdParsed[j] = gdds::LevelObject::parse(createdStr[j]);
    }
    std::vector<char> used(created.size(), 0);

    // Authoritative safety check, on the objects as the game really built
    // them (types can be assigned late, e.g. by setupCustomSprites): anything
    // that is not a decoration is removed at once and reported, so it never
    // stays in the level. If most objects fail together, the field read itself
    // is unreliable and the check is skipped.
    std::vector<size_t> notDeco;
    for (size_t j = 0; j < created.size(); ++j)
        if (static_cast<int>(created[j]->m_objectType) != static_cast<int>(GameObjectType::Decoration)) notDeco.push_back(j);
    if (!notDeco.empty() && notDeco.size() * 3 <= created.size()) {
        std::set<int> ids;
        for (size_t j : notDeco) {
            if (createdParsed[j]) ids.insert(createdParsed[j]->id);
            m_lel->removeObject(created[j], true);
            used[j] = 1;  // never reported as created
        }
        for (int id : ids) {
            log::warn("Object {} is not a decoration once placed in this game version: removed and disabled", id);
            if (onNotDecoration) onNotDecoration(id);
        }
    }
    else if (!notDeco.empty()) {
        log::warn("Type check of created objects looks unreliable ({} of {} not decorations): ignored", notDeco.size(),
                  created.size());
    }
    auto matches = [&](size_t j, gdds::LevelObject const& want) {
        auto& p = createdParsed[j];
        return p && p->id == want.id && std::abs(p->pos.x - want.pos.x) < 0.01 && std::abs(p->pos.y - want.pos.y) < 0.01;
    };
    for (size_t i = 0; i < objects.size(); ++i) {
        if (i < created.size() && !used[i] && matches(i, objects[i])) {
            used[i] = 1;
            out[i] = createdStr[i];
            continue;
        }
        for (size_t j = 0; j < created.size(); ++j) {
            if (used[j] || !matches(j, objects[i])) continue;
            used[j] = 1;
            out[i] = createdStr[j];
            break;
        }
    }
    return out;
}

size_t EditorBackend::removeObjects(std::vector<gdds::LevelObject> const& objects) {
    if (!m_lel || objects.empty()) return 0;
    refreshCache();
    std::unordered_multimap<uint64_t, size_t> byFp;
    for (size_t i = 0; i < m_cache.size(); ++i) byFp.emplace(m_cache[i].fingerprint, i);
    if (m_ui) m_ui->deselectAll();
    std::vector<char> gone(m_cache.size(), 0);
    size_t removed = 0;
    for (auto& o : objects) {
        auto range = byFp.equal_range(o.fingerprint());
        for (auto it = range.first; it != range.second; ++it) {
            if (gone[it->second]) continue;
            gone[it->second] = 1;
            m_lel->removeObject(static_cast<GameObject*>(m_cache[it->second].object), true);
            ++removed;
            break;
        }
    }
    m_cache.clear();
    m_cacheParsed.clear();
    return removed;
}

std::vector<gdds::ColorChannel> EditorBackend::readChannels() { return header().colors(); }

void EditorBackend::writeChannels(std::vector<gdds::ColorChannel> const& channels) {
    if (!m_lel || !m_lel->m_levelSettings || !m_lel->m_levelSettings->m_effectManager) return;
    // Only touch channels whose definition actually changes.
    std::unordered_map<int, std::string> current;
    for (auto& c : readChannels()) current[c.id] = c.serialize();
    auto em = m_lel->m_levelSettings->m_effectManager;
    bool changed = false;
    for (auto& ch : channels) {
        if (ch.id <= 0 || ch.id > 1101) continue;
        auto s = ch.serialize();
        if (auto it = current.find(ch.id); it != current.end() && it->second == s) continue;
        auto action = em->getColorAction(ch.id);
        if (!action) {
            action = ColorAction::create();
            em->setColorAction(action, ch.id);
        }
        action->setupFromString(s);
        changed = true;
    }
    if (!changed) return;
    // Propagate start colors to the live editor colors and refresh objects.
    if (m_lel->m_effectManager) m_lel->transferDefaultColors(em, m_lel->m_effectManager);
    m_lel->updateObjectColors(m_lel->m_objects);
}

void EditorBackend::showPreview(std::vector<gdds::LevelObject> const& add, std::vector<gdds::LevelObject> const& hide) {
    clearPreview();
    if (!m_lel) return;
    if (!add.empty()) {
        std::string str;
        for (auto& o : add) {
            str += o.serialize();
            str += ';';
        }
        if (auto arr = m_lel->createObjectsFromString(str, true, true)) {
            for (auto obj : CCArrayExt<GameObject*>(arr)) m_previewObjects.push_back(obj);
        }
    }
    if (!hide.empty()) {
        refreshCache();
        std::unordered_multimap<uint64_t, size_t> byFp;
        for (size_t i = 0; i < m_cache.size(); ++i) byFp.emplace(m_cache[i].fingerprint, i);
        for (auto& o : hide) {
            auto range = byFp.equal_range(o.fingerprint());
            for (auto it = range.first; it != range.second; ++it) {
                auto obj = static_cast<GameObject*>(m_cache[it->second].object);
                obj->setVisible(false);
                m_previewHidden.push_back(obj);
                byFp.erase(it);
                break;
            }
        }
    }
    // Outline every proposed object so it is distinguishable from the level.
    auto overlay = CCDrawNode::create();
    ccColor4F fill{0.f, 0.9f, 1.f, 0.05f}, border{0.f, 0.9f, 1.f, 0.55f};
    size_t drawn = 0;
    for (auto& obj : m_previewObjects) {
        if (drawn++ > 6000) break;  // keep the overlay cheap on huge previews
        auto r = obj->boundingBox();
        CCPoint verts[4] = {{r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()},
                            {r.getMinX(), r.getMaxY()}};
        overlay->drawPolygon(verts, 4, fill, 0.35f, border);
    }
    m_lel->m_objectLayer->addChild(overlay, 9998);
    m_previewOverlay = overlay;
}

void EditorBackend::clearPreview() {
    if (m_lel && m_lel->m_objects) {
        size_t left = 0;
        for (auto& obj : m_previewObjects) {
            if (!m_lel->m_objects->containsObject(obj)) continue;
            m_lel->removeObject(obj, true);
            if (m_lel->m_objects->containsObject(obj)) ++left;
        }
        if (left) log::error("{} preview objects could not be removed from the level", left);
    }
    m_previewObjects.clear();
    for (auto& obj : m_previewHidden) obj->setVisible(true);
    m_previewHidden.clear();
    if (m_previewOverlay) {
        m_previewOverlay->removeFromParent();
        m_previewOverlay = nullptr;
    }
    m_cache.clear();
    m_cacheParsed.clear();
}

void EditorBackend::setHiddenTagGroups(std::vector<int> const& groups) {
    for (auto& obj : m_hiddenObjects) obj->setVisible(true);
    m_hiddenObjects.clear();
    m_hiddenTags = std::set<int>(groups.begin(), groups.end());
    if (m_hiddenTags.empty() || !m_lel) return;
    refreshCache();
    for (size_t i = 0; i < m_cache.size(); ++i) {
        for (int g : m_cacheParsed[i].groups) {
            if (!m_hiddenTags.count(g)) continue;
            auto obj = static_cast<GameObject*>(m_cache[i].object);
            obj->setVisible(false);
            m_hiddenObjects.push_back(obj);
            break;
        }
    }
}

void EditorBackend::reapplyHidden() {
    for (auto& obj : m_hiddenObjects)
        if (obj->isVisible()) obj->setVisible(false);
    for (auto& obj : m_previewHidden)
        if (obj->isVisible()) obj->setVisible(false);
}

CCPoint EditorBackend::levelToNode(gdds::Vec2 p) {
    if (!m_offsetKnown) refreshCache();
    return {float(p.x + m_offsetX), float(p.y + m_offsetY)};
}

gdds::Vec2 EditorBackend::nodeToLevel(CCPoint p) {
    if (!m_offsetKnown) refreshCache();
    return {p.x - m_offsetX, p.y - m_offsetY};
}

std::vector<gdds::LevelObject> EditorBackend::selectedObjects() {
    std::vector<gdds::LevelObject> out;
    if (!m_lel || !m_ui) return out;
    auto arr = m_ui->getSelectedObjects();
    if (!arr) return out;
    for (auto obj : CCArrayExt<GameObject*>(arr))
        if (auto p = gdds::LevelObject::parse(saveStringOf(obj, m_lel))) out.push_back(std::move(*p));
    return out;
}

int EditorBackend::selectObjectsWithGroups(std::set<int> const& groups) {
    if (!m_lel || !m_ui || groups.empty()) return 0;
    refreshCache();
    auto arr = CCArray::create();
    for (size_t i = 0; i < m_cache.size(); ++i)
        for (int g : m_cacheParsed[i].groups)
            if (groups.count(g)) {
                arr->addObject(m_cache[i].object);
                break;
            }
    m_ui->deselectAll();
    if (arr->count()) m_ui->selectObjects(arr, true);
    return int(arr->count());
}
