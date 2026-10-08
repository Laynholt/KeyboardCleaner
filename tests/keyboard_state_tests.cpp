#include "KeyboardState.h"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(1);
    }
}

int main() {
    KeyboardState state;
    Check(!state.Handle('A', true, 0).blocked, "normal typing passes through");
    state.Start();
    Check(state.Handle('A', true, 0).blocked, "cleaning swallows key down");
    state.Handle('A', true, 10);
    Check(state.presses == 1, "auto repeat is not another press");
    Check(state.Handle('A', false, 20).blocked, "cleaning swallows key up");
    Check(!state.down['A'], "key release clears highlight");
    Check(state.Handle(300, true, 30).blocked, "unknown keys are still blocked");
    state.Handle(VK_LCONTROL, true, 100);
    state.Handle(VK_RSHIFT, true, 200);
    Check(!state.Tick(1699), "unlock requires the full hold interval");
    state.Handle(VK_RSHIFT, false, 1700);
    Check(!state.Tick(2000), "releasing either key cancels unlock");
    state.Handle(VK_RSHIFT, true, 2100);
    Check(state.Tick(3600), "opposite-side chord exits after 1.5 seconds");
    Check(state.mode == KeyboardState::Mode::Draining, "held keys drain before unhook");
    Check(state.Handle(VK_LCONTROL, true, 3610).blocked, "held key repeats cannot leak on exit");
    Check(!state.Handle('B', true, 3620).blocked, "new keys pass during drain");
    Check(state.Handle(VK_LCONTROL, false, 3630).blocked, "suppressed modifier release stays suppressed");
    state.Handle(VK_RSHIFT, false, 3640);
    Check(state.mode == KeyboardState::Mode::Idle, "last release finishes drain");
    Check(!state.Handle('A', true, 3650).blocked, "typing resumes after unlock");
    state.Start();
    state.Stop();
    Check(state.mode == KeyboardState::Mode::Idle, "mouse exit with no held keys is immediate");
    state.Start();
    state.Handle(VK_RCONTROL, true, 10);
    state.Handle(VK_LSHIFT, true, 20);
    Check(!state.Tick(2000), "wrong-side modifiers do not unlock");
    Check(NormalizeKey(VK_SHIFT, 0x36, false) == VK_RSHIFT, "right shift normalization");
    Check(NormalizeKey(VK_CONTROL, 0x1d, true) == VK_RCONTROL, "right control normalization");
    Check(NormalizeKey(VK_END, 0x4f, false) == VK_NUMPAD1, "numpad with Num Lock off");
    Check(NormalizeKey(VK_END, 0x4f, true) == VK_END, "navigation cluster stays separate");
    Check(NormalizeKey(VK_RETURN, 0x1c, true) == NumpadEnter, "numpad enter stays separate");
    std::cout << "Keyboard state checks passed\n";
}
