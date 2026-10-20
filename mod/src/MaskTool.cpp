#include "MaskTool.hpp"

#include "Session.hpp"

#include <gdds/Layer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
ccColor4F includeFill{0.f, 0.85f, 1.f, 0.10f}, includeBorder{0.f, 0.85f, 1.f, 0.85f};
ccColor4F excludeFill{1.f, 0.2f, 0.25f, 0.12f}, excludeBorder{1.f, 0.25f, 0.3f, 0.9f};
ccColor4F selectedBorder{1.f, 0.95f, 0.3f, 1.f};
ccColor4F disabledBorder{0.6f, 0.6f, 0.6f, 0.5f};

std::string newMaskId() { return gdds::LayerStack::newId("mask"); }
}  // namespace

gdds::Vec2 MaskTool::toLevel(CCPoint world) {
    auto lel = m_session->editor();
    auto node = lel->m_objectLayer->convertToNodeSpace(world);
    return m_session->backend().nodeToLevel(node);
}

gdds::Mask* MaskTool::editedMask() {
    for (auto& m : m_session->project().masks)
        if (m.id == m_maskId) return &m;
    return nullptr;
}

void MaskTool::begin(MaskToolMode mode, gdds::MaskMode maskMode, std::string maskId) {
    m_mode = mode;
    m_maskMode = maskMode;
    m_maskId = std::move(maskId);
    m_points.clear();
    m_dragging = false;
    m_session->ensureReady();
    if ((mode == MaskToolMode::Brush) && m_maskId.empty()) {
        // Brush strokes accumulate into a new brush mask.
        gdds::Mask m;
        m.id = newMaskId();
        m.name = "Brush " + std::to_string(m_session->project().masks.size() + 1);
        m.shape = gdds::MaskShape::Brush;
        m.mode = maskMode;
        m.enabled = maskMode == gdds::MaskMode::Exclude;
        m_session->beginConfigEdit();
        m_session->project().masks.push_back(m);
        m_maskId = m.id;
    }
    redraw();
}

void MaskTool::cancel() {
    if (m_mode == MaskToolMode::Brush) {
        // An empty brush mask is useless
        auto& masks = m_session->project().masks;
        masks.erase(std::remove_if(masks.begin(), masks.end(),
                                   [&](gdds::Mask const& m) { return m.id == m_maskId && m.strokes.empty(); }),
                    masks.end());
    }
    m_mode = MaskToolMode::None;
    m_points.clear();
    m_dragging = false;
    redraw();
}

std::string MaskTool::finish() {
    std::string id;
    if (m_mode == MaskToolMode::Polygon && m_points.size() >= 3) {
        gdds::Mask m;
        m.id = newMaskId();
        m.name = "Polygon " + std::to_string(m_session->project().masks.size() + 1);
        m.shape = gdds::MaskShape::Polygon;
        m.mode = m_maskMode;
        m.enabled = m_maskMode == gdds::MaskMode::Exclude;  // include masks: local operations only, until enabled
        m.polygon.points = m_points;
        m_session->beginConfigEdit();
        m_session->project().masks.push_back(m);
        id = m.id;
    }
    else if (m_mode == MaskToolMode::Brush || m_mode == MaskToolMode::Move || m_mode == MaskToolMode::Resize) {
        id = m_maskId;
    }
    m_mode = MaskToolMode::None;
    m_points.clear();
    if (!id.empty()) {
        m_session->selectedMask = id;
        m_session->commitConfigEdit("edit mask");
    }
    redraw();
    if (onFinished) onFinished(id);
    return id;
}

std::string MaskTool::hint() const {
    switch (m_mode) {
        case MaskToolMode::Rectangle: return "Drag to draw a rectangle mask";
        case MaskToolMode::Polygon: return "Tap to add points, then Done";
        case MaskToolMode::Brush: return "Paint the allowed area, then Done";
        case MaskToolMode::Section: return "Drag horizontally to select an X range";
        case MaskToolMode::Move: return "Drag to move the selected mask";
        case MaskToolMode::Resize: return "Drag a corner of the selected rectangle";
        default: return "";
    }
}

