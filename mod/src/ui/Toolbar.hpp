#pragma once

// Small persistent toolbar in the editor: opens the studio, and shows the
// contextual actions of the preview (Apply / Cancel) and of the mask tool
// (Done / Cancel) while the studio window is closed.

#include <Geode/Geode.hpp>

class Toolbar : public cocos2d::CCNode {
public:
    static Toolbar* create();
    static void refreshCurrent();
    void refresh();
    static void openStudio();

    // Moves the toolbar to the first spot of the editor's left side that no
    // editor button covers (measured at runtime, so it follows GD's layout and
    // other mods' buttons instead of a hard-coded position).
    void placeClearOfEditorButtons();

protected:
    bool init() override;
    void onExit() override;
    void tick(float dt);
    void placeOnce(float dt);

private:
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    bool m_wasRunning = false;
};
