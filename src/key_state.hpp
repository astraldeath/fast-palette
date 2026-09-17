#pragma once
#include "settings.hpp"
#include <windows.h>
#include <array>
#include <vector>

namespace palette::detail {

struct KeyDecision {
    bool invoke = false;
    bool suppress = false;
    bool mask_release = false;
};

class KeyState {
public:
    KeyState(bool left_enabled, bool right_enabled, std::vector<Hotkey> bindings = {})
        : left_enabled_(left_enabled), right_enabled_(right_enabled) {
        configure(left_enabled, right_enabled, std::move(bindings));
    }

    void configure(bool left_enabled, bool right_enabled, std::vector<Hotkey> bindings) {
        left_enabled_ = left_enabled;
        right_enabled_ = right_enabled;
        bindings_.clear();
        for (const auto& binding : bindings) {
            if ((binding.modifiers & MOD_WIN) != 0 && binding.key < down_.size() &&
                binding.key != VK_LWIN && binding.key != VK_RWIN) {
                bindings_.push_back(binding);
            }
        }
        cancel_candidates();
    }

    void seed_down(DWORD virtual_key) {
        if (virtual_key >= down_.size()) return;
        down_[virtual_key] = true;
        cancel_candidates();
    }

    KeyDecision key(DWORD virtual_key, bool down, bool injected = false,
                    bool preexisting_chord = false, UINT live_modifiers = 0) {
        if (virtual_key >= down_.size()) return {};
        const bool win = virtual_key == VK_LWIN || virtual_key == VK_RWIN;
        if (injected) {
            if (down && !win) cancel_candidates();
            return {};
        }
        if (!win) {
            if (consumed_[virtual_key]) {
                down_[virtual_key] = down;
                if (!down) consumed_[virtual_key] = false;
                return {false, true, false};
            }
            if (down) {
                const bool repeated = down_[virtual_key];
                down_[virtual_key] = true;
                cancel_candidates();
                if (!repeated && any_win_down() && matches_binding(virtual_key, live_modifiers)) {
                    consumed_[virtual_key] = true;
                    mark_active_win_chord();
                    return {true, true, false};
                }
            } else {
                down_[virtual_key] = false;
            }
            return {};
        }

        const bool left = virtual_key == VK_LWIN;
        bool& candidate = left ? left_candidate_ : right_candidate_;
        if (down) {
            if (down_[virtual_key]) return {};
            bool another_key_down = false;
            for (std::size_t key = 0; key < down_.size(); ++key) {
                if (down_[key]) {
                    another_key_down = true;
                    break;
                }
            }
            down_[virtual_key] = true;
            candidate = (left ? left_enabled_ : right_enabled_) &&
                        !another_key_down && !preexisting_chord;
            if (down_[left ? VK_RWIN : VK_LWIN]) cancel_candidates();
            if (left_chord_ || right_chord_ || any_consumed_key()) {
                candidate = false;
                (left ? left_chord_ : right_chord_) = true;
            }
            return {};
        }

        if (!down_[virtual_key]) return {};
        down_[virtual_key] = false;
        bool& chord = left ? left_chord_ : right_chord_;
        if (chord) {
            chord = false;
            candidate = false;
            return {false, true, true};
        }
        const bool invoke = candidate;
        candidate = false;
        return {invoke, invoke, invoke};
    }

    void mouse_button(bool = false) {
        cancel_candidates();
    }

    void reset() {
        down_.fill(false);
        consumed_.fill(false);
        cancel_candidates();
        left_chord_ = false;
        right_chord_ = false;
    }

    bool needs_release_mask(DWORD virtual_key) const {
        if (virtual_key == VK_LWIN) return left_chord_ && down_[VK_LWIN];
        if (virtual_key == VK_RWIN) return right_chord_ && down_[VK_RWIN];
        return false;
    }

private:
    bool any_win_down() const {
        return down_[VK_LWIN] || down_[VK_RWIN];
    }

    bool any_consumed_key() const {
        for (const bool consumed : consumed_) {
            if (consumed) return true;
        }
        return false;
    }

    bool modifier_down(DWORD generic, DWORD left, DWORD right) const {
        return down_[generic] || down_[left] || down_[right];
    }

    UINT active_modifiers(UINT live_modifiers) const {
        UINT result = MOD_WIN | (live_modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT));
        if (modifier_down(VK_CONTROL, VK_LCONTROL, VK_RCONTROL)) result |= MOD_CONTROL;
        if (modifier_down(VK_MENU, VK_LMENU, VK_RMENU)) result |= MOD_ALT;
        if (modifier_down(VK_SHIFT, VK_LSHIFT, VK_RSHIFT)) result |= MOD_SHIFT;
        return result;
    }

    bool matches_binding(DWORD virtual_key, UINT live_modifiers) const {
        const UINT current = active_modifiers(live_modifiers);
        constexpr UINT relevant = MOD_WIN | MOD_CONTROL | MOD_ALT | MOD_SHIFT;
        for (const auto& binding : bindings_) {
            if (binding.key == virtual_key && (binding.modifiers & relevant) == current) return true;
        }
        return false;
    }

    void mark_active_win_chord() {
        if (down_[VK_LWIN]) left_chord_ = true;
        if (down_[VK_RWIN]) right_chord_ = true;
    }

    void cancel_candidates() {
        left_candidate_ = false;
        right_candidate_ = false;
    }

    std::array<bool, 256> down_{};
    std::array<bool, 256> consumed_{};
    std::vector<Hotkey> bindings_;
    bool left_enabled_ = false;
    bool right_enabled_ = false;
    bool left_candidate_ = false;
    bool right_candidate_ = false;
    bool left_chord_ = false;
    bool right_chord_ = false;
};

} // namespace palette::detail