bool MaskTool::touchBegan(CCPoint world) {
    if (!active()) return false;
    auto p = toLevel(world);
    m_start = m_current = p;
    m_dragging = true;
    switch (m_mode) {
        case MaskToolMode::Polygon:
            m_points.push_back({std::round(p.x * 2) / 2, std::round(p.y * 2) / 2});
            break;
        case MaskToolMode::Brush:
            if (auto m = editedMask()) m->strokes.push_back({p, brushRadius});
            break;
        case MaskToolMode::Move:
        case MaskToolMode::Resize:
            m_session->beginConfigEdit();
            break;
        default: break;
    }
    redraw();
    return true;
}

void MaskTool::touchMoved(CCPoint world) {
    if (!active() || !m_dragging) return;
    auto p = toLevel(world);
    auto prev = m_current;
    m_current = p;
    switch (m_mode) {
        case MaskToolMode::Brush:
            if (auto m = editedMask()) {
                // Space strokes so the mask stays light.
                if (m->strokes.empty() || (m->strokes.back().center - p).length() > brushRadius * 0.4)
                    m->strokes.push_back({p, brushRadius});
            }
            break;
        case MaskToolMode::Move:
            if (auto m = editedMask()) m->translate(p - prev);
            break;
        case MaskToolMode::Resize:
            if (auto m = editedMask(); m && m->shape == gdds::MaskShape::Rectangle) {
                // Move the corner closest to the touch.
                auto& r = m->rect;
                bool left = std::abs(p.x - r.x0) < std::abs(p.x - r.x1);
                bool bottom = std::abs(p.y - r.y0) < std::abs(p.y - r.y1);
                (left ? r.x0 : r.x1) = p.x;
                (bottom ? r.y0 : r.y1) = p.y;
                if (r.x0 > r.x1) std::swap(r.x0, r.x1);
                if (r.y0 > r.y1) std::swap(r.y0, r.y1);
            }
            break;
        default: break;
    }
    redraw();
}

void MaskTool::touchEnded(CCPoint world) {
    if (!active()) return;
    auto p = toLevel(world);
    m_current = p;
    m_dragging = false;
    if (m_mode == MaskToolMode::Rectangle || m_mode == MaskToolMode::Section) {
        if (std::abs(p.x - m_start.x) < 4 && (m_mode == MaskToolMode::Section || std::abs(p.y - m_start.y) < 4)) {
            redraw();
            return;  // a tap, not a drag
        }
        gdds::Mask m;
        m.id = newMaskId();
        m.mode = m_maskMode;
        m.enabled = m_maskMode == gdds::MaskMode::Exclude;  // include masks: local operations only, until enabled
        if (m_mode == MaskToolMode::Rectangle) {
            m.shape = gdds::MaskShape::Rectangle;
            m.name = "Rectangle " + std::to_string(m_session->project().masks.size() + 1);
            m.rect = {std::min(m_start.x, p.x), std::min(m_start.y, p.y), std::max(m_start.x, p.x), std::max(m_start.y, p.y)};
        }
        else {
            m.shape = gdds::MaskShape::Section;
            m.name = "Section " + std::to_string(m_session->project().masks.size() + 1);
            m.sectionX0 = std::min(m_start.x, p.x);
            m.sectionX1 = std::max(m_start.x, p.x);
        }
        m_session->beginConfigEdit();
        m_session->project().masks.push_back(m);
        m_maskId = m.id;
        m_mode = MaskToolMode::None;
        m_session->selectedMask = m.id;
        m_session->commitConfigEdit("new mask");
        redraw();
        if (onFinished) onFinished(m.id);
        return;
    }
    if (m_mode == MaskToolMode::Move || m_mode == MaskToolMode::Resize) {
        m_session->commitConfigEdit("edit mask");
    }
    redraw();
}

