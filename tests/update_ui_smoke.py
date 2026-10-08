"""Exercise the real UI/updater using a test-only local transport, never public releases."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
build = root / "build/Release"
workspace = (root / "out").resolve()
user = c.WinDLL("user32")
for name, args, result in (
    ("FindWindowW", [w.LPCWSTR, w.LPCWSTR], w.HWND),
    ("GetDlgItem", [w.HWND, c.c_int], w.HWND),
    ("SendMessageW", [w.HWND, w.UINT, w.WPARAM, w.LPARAM], w.LPARAM),
    ("IsWindowVisible", [w.HWND], w.BOOL),
    ("IsWindow", [w.HWND], w.BOOL),
    ("GetWindowTextW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ("GetWindowRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetCursorPos", [c.POINTER(w.POINT)], w.BOOL),
    ("SetCursorPos", [c.c_int, c.c_int], w.BOOL),
):
    fn = getattr(user, name); fn.argtypes = args; fn.restype = result

def wait(predicate, reason, seconds=10):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = predicate()
        if value: return value
        time.sleep(.03)
    raise AssertionError(reason)

def text(hwnd):
    buffer = c.create_unicode_buffer(512); user.GetWindowTextW(hwnd, buffer, len(buffer))
    return buffer.value

def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()

for case in ("offline", "equal", "missing_hash", "malformed", "bad_hash", "new"):
    assert not user.FindWindowW("KeyboardCleaner.Main", None), "close the regular app before testing"
    folder = Path(tempfile.mkdtemp(prefix=f"update-ui-{case}-", dir=workspace)).resolve()
    assert folder.is_relative_to(workspace)
    app = folder / "KeyboardCleaner.exe"
    shutil.copyfile(build / "KeyboardCleanerUiFixture.exe", app)
    original_hash = digest(app)
    release_dir = folder / "release"; release_dir.mkdir()
    asset = release_dir / "KeyboardCleaner.exe"
    shutil.copyfile(build / "UpdateFixture.exe", asset)
    asset_hash = digest(asset)
    tag = "v1.0.1" if case == "equal" else "v2.0.0"
    prefix = f"https://github.com/Laynholt/KeyboardCleaner/releases/download/{tag}/"
    assets = [{"name": "KeyboardCleaner.exe", "size": asset.stat().st_size, "browser_download_url": prefix + "KeyboardCleaner.exe"}]
    if case != "missing_hash":
        assets.append({"name": "SHA256SUMS.txt", "browser_download_url": prefix + "SHA256SUMS.txt"})
    if case != "offline":
        (release_dir / "release.json").write_text("{broken" if case == "malformed" else
            json.dumps({"tag_name": tag, "draft": False, "prerelease": False, "assets": assets}), encoding="utf-8")
    checksum = "0" * 64 if case == "bad_hash" else asset_hash
    (release_dir / "SHA256SUMS.txt").write_text(checksum + "  KeyboardCleaner.exe\n", encoding="ascii")
    env = dict(os.environ, KEYBOARD_CLEANER_TEST_RELEASE=str(release_dir))
    process = subprocess.Popen([str(app)], env=env)
    hwnd = None
    try:
        hwnd = wait(lambda: user.FindWindowW("KeyboardCleaner.Main", None), "main window starts")
        button = user.GetDlgItem(hwnd, 105)
        assert button, "update button exists as a custom control"
        if case in ("bad_hash", "new"):
            wait(lambda: user.IsWindowVisible(button), "new release shows update arrow")
            assert text(button) == "↓"
            saved = w.POINT(); user.GetCursorPos(c.byref(saved))
            try:
                rect = w.RECT(); user.GetWindowRect(button, c.byref(rect))
                user.SetCursorPos((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)
                user.SendMessageW(button, 0x0200, 0, 5 | (5 << 16))
                tip = wait(lambda: user.FindWindowW("WcwTooltipPopup", None), "custom tooltip appears")
                wait(lambda: user.IsWindowVisible(tip), "update tooltip is visible")
                assert "2.0.0" in text(tip) and "скачать" in text(tip), "tooltip describes the available release"
            finally:
                user.SetCursorPos(saved.x, saved.y)
            if case == "new":
                subprocess.run([str(root / "build/Release/UpdateTests.exe")], check=True)
            user.SendMessageW(button, 0x00F5, 0, 0)  # BM_CLICK: download and install
            if case == "bad_hash":
                time.sleep(.5)
                assert process.poll() is None and digest(app) == original_hash, "failed hash never replaces the working app"
                assert user.IsWindowVisible(button), "manual failure leaves a retry button"
            else:
                assert process.wait(timeout=10) == 0, "old UI exits after verified download"
                wait(lambda: user.FindWindowW("KeyboardCleaner.UpdateFixture", "KeyboardCleaner.NewFixture"),
                     "installed update starts", seconds=15)
                wait(lambda: digest(app) == asset_hash and not list(folder.glob(".keyboard-cleaner-update-*")),
                     "replacement is verified and temporary files removed")
        else:
            time.sleep(.6)
            assert not user.IsWindowVisible(button), "no arrow for offline/current/invalid releases"
            assert process.poll() is None and digest(app) == original_hash
        print(f"Update UI {case} passed")
    finally:
        if hwnd and user.IsWindow(hwnd): user.SendMessageW(hwnd, 0x0010, 0, 0)
        fixture = user.FindWindowW("KeyboardCleaner.UpdateFixture", "KeyboardCleaner.NewFixture")
        if fixture: user.SendMessageW(fixture, 0x0010, 0, 0)
        process.wait(timeout=10)
        time.sleep(.2)
        # Scope verified above; remove only fixture files, never a recursive computed target.
        for job in folder.glob(".keyboard-cleaner-update-*"):
            assert job.resolve().is_relative_to(folder)
            for file in job.iterdir(): file.unlink()
            job.rmdir()
        for file in release_dir.iterdir(): file.unlink()
        release_dir.rmdir(); app.unlink(); folder.rmdir()
