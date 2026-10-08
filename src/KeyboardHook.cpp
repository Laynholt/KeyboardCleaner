#include "KeyboardHook.h"

namespace {
constexpr UINT StopMessage = WM_APP + 1;
constexpr UINT AbortMessage = WM_APP + 2;
}
thread_local KeyboardHook* KeyboardHook::current_{};

bool KeyboardHook::Start() {
    Abort();
    error = 0;
    presses = 0;
    lastKey = 0;
    abort_ = false;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready_) { error = GetLastError(); return false; }
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, &threadId_);
    if (!thread_) {
        error = GetLastError();
        CloseHandle(ready_); ready_ = nullptr;
        return false;
    }
    const DWORD wait = WaitForSingleObject(ready_, 5000);
    if (wait != WAIT_OBJECT_0) {
        error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        Abort();
        return false;
    }
    CloseHandle(ready_); ready_ = nullptr;
    if (!running) { Abort(); return false; }
    return true;
}

void KeyboardHook::Stop() {
    if (running && !PostThreadMessageW(threadId_, StopMessage, 0, 0)) {
        error = GetLastError();
        Abort();
    }
}

void KeyboardHook::Abort() {
    if (thread_) {
        abort_ = true;
        PostThreadMessageW(threadId_, AbortMessage, 0, 0);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_); thread_ = nullptr;
    }
    if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
}

void KeyboardHook::Publish() {
    for (unsigned i = 0; i < down.size(); ++i) down[i].store(state_.down[i], std::memory_order_relaxed);
    presses.store(state_.presses, std::memory_order_relaxed);
    unlockProgress.store(state_.UnlockProgress(GetTickCount64()), std::memory_order_relaxed);
    mode.store(state_.mode, std::memory_order_relaxed);
}

LRESULT CALLBACK KeyboardHook::HookProc(int code, WPARAM message, LPARAM data) {
    if (code != HC_ACTION || !current_ || current_->abort_)
        return CallNextHookEx(nullptr, code, message, data);
    if (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP)
        return CallNextHookEx(nullptr, code, message, data);
    auto& self = *current_;
    const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const unsigned normalized = NormalizeKey(key.vkCode, key.scanCode, (key.flags & LLKHF_EXTENDED) != 0);
    const auto result = self.state_.Handle(normalized, pressed, GetTickCount64());
    if (result.changed) {
        if (pressed) self.lastKey = normalized;
        self.Publish();
    }
    return result.blocked ? 1 : CallNextHookEx(nullptr, code, message, data);
}

DWORD WINAPI KeyboardHook::ThreadProc(void* context) {
    auto& self = *static_cast<KeyboardHook*>(context);
    current_ = &self;
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    UINT_PTR timer{};
    if (!hook) self.error = GetLastError();
    else {
        timer = SetTimer(nullptr, 0, 25, nullptr);
        if (!timer) self.error = GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (hook && timer && !self.abort_) {
        self.state_.Start();
        self.Publish();
        self.running = true;
    }
    SetEvent(self.ready_);
    while (self.running && !self.abort_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result < 0) self.error = GetLastError();
            break;
        }
        if (message.message == AbortMessage) break;
        if (message.message == StopMessage) self.state_.Stop();
        if (message.message == WM_TIMER) self.state_.Tick(GetTickCount64());
        self.Publish();
        if (self.state_.mode == KeyboardState::Mode::Idle) break;
    }
    if (timer) KillTimer(nullptr, timer);
    if (hook) UnhookWindowsHookEx(hook);
    self.state_.down.fill(false);
    self.state_.mode = KeyboardState::Mode::Idle;
    self.Publish();
    self.running = false;
    current_ = nullptr;
    return 0;
}
