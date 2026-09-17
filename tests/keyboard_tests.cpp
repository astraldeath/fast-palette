#include "key_state.hpp"
#include "keyboard.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void lone_win_release_invokes_once(DWORD win) {
    palette::detail::KeyState state(true, true);
    expect(!state.key(win, true).invoke, "Win down does not invoke");
    const auto release = state.key(win, false);
    expect(release.invoke && release.mask_release, "lone Win release invokes and is masked");
    expect(!state.key(win, false).invoke, "repeat release does not invoke twice");
}

void repeated_down_does_not_duplicate() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true);
    state.key(VK_LWIN, true);
    expect(state.key(VK_LWIN, false).invoke, "repeated Win down still invokes once");
    expect(!state.key(VK_LWIN, false).invoke, "repeated sequence cannot invoke twice");
}

void win_shortcut_cancels() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true);
    state.key('E', true);
    state.key('E', false);
    const auto release = state.key(VK_LWIN, false);
    expect(!release.invoke && !release.mask_release, "Win+E release passes through without invoke");
}

void modifier_before_win_cancels() {
    palette::detail::KeyState state(true, true);
    state.key(VK_CONTROL, true);
    state.key(VK_RWIN, true);
    const auto release = state.key(VK_RWIN, false);
    expect(!release.invoke && !release.mask_release, "modifier held before Win cancels bare tap");
    state.key(VK_CONTROL, false);
}

void dual_win_keys_cancel() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true);
    state.key(VK_RWIN, true);
    expect(!state.key(VK_LWIN, false).invoke, "first release in dual-Win chord does not invoke");
    expect(!state.key(VK_RWIN, false).invoke, "second release in dual-Win chord does not invoke");
}

void mouse_chord_cancels() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true);
    state.mouse_button();
    expect(!state.key(VK_LWIN, false).invoke, "mouse click while Win held cancels bare tap");

    state.key(VK_RWIN, true);
    state.mouse_button(true);
    expect(!state.key(VK_RWIN, false).invoke, "injected mouse chord also cancels a bare tap");
}

void injected_events_do_not_trigger_or_poison_state() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true, true);
    expect(!state.key(VK_LWIN, false, true).invoke, "injected Win sequence does not invoke");

    state.key(VK_LWIN, true);
    state.key('E', true, true);
    state.key('E', false, true);
    expect(!state.key(VK_LWIN, false).invoke, "an external injected chord cancels a real bare tap");
}

void configuration_and_reset_are_honored() {
    palette::detail::KeyState state(false, true);
    state.key(VK_LWIN, true);
    expect(!state.key(VK_LWIN, false).invoke, "disabled left Win does not invoke");
    state.key(VK_RWIN, true);
    state.reset();
    expect(!state.key(VK_RWIN, false).invoke, "reset prevents a stale Win release invocation");
    state.key(VK_RWIN, true);
    expect(state.key(VK_RWIN, false).invoke, "enabled right Win invokes after reset");
}

void startup_held_keys_block_bare_win() {
    palette::detail::KeyState state(true, true);
    state.seed_down(VK_LCONTROL);
    state.key(VK_LWIN, true);
    expect(!state.key(VK_LWIN, false).invoke,
           "a modifier held before hook startup blocks a bare-Win candidate");
    state.key(VK_LCONTROL, false);
    state.key(VK_LWIN, true);
    expect(state.key(VK_LWIN, false).invoke,
           "candidate tracking recovers after a seeded modifier is released");

    palette::detail::KeyState win_held(true, true);
    win_held.seed_down(VK_RWIN);
    win_held.key(VK_RWIN, true);
    expect(!win_held.key(VK_RWIN, false).invoke,
           "a Win key held before hook startup cannot create a candidate on repeat");
}

void live_physical_chord_blocks_win_down() {
    palette::detail::KeyState modifier(true, true);
    modifier.key(VK_LWIN, true, false, true);
    expect(!modifier.key(VK_LWIN, false).invoke,
           "live modifier state blocks Win when its down transition was not observed");

    palette::detail::KeyState mouse(true, true);
    mouse.key(VK_RWIN, true, false, true);
    expect(!mouse.key(VK_RWIN, false).invoke,
           "live mouse-button state blocks Win when its down transition was not observed");
}

void configured_win_chord_invokes_once_and_is_fully_consumed() {
    palette::detail::KeyState state(false, false, {{MOD_WIN, 'R'}});
    expect(!state.key(VK_LWIN, true).suppress, "configured chord preserves real Win down");

    const auto first_down = state.key('R', true);
    expect(first_down.invoke && first_down.suppress && !first_down.mask_release,
           "configured Win chord invokes and swallows its terminal down");
    const auto repeat = state.key('R', true);
    expect(!repeat.invoke && repeat.suppress, "configured Win chord repeat is swallowed once");
    const auto terminal_up = state.key('R', false);
    expect(!terminal_up.invoke && terminal_up.suppress,
           "configured Win chord terminal up is swallowed");
    const auto win_up = state.key(VK_LWIN, false);
    expect(!win_up.invoke && win_up.suppress && win_up.mask_release,
           "configured Win chord masks the eventual Win release without invoking twice");
}

