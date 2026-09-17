#include "shortcut_capture.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using palette::shortcut_capture::State;

    {
        State state;
        state.begin();
        require(state.event(VK_LWIN, true).swallow, "Win down is owned");
        const auto recorded = state.event('R', true);
        require(recorded.swallow && state.completed && state.key == 'R' && state.modifiers == MOD_WIN,
            "Win+R is recorded on the non-modifier down");
        require(state.event('R', false).swallow, "terminal key release is swallowed");
        const auto finished = state.event(VK_LWIN, false);
        require(finished.swallow && finished.finished, "recording finishes after all owned keys release");
    }

    {
        State state;
        state.begin();
        require(state.event(VK_ESCAPE, true).swallow && state.cancelled, "Escape cancels and is swallowed");
        const auto finished = state.event(VK_ESCAPE, false);
        require(finished.swallow && finished.finished && !state.completed, "Escape release drains cancellation");
    }

    {
        std::array<bool, 256> held{};
        held[VK_LMENU] = true;
        State state;
        state.begin(held);
        require(!state.event(VK_LMENU, true).swallow, "pre-held repeat is passed");
        require(!state.event('R', true).swallow && !state.completed,
            "terminal key is not recorded while a pre-held modifier remains down");
        require(!state.event(VK_LMENU, false).swallow, "pre-held release is passed");
    }

    {
        std::array<bool, 256> held{};
        held[VK_LBUTTON] = true;
        State state;
        state.begin(held);
        require(state.event(VK_LWIN, true).swallow, "pre-held mouse button does not block keyboard capture");
        require(state.event('R', true).swallow && state.completed && state.modifiers == MOD_WIN,
            "Win+R records after mouse activation");
    }

    {
        State state;
        state.begin();
        require(state.event(VK_LCONTROL, true).swallow, "owned modifier is swallowed");
        require(!state.request_stop(), "stop waits for an owned release");
        require(!state.event('X', true).swallow, "new keydown passes while draining");
        require(!state.event('X', false).swallow, "unowned release passes while draining");
        const auto finished = state.event(VK_LCONTROL, false);
        require(finished.swallow && finished.finished, "owned release completes drain");
    }

    std::cout << "shortcut capture tests passed\n";
    return 0;
}
