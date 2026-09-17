#pragma once
#include "settings.hpp"
#include <windows.h>
#include <memory>
#include <vector>
namespace palette {
// Owns a dedicated message-loop thread. Posts message for a bare Win tap or intercepted Win hotkey.
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