void configured_win_chord_toggles_on_each_new_press() {
    palette::detail::KeyState state(false, false, {{MOD_WIN, 'R'}});
    state.key(VK_RWIN, true);
    expect(state.key('R', true).invoke, "first configured key press invokes");
    state.key('R', false);
    expect(state.key('R', true).invoke, "second configured key press invokes while Win stays held");
    state.key('R', false);
    expect(state.key(VK_RWIN, false).mask_release, "multi-press chord still masks Win release");
}

void consumed_terminal_stays_balanced_when_win_is_released_first() {
    palette::detail::KeyState state(false, false, {{MOD_WIN | MOD_CONTROL, 'R'}});
    state.key(VK_CONTROL, true);
    state.key(VK_LWIN, true);
    expect(state.key('R', true).invoke, "modified configured Win chord invokes");
    state.key(VK_CONTROL, false);

    const auto win_up = state.key(VK_LWIN, false);
    expect(win_up.suppress && win_up.mask_release,
           "Win release is masked even after another modifier is released");
    expect(state.key('R', true).suppress,
           "terminal repeat remains swallowed after modifier release");
    expect(state.key('R', false).suppress,
           "terminal up remains swallowed after modifier release");
}

void configured_chords_require_exact_modifiers_and_preserve_other_shortcuts() {
    palette::detail::KeyState state(false, false, {{MOD_WIN, 'R'}});
    state.key(VK_LWIN, true);
    expect(!state.key('E', true).suppress, "unconfigured Win chord down passes through");
    expect(!state.key('E', false).suppress, "unconfigured Win chord up passes through");
    expect(!state.key(VK_LWIN, false).mask_release,
           "unconfigured Win chord leaves its Win release untouched");

    state.key(VK_CONTROL, true);
    state.key(VK_LWIN, true);
    expect(!state.key('R', true).suppress, "extra Ctrl prevents a Win+R-only match");
    state.key('R', false);
    expect(!state.key(VK_LWIN, false).mask_release,
           "nonmatching modifier set does not consume Win release");
    state.key(VK_CONTROL, false);

    palette::detail::KeyState live(false, false, {{MOD_WIN | MOD_CONTROL, 'R'}});
    live.key(VK_RWIN, true, false, true, MOD_CONTROL);
    const auto matched = live.key('R', true, false, false, MOD_CONTROL);
    expect(matched.invoke && matched.suppress,
           "live startup-held modifier participates in exact chord matching");
}

void configured_chords_reject_injected_input_and_handle_seeded_keys() {
    palette::detail::KeyState injected(false, false, {{MOD_WIN, 'R'}});
    injected.key(VK_LWIN, true, true);
    expect(!injected.key('R', true, true).invoke, "fully injected Win chord cannot invoke");

    injected.key(VK_LWIN, true);
    expect(!injected.key('R', true, true).suppress,
           "injected terminal input is not swallowed as a configured chord");
    expect(!injected.key(VK_LWIN, false).invoke,
           "injected terminal input cancels a pending bare candidate");

    palette::detail::KeyState seeded(false, false, {{MOD_WIN, 'R'}});
    seeded.seed_down('R');
    seeded.key(VK_LWIN, true);
    const auto repeat = seeded.key('R', true);
    expect(!repeat.invoke && !repeat.suppress,
           "startup-held terminal repeat stays with its already-delivered down");
    expect(!seeded.key('R', false).suppress,
           "startup-held terminal up stays with its already-delivered down");
    expect(!seeded.key(VK_LWIN, false).mask_release,
           "startup-held terminal does not claim or mask a chord it did not start");

    palette::detail::KeyState terminal_first(false, false, {{MOD_WIN, 'R'}});
    expect(!terminal_first.key('R', true).suppress, "terminal down before Win passes through");
    terminal_first.key(VK_LWIN, true);
    expect(!terminal_first.key('R', true).suppress,
           "terminal repeat after Win stays with the earlier delivered down");
    expect(!terminal_first.key('R', false).suppress,
           "terminal up after Win stays with the earlier delivered down");
    expect(!terminal_first.key(VK_LWIN, false).mask_release,
           "terminal-first ordering never claims the Win release");
}

