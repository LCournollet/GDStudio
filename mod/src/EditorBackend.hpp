#pragma once

// LevelBackend implementation on top of the live level editor.
//
// Deliberately uses a minimal, version-stable surface of the game API:
// objects are read with GameObject::getSaveString and created with
// LevelEditorLayer::createObjectsFromString (exactly what copy/paste does), so
// the engine never depends on in-memory object layouts. All calls used here
// have Windows addresses in both the 2.2081 and the 2.2091 bindings.

#include <Geode/Geode.hpp>
#include <gdds/Studio.hpp>

#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

class EditorBackend : public gdds::LevelBackend {
public:
    EditorBackend(LevelEditorLayer* lel, EditorUI* ui);
    ~EditorBackend() override;

    LevelEditorLayer* editor() const { return m_lel; }

    std::vector<gdds::LevelObject> readObjects() override;
    gdds::LevelHeader header() override;
    std::string levelName() override;
    std::vector<std::string> createObjects(std::vector<gdds::LevelObject> const& objects) override;
    size_t removeObjects(std::vector<gdds::LevelObject> const& objects) override;
    std::vector<gdds::ColorChannel> readChannels() override;
    void writeChannels(std::vector<gdds::ColorChannel> const& channels) override;

    bool supportsPreview() const override { return true; }
    void showPreview(std::vector<gdds::LevelObject> const& add, std::vector<gdds::LevelObject> const& hide) override;
    void clearPreview() override;
    bool previewActive() const { return !m_previewObjects.empty() || !m_previewHidden.empty(); }

    void setHiddenTagGroups(std::vector<int> const& groups) override;
    // Re-applies visibility after the editor refreshed its sections.
    void reapplyHidden();

    // Level units <-> object layer node space.
    cocos2d::CCPoint levelToNode(gdds::Vec2 p);
    gdds::Vec2 nodeToLevel(cocos2d::CCPoint p);

    // Objects currently selected in the editor (parsed).
    std::vector<gdds::LevelObject> selectedObjects();
    // Selects every live object carrying one of these groups.
    int selectObjectsWithGroups(std::set<int> const& groups);

    // Called with the id of every created object that the game does not treat
    // as a decoration once it is really in the level (the object is removed
    // again before createObjects returns).
    std::function<void(int)> onNotDecoration;

private:
    struct Live {
        cocos2d::CCObject* object;  // GameObject*, kept as CCObject to avoid layout access
        uint64_t fingerprint;
    };
    void refreshCache();
    void detectOffset(GameObject* obj, gdds::LevelObject const& parsed);
    static std::string saveStringOf(GameObject* obj, LevelEditorLayer* lel);

    LevelEditorLayer* m_lel;
    EditorUI* m_ui;  // from the EditorUI::init hook (avoids reading LevelEditorLayer::m_editorUI)
    std::vector<Live> m_cache;
    std::vector<gdds::LevelObject> m_cacheParsed;
    std::vector<geode::Ref<GameObject>> m_previewObjects;
    std::vector<geode::Ref<GameObject>> m_previewHidden;
    geode::Ref<cocos2d::CCDrawNode> m_previewOverlay;
    std::set<int> m_hiddenTags;
    std::vector<geode::Ref<GameObject>> m_hiddenObjects;
    double m_offsetX = 0, m_offsetY = 90;
    bool m_offsetKnown = false;
};
