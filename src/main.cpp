#include "KeyboardHook.h"
#include "KeyboardLayout.h"
#include "Update.h"
#include "AppVersion.h"
#include <wcw/Controls.h>
#include <wcw/Runtime.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr wchar_t WindowClass[] = L"KeyboardCleaner.Main";
constexpr int CleanId = 101, ThemeId = 102, MinimizeId = 103, CloseId = 104, UpdateId = 105;
constexpr float DesignWidth = 1100, DesignHeight = 790;
using Gdiplus::RectF;

LRESULT CALLBACK SmoothButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR, DWORD_PTR) {
    // A binary window region cuts off the widget's antialiased outer edge.
    RECT region{};
    if (message == WM_PAINT && GetWindowRgnBox(window, &region) != ERROR)
        SetWindowRgn(window, nullptr, FALSE);
    return DefSubclassProc(window, message, wParam, lParam);
}

wcw::Color Rgb(BYTE r, BYTE g, BYTE b) { return wcw::Color::FromRgb(r, g, b); }
Gdiplus::Color Color(wcw::Color value) { return {value.a, value.r, value.g, value.b}; }

void Rounded(Gdiplus::Graphics& graphics, RectF rect, float radius, wcw::Color fill,
             wcw::Color border = {}, bool outlined = false) {
    Gdiplus::GraphicsPath path;
    const float diameter = std::min(radius * 2, std::min(rect.Width, rect.Height));
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Color(fill));
    graphics.FillPath(&brush, &path);
    if (outlined) { Gdiplus::Pen pen(Color(border), 1); graphics.DrawPath(&pen, &path); }
}

