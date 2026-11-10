#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "aether/platform.h"

namespace aether {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool keyFromName(const std::string& nameIn, Key& out) {
    static const std::unordered_map<std::string, Key> table = [] {
        std::unordered_map<std::string, Key> t;
        for (char c = 'a'; c <= 'z'; c++) t[std::string(1, c)] = (Key)((int)Key::A + (c - 'a'));
        for (char c = '0'; c <= '9'; c++) t[std::string(1, c)] = (Key)((int)Key::Num0 + (c - '0'));
        for (int i = 1; i <= 12; i++) t["f" + std::to_string(i)] = (Key)((int)Key::F1 + i - 1);
        t["left"] = Key::Left; t["right"] = Key::Right; t["up"] = Key::Up; t["down"] = Key::Down;
        t["space"] = Key::Space; t["enter"] = Key::Enter; t["return"] = Key::Enter;
        t["escape"] = Key::Escape; t["esc"] = Key::Escape; t["tab"] = Key::Tab;
        t["backspace"] = Key::Backspace; t["shift"] = Key::Shift; t["ctrl"] = Key::Ctrl; t["alt"] = Key::Alt;
        return t;
    }();
    auto it = table.find(lower(nameIn));
    if (it == table.end()) return false;
    out = it->second;
    return true;
}

bool padButtonFromName(const std::string& nameIn, PadButton& out) {
    static const std::unordered_map<std::string, PadButton> table = {
        {"south", PadButton::South}, {"east", PadButton::East}, {"west", PadButton::West}, {"north", PadButton::North},
        {"lb", PadButton::LeftShoulder}, {"rb", PadButton::RightShoulder},
        {"back", PadButton::Back}, {"start", PadButton::Start},
        {"lstick", PadButton::LeftStick}, {"rstick", PadButton::RightStick},
        {"dpad_up", PadButton::DpadUp}, {"dpad_down", PadButton::DpadDown},
        {"dpad_left", PadButton::DpadLeft}, {"dpad_right", PadButton::DpadRight},
    };
    auto it = table.find(lower(nameIn));
    if (it == table.end()) return false;
    out = it->second;
    return true;
}

bool padAxisFromName(const std::string& nameIn, PadAxis& out) {
    static const std::unordered_map<std::string, PadAxis> table = {
        {"leftx", PadAxis::LeftX}, {"lefty", PadAxis::LeftY}, {"rightx", PadAxis::RightX}, {"righty", PadAxis::RightY},
        {"lt", PadAxis::LeftTrigger}, {"rt", PadAxis::RightTrigger},
    };
    auto it = table.find(lower(nameIn));
    if (it == table.end()) return false;
    out = it->second;
    return true;
}

}  // namespace aether