std::string MaskTool::structureMaskFromSelection(gdds::MaskMode mode) {
    m_session->ensureReady();
    auto& analysis = m_session->studio().analysis();
    auto selected = m_session->backend().selectedObjects();
    gdds::Mask m;
    m.id = newMaskId();
    m.name = "Structures " + std::to_string(m_session->project().masks.size() + 1);
    m.shape = gdds::MaskShape::Structure;
    m.mode = mode;
    m.enabled = mode == gdds::MaskMode::Exclude;
    std::vector<int> seen;
    for (auto& o : selected) {
        int sid = analysis.structureAt(gdds::LevelAnalysis::cellAt(o.pos));
        if (sid < 0 || std::find(seen.begin(), seen.end(), sid) != seen.end()) continue;
        seen.push_back(sid);
        m.structureSignatures.push_back(analysis.structures[sid].signature);
        m.structureBounds.push_back(analysis.structures[sid].bounds);
    }
    if (seen.empty()) return "";
    m_session->beginConfigEdit();
    m_session->project().masks.push_back(m);
    m_session->selectedMask = m.id;
    m_session->commitConfigEdit("structure mask");
    redraw();
    return m.id;
}

void MaskTool::detachOverlay() {
    if (m_overlay) {
        m_overlay->removeFromParent();
        m_overlay = nullptr;
    }
}

void MaskTool::redraw() {
    auto lel = m_session->editor();
    if (!lel || !lel->m_objectLayer) return;
    if (!m_overlay) {
        m_overlay = CCDrawNode::create();
        lel->m_objectLayer->addChild(m_overlay, 9999);
    }
    m_overlay->clear();
    if (!showMasks && !active()) return;
    auto& backend = m_session->backend();
    auto toNode = [&](gdds::Vec2 p) { return backend.levelToNode(p); };
    auto rectPoly = [&](gdds::Rect r, ccColor4F fill, ccColor4F border, float w) {
        CCPoint v[4] = {toNode({r.x0, r.y0}), toNode({r.x1, r.y0}), toNode({r.x1, r.y1}), toNode({r.x0, r.y1})};
        m_overlay->drawPolygon(v, 4, fill, w, border);
    };
    // Visible vertical extent used for section masks.
    gdds::Rect view{-1e5, -300, 1e7, 3000};
    for (auto& m : m_session->project().masks) {
        bool ex = m.mode == gdds::MaskMode::Exclude;
        auto fill = ex ? excludeFill : includeFill;
        auto border = m.id == m_session->selectedMask ? selectedBorder : (!m.enabled ? disabledBorder : (ex ? excludeBorder : includeBorder));
        float w = m.id == m_session->selectedMask ? 1.4f : 0.8f;
        switch (m.shape) {
            case gdds::MaskShape::Rectangle: rectPoly(m.rect, fill, border, w); break;
            case gdds::MaskShape::Section:
                rectPoly({std::min(m.sectionX0, m.sectionX1), view.y0, std::max(m.sectionX0, m.sectionX1), view.y1}, fill,
                         border, w);
                break;
            case gdds::MaskShape::Structure:
                for (auto& b : m.structureBounds) rectPoly(b.expanded(m.structureMargin), fill, border, w);
                break;
            case gdds::MaskShape::Polygon: {
                std::vector<CCPoint> pts;
                for (auto& p : m.polygon.points) pts.push_back(toNode(p));
                if (pts.size() >= 3) m_overlay->drawPolygon(pts.data(), unsigned(pts.size()), fill, w, border);
                break;
            }
            case gdds::MaskShape::Brush:
                for (auto& s : m.strokes) m_overlay->drawDot(toNode(s.center), float(s.radius), fill);
                break;
        }
    }
    // Shape being drawn
    if (m_mode == MaskToolMode::Rectangle && m_dragging) {
        rectPoly({std::min(m_start.x, m_current.x), std::min(m_start.y, m_current.y), std::max(m_start.x, m_current.x),
                  std::max(m_start.y, m_current.y)},
                 m_maskMode == gdds::MaskMode::Exclude ? excludeFill : includeFill, selectedBorder, 1.2f);
    }
    if (m_mode == MaskToolMode::Section && m_dragging) {
        rectPoly({std::min(m_start.x, m_current.x), view.y0, std::max(m_start.x, m_current.x), view.y1}, includeFill,
                 selectedBorder, 1.2f);
    }
    if (m_mode == MaskToolMode::Polygon) {
        for (size_t i = 0; i < m_points.size(); ++i) {
            m_overlay->drawDot(toNode(m_points[i]), 3.f, selectedBorder);
            if (i > 0) m_overlay->drawSegment(toNode(m_points[i - 1]), toNode(m_points[i]), 0.8f, selectedBorder);
        }
    }
}
