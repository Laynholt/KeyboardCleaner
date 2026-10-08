#pragma once
#include "KeyboardState.h"
#include <atomic>

class KeyboardHook {
public:
    KeyboardHook() = default;
    ~KeyboardHook() { Abort(); }
    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;
    bool Start();
    void Stop();
    void Abort();

    std::array<std::atomic<bool>, 256> down{};
    std::atomic<KeyboardState::Mode> mode{KeyboardState::Mode::Idle};
    std::atomic<std::uint64_t> presses{};
    std::atomic<float> unlockProgress{};
    std::atomic<unsigned> lastKey{};
    std::atomic<DWORD> error{};
    std::atomic<bool> running{};

private:
    static DWORD WINAPI ThreadProc(void* context);
    static LRESULT CALLBACK HookProc(int code, WPARAM message, LPARAM data);
    void Publish();
    HANDLE thread_{};
    HANDLE ready_{};
    DWORD threadId_{};
    std::atomic<bool> abort_{};
    KeyboardState state_;
    static thread_local KeyboardHook* current_;
};
