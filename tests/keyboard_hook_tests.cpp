#include "KeyboardHook.h"
#include <cstdlib>
#include <iostream>

namespace {
unsigned received{};
LRESULT CALLBACK Receiver(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
        ++received;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
void Pump(DWORD duration) {
    const auto end = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < end);
}
void Key(WORD key, bool down, WORD scan = 0, bool extended = false) {
    INPUT input{}; input.type = INPUT_KEYBOARD;
    input.ki.wVk = key; input.ki.wScan = scan;
    input.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    Check(SendInput(1, &input, sizeof(input)) == 1, "SendInput reaches the test desktop");
    Pump(30);
}
void Press(WORD key) { Key(key, true); Key(key, false); }
}

int main() {
    const HWND previous = GetForegroundWindow();
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = Receiver; cls.lpszClassName = L"KeyboardCleaner.HookTest";
    Check(RegisterClassW(&cls) != 0, "register receiver");
    const HWND window = CreateWindowW(cls.lpszClassName, L"Keyboard Cleaner input test", WS_OVERLAPPEDWINDOW,
                                     50, 50, 340, 140, nullptr, nullptr, cls.hInstance, nullptr);
    Check(window != nullptr, "create receiver");
    ShowWindow(window, SW_SHOW); SetForegroundWindow(window); SetFocus(window); Pump(100);
    if (GetForegroundWindow() != window) {
        const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD testThread = GetCurrentThreadId();
        if (foregroundThread != testThread && AttachThreadInput(testThread, foregroundThread, TRUE)) {
            BringWindowToTop(window); SetForegroundWindow(window); SetFocus(window);
            AttachThreadInput(testThread, foregroundThread, FALSE);
            Pump(100);
        }
    }
    Check(GetForegroundWindow() == window, "only send input to our foreground receiver");
    Press(VK_F24);
    Check(received == 2, "baseline key down and up reach the receiver");

    KeyboardHook hook;
    Check(hook.Start(), "install real WH_KEYBOARD_LL hook");
    Key('A', true); Key('A', true); Key('A', false);
    Press(VK_F24); Press(VK_LWIN); Press(VK_CAPITAL); Press(VK_LMENU);
    Check(received == 2, "letters, repeats, Win, Caps Lock and Alt are swallowed");
    Check(hook.presses == 5, "press counter counts distinct downs only");
    Check(!hook.down['A'] && !hook.down[VK_LWIN], "release clears real hook snapshot");
    Key('A', true);
    hook.Stop(); Pump(50);
    Check(hook.mode == KeyboardState::Mode::Draining, "mouse stop drains real held keys");
    Key('A', true);
    Press('B');
    Check(received == 4, "new keys pass while held key repeats stay blocked");
    Key('A', false); Pump(75);
    Check(!hook.running, "last held release removes hook");
    Press(VK_F24);
    Check(received == 6, "input is restored after mouse stop");

    Check(hook.Start(), "hook can start another session");
    Key(VK_LCONTROL, true, 0x1d);
    Key(VK_RSHIFT, true, 0x36);
    Pump(1600);
    Check(hook.mode == KeyboardState::Mode::Draining, "real modifier chord unlocks on a timer without repeats");
    Key(VK_RSHIFT, false, 0x36); Key(VK_LCONTROL, false, 0x1d); Pump(75);
    Check(!hook.running, "shortcut key releases remove hook");
    Check(received == 6, "unlock chord never reaches receiver");
    Press(VK_F24);
    Check(received == 8, "typing resumes after shortcut");

    Check(hook.Start(), "start before close");
    Key('A', true);
    hook.Abort();
    Key('A', false);
    Press(VK_F24);
    Check(received == 11, "close removes hook even with held keys");
    Check(hook.error == 0, "hook thread reported no errors");
    DestroyWindow(window);
    if (previous) SetForegroundWindow(previous);
    std::cout << "Real Win32 keyboard hook checks passed\n";
}
