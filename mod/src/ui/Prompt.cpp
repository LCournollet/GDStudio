#include "Prompt.hpp"

#include "Widgets.hpp"

using namespace geode::prelude;

TextPrompt* TextPrompt::create(std::string const& title, std::string const& initial, std::function<void(std::string)> onOk) {
    auto ret = new TextPrompt();
    if (ret->init(title, initial, std::move(onOk))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TextPrompt::init(std::string const& title, std::string const& initial, std::function<void(std::string)> onOk) {
    if (!Popup::init(260.f, 120.f)) return false;
    m_onOk = std::move(onOk);
    this->setTitle(title);
    m_input = TextInput::create(200.f, "Name");
    m_input->setString(initial);
    m_input->setMaxCharCount(48);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0, 5});
    auto ok = ui::button("OK", [this] {
        auto text = m_input->getString();
        auto cb = m_onOk;
        this->onClose(nullptr);
        if (cb && !text.empty()) cb(text);
    }, 0.6f, "GJ_button_01.png");
    m_buttonMenu->addChildAtPosition(ok, Anchor::Bottom, {0, 22});
    return true;
}
