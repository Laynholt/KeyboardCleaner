#include <windows.h>
LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
#ifdef FIXTURE_EXIT_IMMEDIATELY
    return 1;
#endif
#ifdef FIXTURE_OLD
    constexpr auto title = L"KeyboardCleaner.OldFixture";
#else
    constexpr auto title = L"KeyboardCleaner.NewFixture";
#endif
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = FixtureProc; cls.lpszClassName = L"KeyboardCleaner.UpdateFixture";
    if (!RegisterClassW(&cls)) return 1;
    const HWND window = CreateWindowW(cls.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                      40, 40, 300, 120, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return 0;
}
