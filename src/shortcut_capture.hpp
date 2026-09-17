#pragma once

#include <windows.h>

#include <algorithm>
#include <array>

namespace palette::shortcut_capture {

inline UINT modifier_for_key(UINT key) {
    if (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL) return MOD_CONTROL;
    if (key == VK_MENU || key == VK_LMENU || key == VK_RMENU) return MOD_ALT;
    if (key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT) return MOD_SHIFT;
    if (key == VK_LWIN || key == VK_RWIN) return MOD_WIN;
    return 0;
}

struct Result {
    bool swallow = false;
    bool changed = false;
    bool finished = false;
};

struct State {
    std::array<bool, 256> held_before{};
    std::array<bool, 256> owned{};
    bool active = false;
    bool draining = false;
    bool completed = false;
    bool cancelled = false;
    UINT modifiers = 0;
    UINT key = 0;

    void begin(const std::array<bool, 256>& initially_held = {}) {
        *this = {};
        active = true;
        held_before = initially_held;
        std::fill(held_before.begin(), held_before.begin() + VK_BACK, false);
    }

    bool owns_any_key() const {
        return std::find(owned.begin(), owned.end(), true) != owned.end();
    }

    bool has_preheld_key() const {
        return std::find(held_before.begin() + VK_BACK, held_before.end(), true) != held_before.end();
    }

    UINT current_modifiers() const {
        UINT value = 0;
        for (std::size_t vk = 1; vk < owned.size(); ++vk)
            if (owned[vk]) value |= modifier_for_key(static_cast<UINT>(vk));
        return value;
    }

    Result event(UINT vk, bool down) {
        Result result{};
        if (!active || static_cast<std::size_t>(vk) >= owned.size()) return result;
        if (down) {
            if (draining) {
                result.swallow = owned[vk];
                return result;
            }
            if (has_preheld_key()) return result;
            owned[vk] = true;
            result.swallow = true;
            if (vk == VK_ESCAPE) {
                cancelled = true;
                draining = true;
                result.changed = true;
            } else if (modifier_for_key(vk) == 0) {
                modifiers = current_modifiers();
                key = vk;
                completed = true;
                draining = true;
                result.changed = true;
            } else {
                result.changed = true;
            }
            return result;
        }
        if (held_before[vk] && !owned[vk]) {
            held_before[vk] = false;
            return result;
        }
        if (!owned[vk]) return result;
        owned[vk] = false;
        result.swallow = true;
        result.changed = true;
        result.finished = draining && !owns_any_key();
        return result;
    }

    bool request_stop() {
        if (!active) return true;
        cancelled = true;
        draining = true;
        return !owns_any_key();
    }
};

} // namespace palette::shortcut_capture
