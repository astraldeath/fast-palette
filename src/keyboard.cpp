#include "keyboard.hpp"
#include "key_state.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace palette {

namespace {

constexpr ULONG_PTR kInjectionMarker = sizeof(ULONG_PTR) == 8
    ? static_cast<ULONG_PTR>(0x4650414C45545445ull)
    : static_cast<ULONG_PTR>(0x4650414Cul);
constexpr WORD kMaskVirtualKey = 0xE8; // Unassigned by the Win32 virtual-key table.
constexpr UINT kConfigureMessage = WM_APP + 0x05E1;

bool currently_down(int virtual_key) {
    return GetAsyncKeyState(virtual_key) < 0;
}

bool is_mouse_button(int virtual_key) {
    return virtual_key == VK_LBUTTON || virtual_key == VK_RBUTTON ||
           virtual_key == VK_MBUTTON || virtual_key == VK_XBUTTON1 ||
           virtual_key == VK_XBUTTON2;
}

bool seedable_keyboard_key(int virtual_key) {
    if (virtual_key == VK_SHIFT || virtual_key == VK_CONTROL || virtual_key == VK_MENU) return false;
    return virtual_key >= VK_BACK && !is_mouse_button(virtual_key);
}

UINT physical_modifier_mask() {
    UINT modifiers = 0;
    if (currently_down(VK_SHIFT)) modifiers |= MOD_SHIFT;
    if (currently_down(VK_CONTROL)) modifiers |= MOD_CONTROL;
    if (currently_down(VK_MENU)) modifiers |= MOD_ALT;
    return modifiers;
}

bool physical_mouse_held() {
    constexpr int keys[] = {
        VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
    };
    for (const int key : keys) {
        if (currently_down(key)) return true;
    }
    return false;
}

} // namespace

struct KeyboardHook::Impl {
    struct Configuration {
        bool left = false;
        bool right = false;
        std::vector<Hotkey> bindings;

        bool enabled() const { return left || right || !bindings.empty(); }
    };

    Impl(HWND destination, UINT notification) : target(destination), message(notification) {}

    static Configuration make_configuration(bool left, bool right,
                                            const std::vector<Hotkey>& bindings) {
        Configuration configuration{left, right, {}};
        for (const auto& binding : bindings) {
            if ((binding.modifiers & MOD_WIN) != 0 && binding.key < 256 &&
                binding.key != VK_LWIN && binding.key != VK_RWIN) {
                configuration.bindings.push_back(binding);
            }
        }
        return configuration;
    }

    bool start(bool left, bool right, const std::vector<Hotkey>& bindings) {
        Configuration configuration = make_configuration(left, right, bindings);
        {
            std::unique_lock lock(mutex);
            if (thread_id != 0 && startup_success) {
                return apply_configuration_locked(std::move(configuration), lock);
            }
        }

        stop();
        if (!configuration.enabled()) return true;
        {
            std::lock_guard lock(mutex);
            startup_complete = false;
            startup_success = false;
            worker = std::thread([this, configuration = std::move(configuration)]() mutable {
                run(std::move(configuration));
            });
        }
        std::unique_lock lock(mutex);
        ready.wait(lock, [this] { return startup_complete; });
        const bool success = startup_success;
        lock.unlock();
        if (!success && worker.joinable()) worker.join();
        return success;
    }

    bool suspend() {
        Configuration disabled;
        std::unique_lock lock(mutex);
        if (thread_id == 0 || !startup_success) return true;
        return apply_configuration_locked(std::move(disabled), lock);
    }

    bool apply_configuration_locked(Configuration configuration,
                                    std::unique_lock<std::mutex>& lock) {
        if (thread_id == 0 || !startup_success) return false;
        const std::uint64_t request = ++requested_configuration;
        const DWORD id = thread_id;
        pending_configurations.emplace_back(request, std::move(configuration));
        if (!PostThreadMessageW(id, kConfigureMessage, static_cast<WPARAM>(request), 0)) {
            for (auto entry = pending_configurations.begin(); entry != pending_configurations.end(); ++entry) {
                if (entry->first == request) {
                    pending_configurations.erase(entry);
                    break;
                }
            }
            return false;
        }
        const bool acknowledged = configuration_ready.wait_for(
            lock, std::chrono::seconds(2),
            [this, request] { return applied_configuration >= request || thread_id == 0; });
        if (acknowledged && applied_configuration >= request) return true;
        for (auto entry = pending_configurations.begin(); entry != pending_configurations.end(); ++entry) {
            if (entry->first == request) {
                pending_configurations.erase(entry);
                break;
            }
        }
        return false;
    }

    void stop() {
        DWORD id = 0;
        {
            std::lock_guard lock(mutex);
            id = thread_id;
        }
        if (id != 0) PostThreadMessageW(id, WM_QUIT, 0, 0);
        if (worker.joinable()) worker.join();
        std::lock_guard lock(mutex);
        thread_id = 0;
        startup_complete = false;
        startup_success = false;
        pending_configurations.clear();
    }

    void signal_startup(bool success) {
        {
            std::lock_guard lock(mutex);
            startup_success = success;
            startup_complete = true;
        }
        ready.notify_one();
    }

    void run(Configuration configuration) {
        MSG message_record{};
        PeekMessageW(&message_record, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        {
            std::lock_guard lock(mutex);
            thread_id = GetCurrentThreadId();
        }
        state.emplace(configuration.left, configuration.right, std::move(configuration.bindings));
        active = this;
        keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_callback, GetModuleHandleW(nullptr), 0);
        mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_callback, GetModuleHandleW(nullptr), 0);
        if (!keyboard_hook || !mouse_hook) {
            if (keyboard_hook) UnhookWindowsHookEx(keyboard_hook);
            if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
            keyboard_hook = nullptr;
            mouse_hook = nullptr;
            active = nullptr;
            state.reset();
            {
                std::lock_guard lock(mutex);
                thread_id = 0;
            }
            signal_startup(false);
            return;
        }
        for (int virtual_key = 0; virtual_key < 256; ++virtual_key) {
            if (seedable_keyboard_key(virtual_key) && currently_down(virtual_key)) {
                state->seed_down(static_cast<DWORD>(virtual_key));
            }
        }
        signal_startup(true);

