#pragma once
#include "KeyboardState.h"
#include <vector>

struct KeyCap {
    unsigned key;
    const wchar_t* text;
    float x, row, width{1}, height{1};
};

inline std::vector<KeyCap> MakeKeyboard() {
    std::vector<KeyCap> keys;
    auto add = [&](unsigned key, const wchar_t* text, float x, float row, float width = 1, float height = 1) {
        keys.push_back({key, text, x, row, width, height});
    };
    add(VK_ESCAPE, L"Esc", 0, 0);
    const wchar_t* functions[]{L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10", L"F11", L"F12"};
    for (unsigned i = 0; i < 12; ++i) add(VK_F1 + i, functions[i], 2 + static_cast<float>(i) + (i / 4) * .4f, 0);
    add(VK_OEM_3, L"~ Ё", 0, 1);
    const wchar_t* numbers[]{L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};
    for (unsigned i = 0; i < 10; ++i) add(i == 9 ? '0' : '1' + i, numbers[i], 1 + static_cast<float>(i), 1);
    add(VK_OEM_MINUS, L"−", 11, 1); add(VK_OEM_PLUS, L"+", 12, 1); add(VK_BACK, L"Backspace", 13, 1, 2);
    add(VK_TAB, L"Tab", 0, 2, 1.5f);
    const wchar_t* top[]{L"Q Й", L"W Ц", L"E У", L"R К", L"T Е", L"Y Н", L"U Г", L"I Ш", L"O Щ", L"P З"};
    const char* topKeys = "QWERTYUIOP";
    for (int i = 0; i < 10; ++i) add(topKeys[i], top[i], 1.5f + i, 2);
    add(VK_OEM_4, L"[ Х", 11.5f, 2); add(VK_OEM_6, L"] Ъ", 12.5f, 2); add(VK_OEM_5, L"\\", 13.5f, 2, 1.5f);
    add(VK_CAPITAL, L"Caps Lock", 0, 3, 1.75f);
    const wchar_t* home[]{L"A Ф", L"S Ы", L"D В", L"F А", L"G П", L"H Р", L"J О", L"K Л", L"L Д"};
    const char* homeKeys = "ASDFGHJKL";
    for (int i = 0; i < 9; ++i) add(homeKeys[i], home[i], 1.75f + i, 3);
    add(VK_OEM_1, L"; Ж", 10.75f, 3); add(VK_OEM_7, L"' Э", 11.75f, 3); add(VK_RETURN, L"Enter", 12.75f, 3, 2.25f);
    add(VK_LSHIFT, L"Shift", 0, 4, 2.25f);
    const wchar_t* bottom[]{L"Z Я", L"X Ч", L"C С", L"V М", L"B И", L"N Т", L"M Ь"};
    const char* bottomKeys = "ZXCVBNM";
    for (int i = 0; i < 7; ++i) add(bottomKeys[i], bottom[i], 2.25f + i, 4);
    add(VK_OEM_COMMA, L", Б", 9.25f, 4); add(VK_OEM_PERIOD, L". Ю", 10.25f, 4); add(VK_OEM_2, L"/", 11.25f, 4);
    add(VK_RSHIFT, L"Shift", 12.25f, 4, 2.75f);
    add(VK_LCONTROL, L"Ctrl", 0, 5, 1.25f); add(VK_LWIN, L"Win", 1.25f, 5, 1.25f); add(VK_LMENU, L"Alt", 2.5f, 5, 1.25f);
    add(VK_SPACE, L"", 3.75f, 5, 6.25f); add(VK_RMENU, L"Alt", 10, 5, 1.25f); add(VK_RWIN, L"Win", 11.25f, 5, 1.25f);
    add(VK_APPS, L"Menu", 12.5f, 5, 1.25f); add(VK_RCONTROL, L"Ctrl", 13.75f, 5, 1.25f);
    add(VK_SNAPSHOT, L"PrtSc", 15.6f, 0); add(VK_SCROLL, L"Scroll", 16.6f, 0); add(VK_PAUSE, L"Pause", 17.6f, 0);
    add(VK_INSERT, L"Ins", 15.6f, 1); add(VK_HOME, L"Home", 16.6f, 1); add(VK_PRIOR, L"PgUp", 17.6f, 1);
    add(VK_DELETE, L"Del", 15.6f, 2); add(VK_END, L"End", 16.6f, 2); add(VK_NEXT, L"PgDn", 17.6f, 2);
    add(VK_UP, L"↑", 16.6f, 4); add(VK_LEFT, L"←", 15.6f, 5); add(VK_DOWN, L"↓", 16.6f, 5); add(VK_RIGHT, L"→", 17.6f, 5);
    add(VK_NUMLOCK, L"Num", 19.2f, 1); add(VK_DIVIDE, L"/", 20.2f, 1); add(VK_MULTIPLY, L"×", 21.2f, 1); add(VK_SUBTRACT, L"−", 22.2f, 1);
    add(VK_NUMPAD7, L"7", 19.2f, 2); add(VK_NUMPAD8, L"8", 20.2f, 2); add(VK_NUMPAD9, L"9", 21.2f, 2); add(VK_ADD, L"+", 22.2f, 2, 1, 2);
    add(VK_NUMPAD4, L"4", 19.2f, 3); add(VK_NUMPAD5, L"5", 20.2f, 3); add(VK_NUMPAD6, L"6", 21.2f, 3);
    add(VK_NUMPAD1, L"1", 19.2f, 4); add(VK_NUMPAD2, L"2", 20.2f, 4); add(VK_NUMPAD3, L"3", 21.2f, 4); add(NumpadEnter, L"Enter", 22.2f, 4, 1, 2);
    add(VK_NUMPAD0, L"0", 19.2f, 5, 2); add(VK_DECIMAL, L".", 21.2f, 5);
    return keys;
}
