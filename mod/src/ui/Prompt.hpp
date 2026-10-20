#pragma once

// Small text prompt popup (rename layer, name a preset / collection / mask).

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <string>

class TextPrompt : public geode::Popup {
public:
    static TextPrompt* create(std::string const& title, std::string const& initial, std::function<void(std::string)> onOk);

protected:
    bool init(std::string const& title, std::string const& initial, std::function<void(std::string)> onOk);

private:
    geode::TextInput* m_input = nullptr;
    std::function<void(std::string)> m_onOk;
};
