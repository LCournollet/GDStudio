// GD Decoration Studio - editor hooks.
//
// Every hook used here exists with a Windows address in both the 2.2081 and
// the (experimental) 2.2091 bindings.

#include "Session.hpp"
#include "ui/StudioPopup.hpp"
#include "ui/Toolbar.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

using namespace geode::prelude;

class $modify(GDDSEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        StudioSession::begin(lel, this);
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto toolbar = Toolbar::create();
        toolbar->setPosition({12.f, winSize.height - 96.f});
        toolbar->setID("gd-decoration-studio-toolbar"_spr);
        this->addChild(toolbar, 100);
        return true;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto s = StudioSession::current();
        if (s && s->maskTool().active()) return s->maskTool().touchBegan(touch->getLocation());
        return EditorUI::ccTouchBegan(touch, event);
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        auto s = StudioSession::current();
        if (s && s->maskTool().active()) {
            s->maskTool().touchMoved(touch->getLocation());
            return;
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        auto s = StudioSession::current();
        if (s && s->maskTool().active()) {
            s->maskTool().touchEnded(touch->getLocation());
            Toolbar::refreshCurrent();
            return;
        }
        EditorUI::ccTouchEnded(touch, event);
    }

    void keyDown(enumKeyCodes key, double timestamp) {
        auto s = StudioSession::current();
        if (s && s->maskTool().active()) {
            if (key == KEY_Escape) {
                s->maskTool().cancel();
                Toolbar::refreshCurrent();
                return;
            }
            if (key == KEY_Enter) {
                s->maskTool().finish();
                Toolbar::refreshCurrent();
                Toolbar::openStudio();
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};

class $modify(GDDSLevelEditorLayer, LevelEditorLayer) {
    void updateVisibility(float dt) {
        LevelEditorLayer::updateVisibility(dt);
        if (auto s = StudioSession::current()) s->backend().reapplyHidden();
    }
};

// EditorPauseLayer::saveLevel has no Windows address in the 2.2091 bindings:
// the three save buttons are hooked instead (they exist in both versions).
namespace {
void beforeLevelSave() {
    auto s = StudioSession::current();
    if (!s) return;
    // Never save preview objects or a half-drawn mask into the level.
    s->abortJob();
    if (s->studio().hasPendingPreview()) {
        s->studio().cancelPreview();
        Notification::create("Decoration preview cancelled before saving", NotificationIcon::Info)->show();
    }
    if (s->maskTool().active()) s->maskTool().cancel();
    // Hidden layers are shown again so the saved level is complete.
    s->backend().setHiddenTagGroups({});
}

void afterLevelSave(bool leaving) {
    auto s = StudioSession::current();
    if (!s) return;
    s->save();
    if (leaving) StudioSession::end();
    else s->studio().refreshVisibility();
}
}  // namespace

class $modify(GDDSEditorPauseLayer, EditorPauseLayer) {
    void onSave(CCObject* sender) {
        beforeLevelSave();
        EditorPauseLayer::onSave(sender);
        afterLevelSave(false);
    }

    void onSaveAndPlay(CCObject* sender) {
        beforeLevelSave();
        EditorPauseLayer::onSaveAndPlay(sender);
        afterLevelSave(true);
    }

    void onSaveAndExit(CCObject* sender) {
        beforeLevelSave();
        EditorPauseLayer::onSaveAndExit(sender);
        afterLevelSave(true);
    }

    void onExitNoSave(CCObject* sender) {
        if (auto s = StudioSession::current()) {
            if (s->studio().hasPendingPreview()) s->studio().cancelPreview();
            s->backend().setHiddenTagGroups({});
        }
        EditorPauseLayer::onExitNoSave(sender);
        // Unsaved level: the project file is still saved; at the next opening
        // the reconciliation drops records of objects that were never saved.
        StudioSession::end();
    }
};

$on_mod(Loaded) {
    listenForKeybindSettingPresses("open-studio", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return ListenerResult::Propagate;
        if (!StudioSession::current()) return ListenerResult::Propagate;
        if (auto popup = StudioPopup::current()) popup->hideForEditing();
        else Toolbar::openStudio();
        return ListenerResult::Stop;
    });
}
