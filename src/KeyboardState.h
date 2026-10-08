#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

inline constexpr unsigned NumpadEnter = 0x0e;
inline constexpr std::uint64_t UnlockHoldMs = 1500;

inline unsigned NormalizeKey(unsigned key, unsigned scan, bool extended) {
    if (key == VK_SHIFT) return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    if (key == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (key == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    if (key == VK_RETURN && extended) return NumpadEnter;
    if (!extended && scan >= 0x47 && scan <= 0x53) {
        switch (scan) {
        case 0x47: return VK_NUMPAD7; case 0x48: return VK_NUMPAD8; case 0x49: return VK_NUMPAD9;
        case 0x4b: return VK_NUMPAD4; case 0x4c: return VK_NUMPAD5; case 0x4d: return VK_NUMPAD6;
        case 0x4f: return VK_NUMPAD1; case 0x50: return VK_NUMPAD2; case 0x51: return VK_NUMPAD3;
        case 0x52: return VK_NUMPAD0; case 0x53: return VK_DECIMAL;
        }
    }
    return key;
}

struct KeyboardState {
    enum class Mode { Idle, Cleaning, Draining };
    struct Result { bool blocked{}, changed{}; };
    Mode mode{Mode::Idle};
    std::array<bool, 256> down{};
    std::uint64_t presses{};
    std::uint64_t chordSince{};
    bool chordHeld{};

    void Start() { *this = {}; mode = Mode::Cleaning; }
    bool AnyDown() const {
        for (bool value : down) if (value) return true;
        return false;
    }
    void Stop() {
        mode = AnyDown() ? Mode::Draining : Mode::Idle;
        chordHeld = false;
    }
    Result Handle(unsigned key, bool pressed, std::uint64_t now) {
        if (mode == Mode::Idle) return {};
        if (key >= down.size()) return {mode == Mode::Cleaning, false};
        if (mode == Mode::Draining) {
            if (!down[key]) return {};
            if (!pressed) {
                down[key] = false;
                if (!AnyDown()) mode = Mode::Idle;
            }
            return {true, !pressed};
        }
        const bool changed = down[key] != pressed;
        if (changed && pressed) ++presses;
        down[key] = pressed;
        const bool held = down[VK_LCONTROL] && down[VK_RSHIFT];
        if (held && !chordHeld) chordSince = now;
        chordHeld = held;
        return {true, changed};
    }
    float UnlockProgress(std::uint64_t now) const {
        if (!chordHeld || mode != Mode::Cleaning) return 0;
        const auto elapsed = now - chordSince;
        return elapsed >= UnlockHoldMs ? 1.0f : static_cast<float>(elapsed) / UnlockHoldMs;
    }
    bool Tick(std::uint64_t now) {
        if (UnlockProgress(now) < 1) return false;
        Stop();
        return true;
    }
};
