#include "StudioPopup.hpp"

#include "Toolbar.hpp"

using namespace geode::prelude;

namespace {
StudioPopup* g_current = nullptr;
char const* kTabNames[] = {"Layers", "Generate", "Colors", "Objects", "Masks", "Presets", "Stats"};
}  // namespace

StudioPopup* StudioPopup::create(StudioSession* session) {
    auto ret = new StudioPopup();
    if (ret->init(session)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

StudioPopup* StudioPopup::current() { return g_current; }

bool StudioPopup::init(StudioSession* session) {
    if (!Popup::init(kWidth, kHeight, "GJ_square05.png")) return false;
    m_session = session;
    g_current = this;
    this->setTitle("Decoration Studio", "goldFont.fnt", 0.7f, 16.f);
    session->ensureReady();
    // The user may have edited the gameplay since the last analysis.
    session->studio().invalidateAnalysis();

    // Dark inner panel
    auto panel = CCLayerColor::create(ui::kPanel, kWidth - 20, kHeight - 50);
    panel->setPosition({10, 26});
    m_mainLayer->addChild(panel, -1);

    // Tabs (left column)
    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({0, 0});
    m_mainLayer->addChild(m_tabMenu);
    for (int i = 0; i < TabCount; ++i) {
        auto btn = ui::button(kTabNames[i], [this, i] { showTab(i); }, 0.42f, "GJ_button_04.png", 120);
        btn->setPosition({50.f, kHeight - 52.f - i * 30.f});
        m_tabMenu->addChild(btn);
        m_tabButtons.push_back(btn);
    }

    m_content = CCNode::create();
    m_content->setPosition({kWidth - kContentWidth - 12.f, 30.f});
    m_mainLayer->addChild(m_content);

    m_status = ui::label("", 0.3f, ui::kMuted);
    m_status->setAnchorPoint({0, 0.5f});
    m_status->setPosition({14.f, 15.f});
    m_mainLayer->addChild(m_status);
    setStatus(session->lastStatus());

    showTab(session->activeTab);
    return true;
}

void StudioPopup::showTab(int tab) {
    m_session->activeTab = tab;
    for (int i = 0; i < int(m_tabButtons.size()); ++i) {
        auto spr = static_cast<ButtonSprite*>(m_tabButtons[i]->getNormalImage());
        if (spr) spr->updateBGImage(i == tab ? "GJ_button_02.png" : "GJ_button_04.png");
    }
    m_content->removeAllChildren();
    ui::Column col(kContentWidth - 10);
    switch (tab) {
        case Layers: tabs::buildLayers(this, col); break;
        case Generate: tabs::buildGenerate(this, col); break;
        case Colors: tabs::buildColors(this, col); break;
        case Objects: tabs::buildObjects(this, col); break;
        case Masks: tabs::buildMasks(this, col); break;
        case Presets: tabs::buildPresets(this, col); break;
        case Stats: tabs::buildStats(this, col); break;
        default: break;
    }
    auto scroll = col.finish({kContentWidth, kContentHeight});
    m_content->addChild(scroll);
    m_session->maskTool().showMasks = tab == Masks || m_session->maskTool().showMasks;
}

void StudioPopup::refresh() { showTab(m_session->activeTab); }

void StudioPopup::setStatus(std::string const& text) {
    if (!m_status) return;
    m_status->setString(text.c_str());
    ui::fitLabel(m_status, kWidth - 30);
}

void StudioPopup::hideForEditing() { this->onClose(nullptr); }

void StudioPopup::onClose(CCObject* sender) {
    if (g_current == this) g_current = nullptr;
    m_session->commitConfigEdit("settings");
    Toolbar::refreshCurrent();
    Popup::onClose(sender);
}
