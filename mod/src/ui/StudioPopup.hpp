#pragma once

// The Decoration Studio window: one compact popup with tabs.

#include "../Session.hpp"
#include "Widgets.hpp"

#include <Geode/ui/Popup.hpp>

class StudioPopup : public geode::Popup {
public:
    enum Tab { Layers = 0, Generate, Colors, Objects, Masks, Presets, Stats, TabCount };

    static StudioPopup* create(StudioSession* session);
    static StudioPopup* current();

    void showTab(int tab);
    void refresh();  // rebuilds the current tab
    void setStatus(std::string const& text);
    StudioSession* session() const { return m_session; }

    // Closes the window to let the user work in the editor (e.g. mask drawing);
    // the toolbar can reopen it on the same tab.
    void hideForEditing();

    static constexpr float kWidth = 470.f;
    static constexpr float kHeight = 290.f;
    static constexpr float kContentWidth = 375.f;
    static constexpr float kContentHeight = 226.f;

protected:
    bool init(StudioSession* session);
    void onClose(cocos2d::CCObject*) override;

private:
    StudioSession* m_session = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;
};

// Tab builders (one file each)
namespace tabs {
void buildLayers(StudioPopup* popup, ui::Column& col);
void buildGenerate(StudioPopup* popup, ui::Column& col);
void buildColors(StudioPopup* popup, ui::Column& col);
void buildObjects(StudioPopup* popup, ui::Column& col);
void buildMasks(StudioPopup* popup, ui::Column& col);
void buildPresets(StudioPopup* popup, ui::Column& col);
void buildStats(StudioPopup* popup, ui::Column& col);
}  // namespace tabs