        while (GetMessageW(&message_record, nullptr, 0, 0) > 0) {
            if (message_record.message == kConfigureMessage) {
                const auto request = static_cast<std::uint64_t>(message_record.wParam);
                bool applied = false;
                {
                    std::lock_guard lock(mutex);
                    for (auto entry = pending_configurations.begin();
                         entry != pending_configurations.end(); ++entry) {
                        if (entry->first == request) {
                            Configuration next = std::move(entry->second);
                            pending_configurations.erase(entry);
                            state->configure(next.left, next.right, std::move(next.bindings));
                            applied_configuration = request;
                            applied = true;
                            break;
                        }
                    }
                }
                if (applied) configuration_ready.notify_all();
                continue;
            }
            TranslateMessage(&message_record);
            DispatchMessageW(&message_record);
        }

        if (state->needs_release_mask(VK_LWIN)) inject_masked_release(VK_LWIN);
        if (state->needs_release_mask(VK_RWIN)) inject_masked_release(VK_RWIN);
        UnhookWindowsHookEx(mouse_hook);
        UnhookWindowsHookEx(keyboard_hook);
        mouse_hook = nullptr;
        keyboard_hook = nullptr;
        active = nullptr;
        state.reset();
        std::lock_guard lock(mutex);
        thread_id = 0;
        configuration_ready.notify_all();
    }

    bool inject_masked_release(DWORD win_key) const {
        INPUT inputs[3]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = kMaskVirtualKey;
        inputs[0].ki.dwExtraInfo = kInjectionMarker;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = kMaskVirtualKey;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[1].ki.dwExtraInfo = kInjectionMarker;
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = static_cast<WORD>(win_key);
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        inputs[2].ki.dwExtraInfo = kInjectionMarker;
        if (SendInput(3, inputs, sizeof(INPUT)) == 3) return true;

        INPUT cleanup[2]{};
        cleanup[0] = inputs[1]; // A partial prefix can leave only the mask key down.
        cleanup[1] = inputs[2]; // Balance Win if its injected release was not inserted.
        SendInput(2, cleanup, sizeof(INPUT));
        return false;
    }

    static LRESULT CALLBACK keyboard_callback(int code, WPARAM w_param, LPARAM l_param) {
        if (code == HC_ACTION && active && active->state) {
            const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
            const bool down = w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN;
            const bool up = w_param == WM_KEYUP || w_param == WM_SYSKEYUP;
            if ((down || up) && event->dwExtraInfo != kInjectionMarker) {
                const bool injected = (event->flags & LLKHF_INJECTED) != 0;
                const bool win_down = down &&
                    (event->vkCode == VK_LWIN || event->vkCode == VK_RWIN);
                const UINT live_modifiers = !injected && down ? physical_modifier_mask() : 0;
                const bool preexisting_chord = !injected && win_down &&
                    (live_modifiers != 0 || physical_mouse_held());
                const auto decision = active->state->key(event->vkCode, down, injected,
                                                         preexisting_chord, live_modifiers);
                if (decision.mask_release) {
                    if (active->inject_masked_release(event->vkCode)) {
                        if (decision.invoke) PostMessageW(active->target, active->message, 0, 0);
                        return 1;
                    }
                    return CallNextHookEx(nullptr, code, w_param, l_param);
                }
                if (decision.invoke) PostMessageW(active->target, active->message, 0, 0);
                if (decision.suppress) return 1;
            }
        }
        return CallNextHookEx(nullptr, code, w_param, l_param);
    }

    static LRESULT CALLBACK mouse_callback(int code, WPARAM w_param, LPARAM l_param) {
        if (code == HC_ACTION && active && active->state) {
            const bool button_down = w_param == WM_LBUTTONDOWN || w_param == WM_RBUTTONDOWN ||
                                     w_param == WM_MBUTTONDOWN || w_param == WM_XBUTTONDOWN ||
                                     w_param == WM_MOUSEWHEEL || w_param == WM_MOUSEHWHEEL;
            if (button_down) {
                const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
                const bool injected = (event->flags & LLMHF_INJECTED) != 0;
                active->state->mouse_button(injected);
            }
        }
        return CallNextHookEx(nullptr, code, w_param, l_param);
    }

    HWND target = nullptr;
    UINT message = 0;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable configuration_ready;
    bool startup_complete = false;
    bool startup_success = false;
    DWORD thread_id = 0;
    HHOOK keyboard_hook = nullptr;
    HHOOK mouse_hook = nullptr;
    std::uint64_t requested_configuration = 0;
    std::uint64_t applied_configuration = 0;
    std::deque<std::pair<std::uint64_t, Configuration>> pending_configurations;
    std::optional<detail::KeyState> state;
    static thread_local Impl* active;
};

thread_local KeyboardHook::Impl* KeyboardHook::Impl::active = nullptr;

KeyboardHook::KeyboardHook(HWND target, UINT message) : impl_(std::make_unique<Impl>(target, message)) {}
KeyboardHook::~KeyboardHook() { stop(); }
bool KeyboardHook::start(bool left, bool right, const std::vector<Hotkey>& bindings) {
    return impl_->start(left, right, bindings);
}
bool KeyboardHook::suspend() { return impl_->suspend(); }
void KeyboardHook::stop() { impl_->stop(); }

} // namespace palette
