#include "Widgets.hpp"

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <cstdio>

using namespace geode::prelude;

namespace ui {

std::string fmtNumber(double v, int precision) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
    return buf;
}

CCLabelBMFont* label(std::string const& text, float scale, ccColor3B color, char const* font) {
    auto l = CCLabelBMFont::create(text.c_str(), font);
    l->setScale(scale);
    l->setColor(color);
    return l;
}

void fitLabel(CCLabelBMFont* l, float maxWidth) {
    if (l->getScaledContentSize().width <= maxWidth) return;
    std::string s = l->getString();
    while (s.size() > 3 && l->getScaledContentSize().width > maxWidth) {
        s.pop_back();
        l->setString((s + "...").c_str());
    }
}

CCMenuItemSpriteExtra* button(std::string const& text, std::function<void()> callback, float scale, char const* bg,
                              int minWidth) {
    auto spr = ButtonSprite::create(text.c_str(), minWidth, minWidth, 0.6f, false, "bigFont.fnt", bg, 25.f);
    spr->setScale(scale);
    auto btn = CCMenuItemExt::createSpriteExtra(spr, [callback](auto) {
        if (callback) callback();
    });
    return btn;
}

CCMenuItemSpriteExtra* iconButton(char const* frame, std::function<void()> callback, float scale) {
    auto spr = CCSprite::createWithSpriteFrameName(frame);
    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    spr->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(spr, [callback](auto) {
        if (callback) callback();
    });
}

CCMenuItemToggler* toggle(bool on, std::function<void(bool)> callback, float scale) {
    auto t = CCMenuItemExt::createTogglerWithStandardSprites(scale, [callback](CCMenuItemToggler* self) {
        // inside the callback isToggled() still returns the previous state
        if (callback) callback(!self->isToggled());
    });
    t->toggle(on);
    return t;
}

CCMenuItemSpriteExtra* swatch(ccColor3B color, std::function<void()> callback, float size) {
    CCSprite* spr = nullptr;
    for (auto frame : {"GJ_colorBtn_001.png", "whiteSquare20_001.png"})
        if ((spr = CCSprite::createWithSpriteFrameName(frame))) break;
    if (!spr) spr = CCSprite::create("square02_small.png");
    if (!spr) {
        spr = CCSprite::create();
        spr->setTextureRect({0, 0, 20, 20});
    }
    spr->setColor(color);
    spr->setScale(size / std::max(1.f, spr->getContentSize().width));
    return CCMenuItemExt::createSpriteExtra(spr, [callback](auto) {
        if (callback) callback();
    });
}

// -----------------------------------------------------------------------------

CCMenu* Column::row(float height, std::optional<ccColor4B> background) {
    auto container = CCNode::create();
    container->setContentSize({m_width, height});
    if (background) {
        auto bg = CCLayerColor::create(*background, m_width, height - 1);
        bg->setPosition({0, 0.5f});
        container->addChild(bg, -1);
    }
    auto menu = CCMenu::create();
    menu->setContentSize({m_width, height});
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({0, 0.5f});
    menu->setPosition({0, height / 2});
    container->addChild(menu);
    m_nodes.push_back({container, m_y});
    m_root->addChild(container);
    m_y += height;
    return menu;
}

void Column::title(std::string const& text) {
    auto menu = row(20);
    auto l = label(text, 0.42f, kAccent, "goldFont.fnt");
    l->setAnchorPoint({0, 0.5f});
    l->setPosition({4, 0});
    menu->addChild(l);
}

void Column::text(std::string const& t, float scale, ccColor3B color) {
    auto area = SimpleTextArea::create(t, "chatFont.fnt", scale * 2.2f, m_width - 8);
    area->setColor({color.r, color.g, color.b, 255});
    float h = area->getContentSize().height + 4;
    auto menu = row(h);
    area->setAnchorPoint({0, 0.5f});
    area->setPosition({4, 0});
    menu->addChild(area);
}

void Column::slider(std::string const& name, float value, float min, float max, std::function<void(float)> onChange,
                    unsigned precision) {
    auto menu = row(26);
    auto l = label(name, 0.32f, kText);
    l->setAnchorPoint({0, 0.5f});
    l->setPosition({4, 0});
    menu->addChild(l);
    auto valueLabel = label(fmtNumber(value, int(precision)), 0.3f, kMuted);
    valueLabel->setAnchorPoint({1, 0.5f});
    valueLabel->setPosition({m_width - 4, 0});
    menu->addChild(valueLabel);
    auto slider = SliderNode::create([onChange, valueLabel, precision](SliderNode*, float v) {
        valueLabel->setString(fmtNumber(v, int(precision)).c_str());
        if (onChange) onChange(v);
    });
    slider->setMin(min);
    slider->setMax(max);
    slider->setValue(value);
    slider->setScale(0.55f);
    slider->setAnchorPoint({0.5f, 0.5f});
    slider->setPosition({m_width * 0.6f - 10, 0});
    menu->addChild(slider);
}

void Column::toggleRow(std::string const& name, bool on, std::function<void(bool)> callback) {
    auto menu = row(22);
    auto t = toggle(on, std::move(callback), 0.5f);
    t->setPosition({14, 0});
    menu->addChild(t);
    auto l = label(name, 0.32f, kText);
    l->setAnchorPoint({0, 0.5f});
    l->setPosition({28, 0});
    menu->addChild(l);
}

geode::TextInput* Column::input(std::string const& name, std::string const& value, std::function<void(std::string)> onChange,
                                std::string const& filter) {
    auto menu = row(26);
    auto l = label(name, 0.32f, kText);
    l->setAnchorPoint({0, 0.5f});
    l->setPosition({4, 0});
    menu->addChild(l);
    auto in = TextInput::create(m_width * 0.55f, name);
    if (!filter.empty()) in->setFilter(filter);
    in->setString(value);
    in->setCallback([onChange](std::string const& s) {
        if (onChange) onChange(s);
    });
    in->setScale(0.7f);
    in->setAnchorPoint({1, 0.5f});
    in->setPosition({m_width - 4, 0});
    menu->addChild(in);
    return in;
}

void Column::buttons(std::vector<std::pair<std::string, std::function<void()>>> const& items, float scale) {
    auto menu = row(26);
    float slot = m_width / float(std::max<size_t>(1, items.size()));
    for (size_t i = 0; i < items.size(); ++i) {
        auto b = button(items[i].first, items[i].second, scale);
        b->setPosition({slot * (float(i) + 0.5f), 0});
        menu->addChild(b);
    }
}

geode::ScrollLayer* Column::finish(CCSize size) {
    float total = std::max(m_y, size.height);
    m_root->setContentSize({m_width, total});
    for (auto& [node, top] : m_nodes) {
        float h = node->getContentSize().height;
        node->setPosition({0, total - top - h});
    }
    auto scroll = ScrollLayer::create(size);
    scroll->m_contentLayer->setContentSize({size.width, total});
    scroll->m_contentLayer->addChild(m_root);
    m_root->setPosition({(size.width - m_width) / 2, 0});
    scroll->scrollToTop();
    return scroll;
}

}  // namespace ui
