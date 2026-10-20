#include "Toolbar.hpp"

#include "../Session.hpp"
#include "StudioPopup.hpp"
#include "Widgets.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
Toolbar* g_toolbar = nullptr;

CCRect worldRect(CCNode* node) {
    auto bb = node->boundingBox();
    auto parent = node->getParent();
    if (!parent) return bb;
    auto a = parent->convertToWorldSpace(bb.origin);
    auto b = parent->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
    return CCRect(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y));
}

// Screen rectangles of every visible button / slider below `node`.
void collectButtons(CCNode* node, CCNode* skip, std::vector<CCRect>& out) {
    if (!node || node == skip || !node->isVisible()) return;
    if (geode::cast::typeinfo_cast<CCMenuItem*>(node) || geode::cast::typeinfo_cast<Slider*>(node)) {
        auto r = worldRect(node);
        if (r.size.width > 1 && r.size.height > 1) out.push_back(r);
        return;
    }
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) collectButtons(child, skip, out);
}
}  // namespace

Toolbar* Toolbar::create() {
    auto ret = new Toolbar();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool Toolbar::init() {
    if (!CCNode::init()) return false;
    g_toolbar = this;
    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu);
    m_hint = ui::label("", 0.3f, ui::kAccent);
    m_hint->setAnchorPoint({0, 0.5f});
    this->addChild(m_hint);
    refresh();
    this->schedule(schedule_selector(Toolbar::tick), 0.1f);
    // The editor finishes laying out its buttons after EditorUI::init.
    this->scheduleOnce(schedule_selector(Toolbar::placeOnce), 0.f);
    return true;
}

void Toolbar::placeOnce(float) { placeClearOfEditorButtons(); }

void Toolbar::placeClearOfEditorButtons() {
    auto parent = this->getParent();
    if (!parent || !m_menu || !m_menu->getChildrenCount()) return;
    std::vector<CCRect> taken;
    collectButtons(parent, this, taken);
    // Size of the always-present "Studio" button.
    auto studio = static_cast<CCNode*>(m_menu->getChildren()->objectAtIndex(0));
    float w = studio->getScaledContentSize().width, h = studio->getScaledContentSize().height;
    auto win = CCDirector::sharedDirector()->getWinSize();
    float margin = 4.f;
    float lowest = win.height * 0.3f;  // stay above the object panel
    for (float x : {8.f, 70.f, 130.f}) {
        for (float y = win.height - h / 2 - margin; y >= lowest; y -= 2.f) {
            CCRect r(x - margin, y - h / 2 - margin, w + 2 * margin, h + 2 * margin);
            bool clear = std::none_of(taken.begin(), taken.end(), [&](CCRect const& t) { return t.intersectsRect(r); });
            if (!clear) continue;
            this->setPosition(parent->convertToNodeSpace({x, y}));
            log::info("Toolbar placed at ({:.0f}, {:.0f}), clear of {} editor buttons", x, y, taken.size());
            return;
        }
    }
    log::warn("No free spot found for the toolbar among {} editor buttons; keeping its default position", taken.size());
}

void Toolbar::tick(float) {
    auto s = StudioSession::current();
    if (!s) return;
    if (s->jobRunning()) s->pollJob();
    bool running = s->jobRunning();
    if (m_wasRunning && !running) {
        refresh();
        if (auto p = StudioPopup::current()) p->refresh();
    }
    else if (running) {
        if (!m_wasRunning) refresh();
        m_hint->setString(fmt::format("{} {:.1f} s", s->lastStatus(), s->jobSeconds()).c_str());
    }
    m_wasRunning = running;
}

void Toolbar::onExit() {
    if (g_toolbar == this) g_toolbar = nullptr;
    CCNode::onExit();
}

void Toolbar::refreshCurrent() {
    if (g_toolbar) g_toolbar->refresh();
}

void Toolbar::openStudio() {
    auto session = StudioSession::current();
    if (!session) return;
    if (StudioPopup::current()) return;
    if (auto popup = StudioPopup::create(session)) popup->show();
    refreshCurrent();
}

void Toolbar::refresh() {
    m_menu->removeAllChildren();
    m_hint->setString("");
    auto session = StudioSession::current();
    float x = 0;
    auto add = [&](CCMenuItemSpriteExtra* item) {
        item->setPosition({x + item->getScaledContentSize().width / 2, 0});
        x += item->getScaledContentSize().width + 4;
        m_menu->addChild(item);
    };
    add(ui::button("Studio", [] { Toolbar::openStudio(); }, 0.5f, "GJ_button_05.png"));
    if (!session || !session->ready()) return;

    if (session->jobRunning()) {
        add(ui::button("Cancel generation", [] {
            if (auto s = StudioSession::current()) s->cancelJob();
        }, 0.45f, "GJ_button_06.png"));
        m_hint->setString(session->lastStatus().c_str());
        m_hint->setPosition({x + 4, 0});
        return;
    }

    auto& tool = session->maskTool();
    if (tool.active()) {
        add(ui::button("Done", [] {
            if (auto s = StudioSession::current()) {
                s->maskTool().finish();
                Toolbar::refreshCurrent();
                Toolbar::openStudio();
            }
        }, 0.45f, "GJ_button_01.png"));
        add(ui::button("Cancel", [] {
            if (auto s = StudioSession::current()) {
                s->maskTool().cancel();
                Toolbar::refreshCurrent();
            }
        }, 0.45f, "GJ_button_06.png"));
        m_hint->setString(tool.hint().c_str());
        m_hint->setPosition({x + 4, 0});
        return;
    }
    if (session->studio().hasPendingPreview()) {
        add(ui::button("Apply", [] {
            if (auto s = StudioSession::current()) {
                auto out = s->studio().applyPreview();
                s->report(out);
                s->save();
                Toolbar::refreshCurrent();
                if (auto p = StudioPopup::current()) p->refresh();
            }
        }, 0.45f, "GJ_button_01.png"));
        add(ui::button("Cancel", [] {
            if (auto s = StudioSession::current()) {
                s->studio().cancelPreview();
                s->status("Preview cancelled");
                Toolbar::refreshCurrent();
                if (auto p = StudioPopup::current()) p->refresh();
            }
        }, 0.45f, "GJ_button_06.png"));
        m_hint->setString("Preview: proposed objects are outlined in cyan");
        m_hint->setPosition({x + 4, 0});
    }
}