void Text(Gdiplus::Graphics& graphics, const std::wstring& text, RectF rect, float size,
          wcw::Color color, bool bold = false, bool centered = false) {
    Gdiplus::Font font(L"Segoe UI", size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Color(color));
    Gdiplus::StringFormat format;
    format.SetAlignment(centered ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

std::wstring Duration(std::uint64_t milliseconds) {
    const auto seconds = milliseconds / 1000;
    wchar_t text[32]{};
    swprintf_s(text, L"%02llu:%02llu", seconds / 60, seconds % 60);
    return text;
}

struct App {
    HWND window{}, clean{}, theme{}, minimize{}, close{}, updateButton{};
    KeyboardHook hook;
    update::Updater updater;
    update::Updater::Status shownUpdate{update::Updater::Status::None};
    unsigned shownProgress{};
    std::vector<KeyCap> keys{MakeKeyboard()};
    bool light{}, session{}, stopping{};
    KeyboardState::Mode shownMode{KeyboardState::Mode::Idle};
    std::uint64_t started{}, elapsed{};
    float scale{1}, dpiScale{1}, offsetX{}, offsetY{};
    std::wstring notice;

    void Geometry() {
        RECT rect{}; GetClientRect(window, &rect);
        dpiScale = GetDpiForWindow(window) / 96.0f;
        const float width = rect.right / dpiScale, height = rect.bottom / dpiScale;
        scale = std::max(.1f, std::min(width / DesignWidth, height / DesignHeight));
        offsetX = (width - DesignWidth * scale) / 2;
        offsetY = (height - DesignHeight * scale) / 2;
    }
    void Move(HWND child, RectF rect) const {
        auto px = [&](float value) { return static_cast<int>(std::lround(value * dpiScale)); };
        MoveWindow(child, px(offsetX + rect.X * scale), px(offsetY + rect.Y * scale),
                   px(rect.Width * scale), px(rect.Height * scale), TRUE);
    }
    void Layout() {
        Geometry();
        const bool hasUpdate = shownUpdate == update::Updater::Status::Available ||
                               shownUpdate == update::Updater::Status::Downloading;
        Move(theme, {hasUpdate ? 778.f : 822.f, 14, 146, 34});
        Move(updateButton, {932, 14, 36, 34});
        Move(minimize, {976, 14, 36, 34});
        Move(close, {1020, 14, 40, 34});
        Move(clean, {36, 664, 282, 52});
        StyleButtons();
        InvalidateRect(window, nullptr, FALSE);
    }
    void StyleButtons() {
        auto palette = wcw::GetTheme().palette;
        wcw::StyleOverride captionStyle;
        captionStyle.background = palette.window;
        captionStyle.border = palette.window;
        captionStyle.hover = palette.hover;
        captionStyle.font = wcw::FontSpec{L"Segoe UI", 13 * scale};
        captionStyle.cornerRadiusDip = 8 * scale;
        for (HWND button : {theme, minimize, close, updateButton}) if (button) wcw::SetStyleOverride(button, captionStyle);
        captionStyle.foreground = light ? palette.accent : Rgb(194, 178, 255);
        captionStyle.font = wcw::FontSpec{L"Segoe UI", 20 * scale};
        captionStyle.paddingXDip = 4 * scale;
        if (updateButton) wcw::SetStyleOverride(updateButton, captionStyle);
        wcw::StyleOverride primary;
        primary.background = session ? palette.input : palette.accent;
        primary.foreground = session ? palette.text : Rgb(255, 255, 255);
        primary.hover = session ? palette.hover : palette.accentHover;
        primary.pressed = session ? palette.pressed : palette.accentPressed;
        primary.border = session ? palette.border : palette.accent;
        primary.font = wcw::FontSpec{L"Segoe UI", 16 * scale, 600};
        primary.cornerRadiusDip = 12 * scale;
        if (clean) wcw::SetStyleOverride(clean, primary);
    }
    void ApplyTheme() {
        auto value = light ? wcw::LightTheme() : wcw::DarkTheme();
        auto& p = value.palette;
        p.window = light ? Rgb(244, 245, 250) : Rgb(16, 19, 27);
        p.panel = light ? Rgb(255, 255, 255) : Rgb(23, 27, 37);
        p.input = light ? Rgb(246, 247, 251) : Rgb(33, 38, 51);
        p.text = light ? Rgb(29, 34, 50) : Rgb(234, 237, 246);
        p.mutedText = light ? Rgb(106, 114, 135) : Rgb(139, 149, 172);
        p.border = light ? Rgb(222, 226, 236) : Rgb(46, 53, 70);
        p.hover = light ? Rgb(234, 237, 246) : Rgb(43, 50, 68);
        p.pressed = light ? Rgb(221, 224, 237) : Rgb(51, 59, 79);
        p.accent = Rgb(121, 100, 223);
        p.accentHover = Rgb(138, 118, 237);
        p.accentPressed = Rgb(106, 84, 202);
        p.focus = p.accent;
        p.success = light ? Rgb(27, 132, 99) : Rgb(142, 225, 195);
        value.metrics.cornerRadiusDip = 10;
        wcw::SetTheme(value);
        if (theme) SetWindowTextW(theme, light ? L"☾  Тёмная тема" : L"☼  Светлая тема");
        const BOOL dark = !light;
        DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        StyleButtons();
        InvalidateRect(window, nullptr, FALSE);
    }
    HWND Button(int id, const wchar_t* label, const wchar_t* accessible) {
        wcw::ButtonOptions options;
        options.parent = window; options.id = id; options.text = label;
        options.accessibleName = accessible;
        options.bounds = {0, 0, 100, 36}; options.style = WS_VISIBLE;
        const auto button = wcw::CreateButton(options);
        if (button && id == CleanId && !SetWindowSubclass(button, SmoothButtonProc, 1, 0)) {
            DestroyWindow(button);
            return nullptr;
        }
        return button;
    }
    bool CreateControls() {
        clean = Button(CleanId, L"Начать чистку", L"Начать или завершить режим чистки клавиатуры");
        theme = Button(ThemeId, L"☼  Светлая тема", L"Переключить светлую и тёмную темы");
        minimize = Button(MinimizeId, L"−", L"Свернуть окно");
        close = Button(CloseId, L"×", L"Закрыть приложение и вернуть ввод с клавиатуры");
        updateButton = Button(UpdateId, L"↓", L"Скачать и установить доступное обновление");
        if (!clean || !theme || !minimize || !close || !updateButton) return false;
        ShowWindow(updateButton, SW_HIDE);
        ApplyTheme(); Layout();
        SetFocus(clean);
        return true;
    }
    void RefreshState() {
        const auto mode = hook.mode.load();
        if (session && !hook.running) {
            elapsed = GetTickCount64() - started;
            session = stopping = false;
            hook.Abort();
            notice = hook.error ? L"Перехват остановлен. Клавиатура снова работает." : L"Готово. Можно снова печатать.";
            SetWindowTextW(clean, L"Начать чистку");
            EnableWindow(clean, TRUE); EnableWindow(minimize, TRUE);
            SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            StyleButtons(); InvalidateRect(window, nullptr, FALSE);
        }
        if (session && mode != shownMode) {
            if (mode == KeyboardState::Mode::Draining) {
                stopping = true;
                SetWindowTextW(clean, L"Отпустите клавиши…"); EnableWindow(clean, FALSE);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        shownMode = mode;
    }
    void ToggleCleaning() {
        if (updater.State() == update::Updater::Status::Downloading ||
            updater.State() == update::Updater::Status::Ready) return;
        if (session) {
            if (!stopping) { stopping = true; hook.Stop(); RefreshState(); }
            return;
        }
        // Starting with a modifier already down could suppress its release in another application.
        for (int key = VK_BACK; key < 255; ++key) {
            if (GetAsyncKeyState(key) & 0x8000) {
                notice = L"Сначала отпустите все клавиши, затем нажмите «Начать чистку».";
                InvalidateRect(window, nullptr, FALSE); return;
            }
        }
        notice.clear();
        if (!hook.Start()) {
            notice = L"Не удалось включить перехват (код " + std::to_wstring(hook.error.load()) + L"). Попробуйте ещё раз.";
            InvalidateRect(window, nullptr, FALSE); return;
        }
        started = GetTickCount64(); elapsed = 0; session = true; stopping = false;
        shownMode = KeyboardState::Mode::Cleaning;
        SetWindowTextW(clean, L"Завершить чистку");
        EnableWindow(minimize, FALSE);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        StyleButtons(); InvalidateRect(window, nullptr, FALSE);
    }
    void CancelSession(const wchar_t* reason) {
        if (!session) return;
        hook.Abort(); RefreshState(); notice = reason;
        InvalidateRect(window, nullptr, FALSE);
    }
    void DownloadUpdate() {
        if (updater.State() != update::Updater::Status::Available) return;
        CancelSession(L"");
        updater.Download();
        PollUpdate();
    }
    void PollUpdate() {
        using Status = update::Updater::Status;
        const auto state = updater.State();
        if (state == Status::Ready) {
            try {
                updater.Install();
                SendMessageW(window, WM_CLOSE, 0, 0);
                return;
            } catch (...) { /* A manual update error is shown below; the old EXE is untouched. */ }
        }
        const auto current = updater.State();
        if (current != shownUpdate) {
            shownUpdate = current;
            const bool visible = current == Status::Available || current == Status::Downloading;
            ShowWindow(updateButton, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            EnableWindow(updateButton, current == Status::Available);
            EnableWindow(clean, current != Status::Downloading && !stopping);
            if (current == Status::Downloading) {
                shownProgress = 0;
                notice = L"Загрузка обновления: 0%. После проверки программа перезапустится.";
            }
            if (current == Status::Available) {
                wcw::TooltipOptions tip;
                tip.text = L"Доступна версия " + std::wstring(updater.Version().begin(), updater.Version().end()) +
                           L". Нажмите, чтобы скачать и установить.";
                tip.maxWidthDip = 360;
                wcw::AttachTooltip(updateButton, tip);
            }
            if (updater.failed) notice = L"Не удалось скачать или установить обновление. Текущая версия сохранена.";
            Layout();
        }
        if (current == Status::Downloading) {
            const unsigned progress = updater.progress;
            if (progress != shownProgress || notice.empty()) {
                shownProgress = progress;
                notice = L"Загрузка обновления: " + std::to_wstring(progress) + L"%. После проверки программа перезапустится.";
                InvalidateRect(window, nullptr, FALSE);
            }
        }
    }

    void PaintContent(Gdiplus::Graphics& g) {
        const auto p = wcw::GetTheme().palette;
        const bool draining = hook.mode == KeyboardState::Mode::Draining;
        const auto accentSoft = light ? Rgb(239, 235, 255) : Rgb(40, 34, 65);
        const auto greenSoft = light ? Rgb(225, 245, 237) : Rgb(27, 49, 45);
        const auto keyActive = Rgb(142, 225, 195);
        const auto line = light ? Rgb(228, 231, 240) : Rgb(37, 43, 58);

        Rounded(g, {36, 18, 30, 28}, 8, accentSoft);
        Gdiplus::Pen iconPen(Color(light ? p.accent : Rgb(181, 168, 255)), 1.5f);
        g.DrawRectangle(&iconPen, 42.f, 25.f, 18.f, 13.f);
        for (int x = 0; x < 4; ++x) g.DrawLine(&iconPen, 45.f + x * 4, 29.f, 45.f + x * 4, 31.f);
        g.DrawLine(&iconPen, 46.f, 34.f, 56.f, 34.f);
        Text(g, L"Keyboard Cleaner", {78, 14, 240, 36}, 16, p.text, true);
        Gdiplus::Pen divider(Color(line)); g.DrawLine(&divider, 36.f, 62.f, 1064.f, 62.f);

        const wcw::Color badgeText = session ? p.success : (light ? p.accent : Rgb(181, 168, 255));
        Rounded(g, {36, 88, session ? 200.f : 183.f, 28}, 14, session ? greenSoft : accentSoft);
        Gdiplus::SolidBrush dot(Color(badgeText)); g.FillEllipse(&dot, 49.f, 99.f, 6.f, 6.f);
        Text(g, draining ? L"ЗАВЕРШАЕМ ЧИСТКУ" : session ? L"КЛАВИАТУРА ЗАЩИЩЕНА" : L"ГОТОВО К ЧИСТКЕ",
             {63, 88, 165, 28}, 10.5f, badgeText, true);
        Text(g, draining ? L"Отпустите нажатые клавиши." : session ? L"Можно спокойно чистить." : L"Время для чистоты.",
             {34, 125, 930, 52}, 36, p.text, true);
        Text(g, session ? L"Нажатия перехватываются здесь. Мышь продолжает работать." :
             L"Один клик — и клавиатура не помешает уборке. Мышь останется доступна.",
             {36, 183, 955, 26}, 15, p.mutedText);

        Rounded(g, {966, 106, 88, 88}, 26, session ? greenSoft : accentSoft);
        Gdiplus::Pen symbol(Color(session ? p.success : (light ? p.accent : Rgb(181, 168, 255))), 2.5f);
        Gdiplus::PointF shield[]{{1010, 124}, {1030, 132}, {1027, 156}, {1010, 171}, {993, 156}, {990, 132}};
        g.DrawPolygon(&symbol, shield, 6);
        g.DrawLine(&symbol, 1000.f, 147.f, 1007.f, 154.f); g.DrawLine(&symbol, 1007.f, 154.f, 1020.f, 140.f);

        Rounded(g, {36, 228, 1028, 309}, 18, p.panel, p.border, true);
        Text(g, L"ВАША КЛАВИАТУРА", {56, 241, 350, 23}, 11, p.mutedText, true);
        std::wstring last = L"Нажатые клавиши подсвечиваются";
        if (session && hook.lastKey) {
            const unsigned key = hook.lastKey;
            last = L"Последняя клавиша: ";
            auto found = std::find_if(keys.begin(), keys.end(), [&](const KeyCap& cap) { return cap.key == key; });
            last += found != keys.end() ? (*found->text ? found->text : L"Пробел") : L"VK " + std::to_wstring(key);
        }
        Text(g, last, {699, 241, 345, 23}, 11, p.mutedText);
        constexpr float unit = 43.f;
        for (const auto& key : keys) {
            const bool pressed = hook.down[key.key].load(std::memory_order_relaxed);
            const bool escapeKey = key.key == VK_LCONTROL || key.key == VK_RSHIFT;
            const RectF bounds{56 + key.x * unit, 280 + key.row * 39, key.width * unit - 5, key.height * 39 - 6};
            Rounded(g, bounds, 6, pressed ? keyActive : p.input,
                    escapeKey ? (light ? Rgb(155, 136, 223) : Rgb(111, 94, 165)) : p.border, true);
            Text(g, key.text, bounds, key.width > 1.5f ? 11.5f : 10.5f,
                 pressed ? Rgb(21, 54, 44) : escapeKey ? (light ? p.accent : Rgb(194, 178, 255)) : p.text, false, true);
            if (key.key == VK_SPACE) {
                Gdiplus::Pen spacePen(Color(pressed ? Rgb(21, 54, 44) : p.mutedText), 1);
                g.DrawLine(&spacePen, bounds.X + bounds.Width / 2 - 18, bounds.Y + 21,
                           bounds.X + bounds.Width / 2 + 18, bounds.Y + 21);
            }
        }

        Rounded(g, {36, 557, 489, 84}, 14, p.panel, p.border, true);
        Text(g, L"ВРЕМЯ ЧИСТКИ", {56, 568, 200, 20}, 10, p.mutedText, true);
        Text(g, Duration(session ? GetTickCount64() - started : elapsed), {56, 590, 205, 34}, 25, p.text, true);
        g.DrawLine(&divider, 272.f, 576.f, 272.f, 622.f);
        Text(g, L"ПЕРЕХВАЧЕНО НАЖАТИЙ", {295, 568, 210, 20}, 10, p.mutedText, true);
        Text(g, std::to_wstring(hook.presses.load()), {295, 590, 210, 34}, 25, p.text, true);

        Rounded(g, {541, 557, 523, 84}, 14, p.panel, p.border, true);
        Text(g, L"БЫСТРЫЙ ВЫХОД", {561, 568, 220, 20}, 10, p.mutedText, true);
        Rounded(g, {561, 598, 90, 27}, 6, accentSoft);
        Text(g, L"Левый Ctrl", {561, 598, 90, 27}, 11, light ? p.accent : Rgb(194, 178, 255), false, true);
        Text(g, L"+", {656, 598, 18, 27}, 13, p.mutedText, false, true);
        Rounded(g, {680, 598, 105, 27}, 6, accentSoft);
        Text(g, L"Правый Shift", {680, 598, 105, 27}, 11, light ? p.accent : Rgb(194, 178, 255), false, true);
        Text(g, L"удерживать 1,5 сек.", {803, 598, 235, 27}, 13, p.mutedText);
        const float progress = hook.unlockProgress;
        if (progress > 0) Rounded(g, {561, 633, 483 * progress, 3}, 1.5f, p.success);

        const std::wstring help = !notice.empty() ? notice : draining ? L"Дождитесь отпускания всех зажатых клавиш." :
            session ? L"Чтобы вернуть ввод, нажмите кнопку мышью или удерживайте сочетание выше." :
            L"Перед началом отпустите все клавиши. Окно останется поверх остальных.";
        Text(g, help, {342, 664, 712, 52}, 13, p.mutedText);
        g.DrawLine(&divider, 36.f, 740.f, 1064.f, 740.f);
        Text(g, L"Ctrl+Alt+Del, Fn и питание не блокируются. Защищённые экраны Windows — вне режима чистки.",
             {36, 749, 960, 26}, 11, p.mutedText);
        Text(g, L"v" KEYBOARD_CLEANER_VERSION, {1010, 749, 54, 26}, 11, p.mutedText);
    }

    void Paint() {
        PAINTSTRUCT ps{}; const HDC target = BeginPaint(window, &ps);
        RECT bounds{}; GetClientRect(window, &bounds);
        const HDC memory = CreateCompatibleDC(target);
        const HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1L, bounds.right), std::max(1L, bounds.bottom));
        if (memory && bitmap) {
            const auto previous = SelectObject(memory, bitmap);
            {
                Gdiplus::Graphics graphics(memory);
                graphics.Clear(Color(wcw::GetTheme().palette.window));
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
                graphics.TranslateTransform(offsetX * dpiScale, offsetY * dpiScale);
                graphics.ScaleTransform(scale * dpiScale, scale * dpiScale);
                PaintContent(graphics);
            }
            BitBlt(target, 0, 0, bounds.right, bounds.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        EndPaint(window, &ps);
    }
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        if (!app->CreateControls()) return -1;
        if (!SetTimer(window, 1, 50, nullptr)) return -1;
        WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) {
            if (IsZoomed(window)) {
                auto& rect = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
                const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, GetDpiForWindow(window)) +
                                   GetSystemMetricsForDpi(SM_CXPADDEDBORDER, GetDpiForWindow(window));
                InflateRect(&rect, -border, -border);
            }
            return 0;
        }
        return 0;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        // Update activation without allowing DefWindowProc to repaint the system frame.
        return DefWindowProcW(window, message, wParam, -1);
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(window, &point);
        RECT client{}; GetClientRect(window, &client);
        const int border = static_cast<int>(8 * app->dpiScale);
        if (!IsZoomed(window)) {
            const bool left = point.x < border, right = point.x >= client.right - border;
            const bool top = point.y < border, bottom = point.y >= client.bottom - border;
            if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
        }
        if (ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE) != window) return HTCLIENT;
        if (point.y < (app->offsetY + 62 * app->scale) * app->dpiScale) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto& limits = *reinterpret_cast<MINMAXINFO*>(lParam);
        const float dpi = GetDpiForWindow(window) / 96.0f;
        limits.ptMinTrackSize = {static_cast<LONG>(880 * dpi), static_cast<LONG>(632 * dpi)};
        return 0;
    }
    case WM_SIZE: if (wParam != SIZE_MINIMIZED) app->Layout(); return 0;
    case WM_DPICHANGED: {
        const auto& rect = *reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->Layout(); return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED) break;
        switch (LOWORD(wParam)) {
        case CleanId: app->ToggleCleaning(); return 0;
        case ThemeId: app->light = !app->light; app->ApplyTheme(); return 0;
        case MinimizeId: if (!app->session) ShowWindow(window, SW_MINIMIZE); return 0;
        case CloseId: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
        case UpdateId: app->DownloadUpdate(); return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE && app->session) return 0;
        break;
    case WM_TIMER:
        app->RefreshState();
        if (app->session) InvalidateRect(window, nullptr, FALSE);
        app->PollUpdate();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_LOGOFF || wParam == WTS_CONSOLE_DISCONNECT ||
            wParam == WTS_REMOTE_DISCONNECT) app->CancelSession(L"Сеанс изменился. Режим чистки завершён.");
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) app->CancelSession(L"Компьютер переходит в сон. Режим чистки завершён.");
        return TRUE;
    case WM_QUERYENDSESSION: app->hook.Abort(); return TRUE;
    case WM_CLOSE: app->hook.Abort(); DestroyWindow(window); return 0;
    case WM_DESTROY:
        app->hook.Abort(); KillTimer(window, 1); WTSUnRegisterSessionNotification(window); PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: app->Paint(); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    const HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\KeyboardCleaner.Singleton");
    if (!singleton) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const auto existing = FindWindowW(WindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing);
        }
        CloseHandle(singleton); return 0;
    }
    if (!wcw::Initialize(instance)) { CloseHandle(singleton); return 1; }
    App app;
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance; cls.lpfnWndProc = WindowProc; cls.lpszClassName = WindowClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); cls.hIconSm = cls.hIcon;
    if (!RegisterClassExW(&cls)) { wcw::Shutdown(); CloseHandle(singleton); return 1; }
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const float dpi = GetDpiForSystem() / 96.0f;
    const int width = std::min<LONG>(static_cast<LONG>(DesignWidth * dpi), monitor.rcWork.right - monitor.rcWork.left - 32);
    const int height = std::min<LONG>(static_cast<LONG>(DesignHeight * dpi), monitor.rcWork.bottom - monitor.rcWork.top - 32);
    const HWND window = CreateWindowExW(WS_EX_APPWINDOW, WindowClass, L"Keyboard Cleaner",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
        width, height, nullptr, nullptr, instance, &app);
    if (!window) { wcw::Shutdown(); CloseHandle(singleton); return 1; }
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    ShowWindow(window, show); UpdateWindow(window);
    if (std::wstring_view(commandLine) == L"--update-failed")
        app.notice = L"Обновление не установлено. Предыдущая версия восстановлена.";
    app.updater.Check();
    MSG message{}; BOOL result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    app.hook.Abort();
    if (IsWindow(window)) DestroyWindow(window);
    wcw::Shutdown(); CloseHandle(singleton);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
