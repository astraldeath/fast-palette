#pragma once
#include "settings.hpp"
#include <windows.h>
#include <memory>
#include <vector>
namespace palette {
// Posts message with wParam=VK_LWIN/VK_RWIN for bare taps, or 0 for Win hotkeys.
class KeyboardHook {
public:
    KeyboardHook(HWND target, UINT message);
    ~KeyboardHook();
    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;
    bool start(bool left, bool right, const std::vector<Hotkey>& bindings = {});
    bool suspend();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
