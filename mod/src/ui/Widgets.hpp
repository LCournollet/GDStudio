#pragma once

// Small UI toolkit for the studio (dark theme, compact rows).

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ui {

// Theme
inline cocos2d::ccColor3B const kText{230, 230, 240};
inline cocos2d::ccColor3B const kMuted{150, 150, 170};
inline cocos2d::ccColor3B const kAccent{90, 200, 255};
inline cocos2d::ccColor3B const kWarn{255, 170, 80};
inline cocos2d::ccColor4B const kPanel{18, 18, 26, 235};
inline cocos2d::ccColor4B const kRow{32, 32, 44, 200};
inline cocos2d::ccColor4B const kRowSelected{40, 70, 110, 230};

cocos2d::CCLabelBMFont* label(std::string const& text, float scale = 0.4f, cocos2d::ccColor3B color = kText,
                              char const* font = "bigFont.fnt");
// Truncates to a max width (in points) with an ellipsis.
void fitLabel(cocos2d::CCLabelBMFont* label, float maxWidth);

CCMenuItemSpriteExtra* button(std::string const& text, std::function<void()> callback, float scale = 0.5f,
                              char const* bg = "GJ_button_04.png", int minWidth = 0);
CCMenuItemSpriteExtra* iconButton(char const* frame, std::function<void()> callback, float scale = 0.6f);
CCMenuItemToggler* toggle(bool on, std::function<void(bool)> callback, float scale = 0.55f);

// A colored square (palette swatch) as a button.
CCMenuItemSpriteExtra* swatch(cocos2d::ccColor3B color, std::function<void()> callback, float size = 18.f);

// Vertical list builder: rows are stacked top-down inside a fixed width.
class Column {
public:
    explicit Column(float width) : m_width(width) { m_root = cocos2d::CCNode::create(); }

    float width() const { return m_width; }
    // Adds a full-width row of the given height; returns its menu (for items)
    // positioned so that (0,0) is the row's left-center.
    cocos2d::CCMenu* row(float height, std::optional<cocos2d::ccColor4B> background = std::nullopt);
    // Section title
    void title(std::string const& text);
    // Free text (wrapped by width)
    void text(std::string const& text, float scale = 0.32f, cocos2d::ccColor3B color = kMuted);
    // Labelled slider: callback on release and while sliding.
    void slider(std::string const& name, float value, float min, float max, std::function<void(float)> onChange,
                unsigned precision = 2);
    // Labelled toggle
    void toggleRow(std::string const& name, bool on, std::function<void(bool)> callback);
    // Labelled text input
    geode::TextInput* input(std::string const& name, std::string const& value, std::function<void(std::string)> onChange,
                            std::string const& filter = "");
    // Row of buttons (equal spacing)
    void buttons(std::vector<std::pair<std::string, std::function<void()>>> const& items, float scale = 0.45f);
    void spacer(float height) { m_y += height; }

    // Wraps the content in a scroll layer of the given size.
    geode::ScrollLayer* finish(cocos2d::CCSize size);
    cocos2d::CCNode* root() { return m_root; }
    float height() const { return m_y; }

private:
    cocos2d::CCNode* m_root;
    float m_width;
    float m_y = 0;  // grows downward
    std::vector<std::pair<cocos2d::CCNode*, float>> m_nodes;  // node, top y
};

std::string fmtNumber(double v, int precision = 2);

}  // namespace ui