void owned_win_chords_are_visible_for_teardown_balancing() {
    palette::detail::KeyState state(false, false, {{MOD_WIN, 'R'}});
    state.key(VK_LWIN, true);
    state.key('R', true);
    expect(state.needs_release_mask(VK_LWIN),
           "teardown can detect an owned left-Win release");
    expect(!state.needs_release_mask(VK_RWIN),
           "teardown does not mask an unrelated right-Win release");

    state.key(VK_RWIN, true);
    expect(state.needs_release_mask(VK_LWIN) && state.needs_release_mask(VK_RWIN),
           "a second held Win key inherits active chord ownership");
    state.key(VK_LWIN, false);
    expect(!state.needs_release_mask(VK_LWIN) && state.needs_release_mask(VK_RWIN),
           "normal Win release clears only its own teardown ownership");
    state.reset();
    expect(!state.needs_release_mask(VK_RWIN), "reset clears teardown ownership");
}

void suspension_preserves_only_owned_transitions() {
    palette::detail::KeyState state(false, false, {{MOD_WIN, 'R'}});
    state.key(VK_LWIN, true);
    expect(state.key('R', true).invoke, "configured chord begins before suspension");
    state.configure(false, false, {});
    expect(state.key('R', true).suppress,
           "suspension continues swallowing an owned terminal repeat");
    expect(state.key('R', false).suppress,
           "suspension continues swallowing an owned terminal up");
    expect(state.key(VK_LWIN, false).mask_release,
           "suspension retains the owned Win-release mask");

    state.key(VK_LWIN, true);
    const auto disabled = state.key('R', true);
    expect(!disabled.invoke && !disabled.suppress,
           "new configured-looking chord passes while suspended");
    expect(!state.key('R', false).suppress, "unowned terminal up passes while suspended");
    expect(!state.key(VK_LWIN, false).mask_release,
           "unowned Win release passes while suspended");
}

void reconfiguration_preserves_ownership_and_replaces_future_bindings() {
    palette::detail::KeyState state(true, false, {{MOD_WIN, 'R'}});
    state.key(VK_LWIN, true);
    state.key('R', true);
    state.configure(false, false, {});
    state.configure(false, false, {{MOD_WIN, 'E'}});
    expect(state.key('R', true).suppress && state.key('R', false).suppress,
           "resume keeps draining the terminal key owned before suspension");
    expect(state.key(VK_LWIN, false).mask_release,
           "resume keeps the Win mask owned before suspension");

    state.key(VK_RWIN, true);
    expect(!state.key('R', true).suppress, "removed Win binding passes after resume");
    state.key('R', false);
    const auto replacement = state.key('E', true);
    expect(replacement.invoke && replacement.suppress,
           "replacement Win binding invokes after resume");
    state.key('E', false);
    state.key(VK_RWIN, false);
}

void suspension_cancels_partial_bare_tap_candidates() {
    palette::detail::KeyState state(true, true);
    state.key(VK_LWIN, true);
    state.configure(false, false, {});
    const auto release = state.key(VK_LWIN, false);
    expect(!release.invoke && !release.suppress && !release.mask_release,
           "suspension cancels a bare tap that was not yet owned");
}

void hook_lifecycle_is_idempotent() {
    palette::KeyboardHook hook(nullptr, WM_APP + 1);
    expect(hook.start(false, false), "disabled hook configuration starts without installation");
    expect(hook.suspend(), "suspending an inactive hook is idempotent");
    hook.stop();
    hook.stop();
    expect(hook.start(true, true), "low-level hooks install on their dedicated thread");
    hook.stop();
    expect(hook.start(true, false), "hook can restart after teardown");
    hook.stop();
    expect(hook.start(false, false, {{MOD_WIN, 'R'}}),
           "configured Win binding starts the hook while bare taps are disabled");
    expect(hook.suspend(), "active hook acknowledges suspension on its worker thread");
    expect(hook.start(false, false, {{MOD_WIN, 'R'}}),
           "suspended hook acknowledges in-place resume and reconfiguration");
    hook.stop();
}

} // namespace

int main() {
    lone_win_release_invokes_once(VK_LWIN);
    lone_win_release_invokes_once(VK_RWIN);
    repeated_down_does_not_duplicate();
    win_shortcut_cancels();
    modifier_before_win_cancels();
    dual_win_keys_cancel();
    mouse_chord_cancels();
    injected_events_do_not_trigger_or_poison_state();
    configuration_and_reset_are_honored();
    startup_held_keys_block_bare_win();
    live_physical_chord_blocks_win_down();
    configured_win_chord_invokes_once_and_is_fully_consumed();
    configured_win_chord_toggles_on_each_new_press();
    consumed_terminal_stays_balanced_when_win_is_released_first();
    configured_chords_require_exact_modifiers_and_preserve_other_shortcuts();
    configured_chords_reject_injected_input_and_handle_seeded_keys();
    owned_win_chords_are_visible_for_teardown_balancing();
    suspension_preserves_only_owned_transitions();
    reconfiguration_preserves_ownership_and_replaces_future_bindings();
    suspension_cancels_partial_bare_tap_candidates();
    hook_lifecycle_is_idempotent();
    if (failures != 0) {
        std::cerr << failures << " keyboard test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "keyboard tests passed\n";
    return EXIT_SUCCESS;
}
