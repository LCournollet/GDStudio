#pragma once

// Drawing and editing generation masks directly in the editor, in real level
// coordinates. While a tool is active, editor touches are routed here.

#include <Geode/Geode.hpp>
#include <gdds/Mask.hpp>

#include <functional>
#include <string>
#include <vector>

class StudioSession;

enum class MaskToolMode { None, Rectangle, Polygon, Brush, Section, Move, Resize };

class MaskTool {
public:
    explicit MaskTool(StudioSession* session) : m_session(session) {}

    // Starts drawing a new mask (or editing `maskId` for Move/Resize/Brush).
    void begin(MaskToolMode mode, gdds::MaskMode maskMode, std::string maskId = "");
    void cancel();
    // Finishes the current shape (polygon closing, brush end); returns the mask id.
    std::string finish();
    bool active() const { return m_mode != MaskToolMode::None; }
    MaskToolMode mode() const { return m_mode; }
    std::string hint() const;

    // Editor touches (node space of the object layer converted by the caller).
    bool touchBegan(cocos2d::CCPoint worldPos);
    void touchMoved(cocos2d::CCPoint worldPos);
    void touchEnded(cocos2d::CCPoint worldPos);

    // Creates a structure mask from the editor selection.
    std::string structureMaskFromSelection(gdds::MaskMode mode);

    double brushRadius = 30;
    bool showMasks = true;
    std::function<void(std::string const&)> onFinished;  // called with the mask id

    void redraw();
    void detachOverlay();

private:
    gdds::Vec2 toLevel(cocos2d::CCPoint world);
    gdds::Mask* editedMask();

    StudioSession* m_session;
    MaskToolMode m_mode = MaskToolMode::None;
    gdds::MaskMode m_maskMode = gdds::MaskMode::Include;
    std::string m_maskId;
    gdds::Vec2 m_start, m_current;
    bool m_dragging = false;
    std::vector<gdds::Vec2> m_points;
    geode::Ref<cocos2d::CCDrawNode> m_overlay;
};
