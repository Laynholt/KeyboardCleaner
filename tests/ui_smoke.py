"""Exercise the built application and capture only its own window. Requires Pillow."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import sys
import time
import os
from PIL import Image

user = c.WinDLL("user32", use_last_error=True)
gdi = c.WinDLL("gdi32", use_last_error=True)
user.SetProcessDPIAware()
for name, args, result in [
    ("FindWindowW", [w.LPCWSTR, w.LPCWSTR], w.HWND),
    ("GetDlgItem", [w.HWND, c.c_int], w.HWND),
    ("SendMessageW", [w.HWND, w.UINT, w.WPARAM, w.LPARAM], w.LPARAM),
    ("GetWindowTextW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ("GetClassNameW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ("GetWindowRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetWindowDC", [w.HWND], w.HDC),
    ("ReleaseDC", [w.HWND, w.HDC], c.c_int),
    ("SetWindowPos", [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT], w.BOOL),
    ("SetForegroundWindow", [w.HWND], w.BOOL),
    ("GetForegroundWindow", [], w.HWND),
    ("IsWindowEnabled", [w.HWND], w.BOOL),
    ("IsWindow", [w.HWND], w.BOOL),
    ("GetDpiForWindow", [w.HWND], w.UINT),
    ("ShowWindow", [w.HWND, c.c_int], w.BOOL),
    ("IsWindowVisible", [w.HWND], w.BOOL),
    ("GetCursorPos", [c.POINTER(w.POINT)], w.BOOL),
    ("SetCursorPos", [c.c_int, c.c_int], w.BOOL),
    ("WindowFromPoint", [w.POINT], w.HWND),
    ("GetAncestor", [w.HWND, w.UINT], w.HWND),
]:
    fn = getattr(user, name); fn.argtypes = args; fn.restype = result
for name, args, result in [
    ("CreateCompatibleDC", [w.HDC], w.HDC),
    ("CreateCompatibleBitmap", [w.HDC, c.c_int, c.c_int], w.HBITMAP),
    ("SelectObject", [w.HDC, w.HANDLE], w.HANDLE),
    ("BitBlt", [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int, w.DWORD], w.BOOL),
    ("DeleteDC", [w.HDC], w.BOOL),
    ("DeleteObject", [w.HANDLE], w.BOOL),
]:
    fn = getattr(gdi, name); fn.argtypes = args; fn.restype = result

class BitmapHeader(c.Structure):
    _fields_ = [("size", w.DWORD), ("width", w.LONG), ("height", w.LONG),
                ("planes", w.WORD), ("bits", w.WORD), ("compression", w.DWORD),
                ("image_size", w.DWORD), ("xppm", w.LONG), ("yppm", w.LONG),
                ("used", w.DWORD), ("important", w.DWORD)]
class KeyInput(c.Structure):
    _fields_ = [("key", w.WORD), ("scan", w.WORD), ("flags", w.DWORD),
                ("time", w.DWORD), ("extra", c.c_size_t)]
class MouseInput(c.Structure):
    _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("data", w.DWORD),
                ("flags", w.DWORD), ("time", w.DWORD), ("extra", c.c_size_t)]
class InputData(c.Union):
    _fields_ = [("keyboard", KeyInput), ("mouse", MouseInput)]
class Input(c.Structure):
    _fields_ = [("type", w.DWORD), ("data", InputData)]
user.SendInput.argtypes = [w.UINT, c.POINTER(Input), c.c_int]
user.SendInput.restype = w.UINT
gdi.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT]
gdi.GetDIBits.restype = c.c_int

def title(hwnd):
    value = c.create_unicode_buffer(512)
    user.GetWindowTextW(hwnd, value, len(value))
    return value.value

def click(hwnd):
    # Exercise the WCW mouse press/release path, including its capture logic.
    rect = w.RECT(); user.GetWindowRect(hwnd, c.byref(rect))
    x, y = (rect.right - rect.left) // 2, (rect.bottom - rect.top) // 2
    position = x | (y << 16)
    user.SendMessageW(hwnd, 0x0201, 1, position)
    user.SendMessageW(hwnd, 0x0202, 0, position)
    time.sleep(.15)

def key(vk, down):
    value = Input(type=1)
    value.data.keyboard = KeyInput(vk, 0, 0 if down else 2, 0, 0)
    assert user.SendInput(1, c.byref(value), c.sizeof(value)) == 1
    time.sleep(.08)

def foreground(hwnd):
    user.SetForegroundWindow(hwnd)
    if user.GetForegroundWindow() == hwnd:
        return
    if user.GetForegroundWindow() != hwnd:
        # A real click gives focus to this app; synthetic window messages do not.
        rect = w.RECT(); user.GetWindowRect(hwnd, c.byref(rect))
        point = w.POINT((rect.left + rect.right) // 2, rect.top + (rect.bottom - rect.top) // 4)
        assert user.GetAncestor(user.WindowFromPoint(point), 2) == hwnd, "click only our own window"
        saved = w.POINT(); user.GetCursorPos(c.byref(saved))
        try:
            user.SetCursorPos(point.x, point.y)
            events = (Input * 2)()
            events[0].type = events[1].type = 0
            events[0].data.mouse.flags = 2
            events[1].data.mouse.flags = 4
            assert user.SendInput(2, events, c.sizeof(Input)) == 2
            time.sleep(.1)
        finally:
            user.SetCursorPos(saved.x, saved.y)

def capture(hwnd, path, activate=True):
    user.SetWindowPos(hwnd, w.HWND(-1), 0, 0, 0, 0, 0x0013)
    if activate:
        foreground(hwnd)
    time.sleep(.2)
    rect = w.RECT(); assert user.GetWindowRect(hwnd, c.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    dc = user.GetWindowDC(hwnd); memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    try:
        assert gdi.BitBlt(memory, 0, 0, width, height, dc, 0, 0, 0x00CC0020)
        gdi.SelectObject(memory, previous)
        header = BitmapHeader(c.sizeof(BitmapHeader), width, -height, 1, 32)
        pixels = c.create_string_buffer(width * height * 4)
        assert gdi.GetDIBits(dc, bitmap, 0, height, pixels, c.byref(header), 0) == height
        image = Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX")
        image.save(path)
        assert len(image.getcolors(width * height) or []) > 100, "window rendered a real interface"
        return image
    finally:
        gdi.SelectObject(memory, previous); gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory); user.ReleaseDC(hwnd, dc)

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/bin/Release/KeyboardCleaner.exe"
output = root / "out/previews"
output.mkdir(parents=True, exist_ok=True)
assert not user.FindWindowW("KeyboardCleaner.Main", None), "close the running app before smoke testing"
process = subprocess.Popen([str(exe)])
hwnd = None
held = False
try:
    for _ in range(100):
        hwnd = user.FindWindowW("KeyboardCleaner.Main", None)
        if hwnd: break
        assert process.poll() is None, "application exited during startup"
        time.sleep(.05)
    assert hwnd, "application creates its main window"
    time.sleep(.2)
    clean, theme, minimize = [user.GetDlgItem(hwnd, value) for value in (101, 102, 103)]
    for control in (clean, theme, minimize):
        name = c.create_unicode_buffer(128); user.GetClassNameW(control, name, len(name))
        assert name.value == "WcwButton", "the real supplied widget library is in use"
    assert title(clean) == "Начать чистку"
    dark = capture(hwnd, output / "dark.png")
    button_rect, window_rect = w.RECT(), w.RECT()
    assert user.GetWindowRect(clean, c.byref(button_rect))
    assert user.GetWindowRect(hwnd, c.byref(window_rect))
    radius = round(12 * user.GetDpiForWindow(hwnd) / 96)
    corner = dark.crop((button_rect.left - window_rect.left, button_rect.top - window_rect.top,
                        button_rect.left - window_rect.left + radius,
                        button_rect.top - window_rect.top + radius))
    assert any(22 < corner.getpixel((x, y))[1] < 47 for x in range(corner.width)
               for y in range(corner.height)), "rounded button retains softly blended outer edge pixels"
    if os.environ.get("KEYBOARD_CLEANER_TEST_RELEASE"):
        update = user.GetDlgItem(hwnd, 105)
        assert user.IsWindowVisible(update), "fixture release shows its update arrow"
        capture(hwnd, output / "update-available.png")
    user.SendMessageW(hwnd, 0x0086, 0, 0)  # WM_NCACTIVATE: another window takes focus
    user.SendMessageW(hwnd, 0x0085, 1, 0)  # WM_NCPAINT: exposed frame
    exposed = capture(hwnd, output / "frame-exposed.png", activate=False)
    width, height = exposed.size
    for point in ((width // 2, 3), (3, height // 2), (width - 4, height // 2), (width // 2, height - 4)):
        assert exposed.getpixel(point) == (16, 19, 27), "no system border after non-client repaint"
    user.ShowWindow(hwnd, 0)
    user.ShowWindow(hwnd, 4)
    user.SendMessageW(hwnd, 0x0086, 1, 0)
    restored = capture(hwnd, output / "frame-restored.png")
    assert restored.getpixel((3, restored.height // 2)) == (16, 19, 27), "no border after hide and restore"
    click(theme)
    assert "Тёмная" in title(theme), "light theme toggle updates its label"
    capture(hwnd, output / "light.png")
    click(theme)
    other = subprocess.Popen([str(exe)])
    assert other.wait(timeout=3) == 0, "second instance exits without creating a second hook"
    click(clean)
    assert title(clean) == "Завершить чистку", "mouse enables cleaning"
    assert not user.IsWindowEnabled(minimize), "exit button stays reachable during cleaning"
    capture(hwnd, output / "cleaning.png")
    assert user.GetForegroundWindow() == hwnd
    key(ord("K"), True); held = True
    capture(hwnd, output / "key-pressed.png")
    click(theme)
    assert title(clean) == "Завершить чистку", "changing theme preserves the session"
    click(clean)
    assert "Отпустите" in title(clean), "mouse stop waits for held keys"
    key(ord("K"), False); held = False
    time.sleep(.2)
    assert title(clean) == "Начать чистку", "mouse stop restores normal mode after key release"
    assert user.IsWindowEnabled(minimize)
    dpi = user.GetDpiForWindow(hwnd) / 96
    user.SetWindowPos(hwnd, None, 0, 0, int(880 * dpi), int(632 * dpi), 0x0016)
    time.sleep(.2)
    capture(hwnd, output / "minimum-size.png")
    click(clean)
    assert title(clean) == "Завершить чистку", "session can restart after resize"
    user.SendMessageW(hwnd, 0x02B1, 7, 0)  # WM_WTSSESSION_CHANGE / WTS_SESSION_LOCK
    time.sleep(.2)
    assert title(clean) == "Начать чистку", "session lock restores input"
    click(clean)
    assert title(clean) == "Завершить чистку"
    user.SendMessageW(hwnd, 0x0218, 4, 0)  # PBT_APMSUSPEND
    time.sleep(.2)
    assert title(clean) == "Начать чистку", "suspend restores input"
    click(clean)
    user.SendMessageW(hwnd, 0x0010, 0, 0)
    assert process.wait(timeout=3) == 0, "closing an active app removes its hook and exits"
    print("UI smoke checks passed; dark, light, active, pressed and minimum-size previews saved")
finally:
    if held: key(ord("K"), False)
    if hwnd and user.IsWindow(hwnd): user.SendMessageW(hwnd, 0x0010, 0, 0)
    try: process.wait(timeout=3)
    except subprocess.TimeoutExpired: process.kill()
