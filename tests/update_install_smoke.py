"""Use disposable EXEs to test replacement, restart, rollback and a late hash mismatch."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
build = root / "build/Release"
workspace = (root / "out").resolve()
workspace.mkdir(exist_ok=True)
user = c.WinDLL("user32")
user.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]; user.FindWindowW.restype = w.HWND
user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def close_fixtures():
    for name in ("KeyboardCleaner.NewFixture", "KeyboardCleaner.OldFixture"):
        hwnd = user.FindWindowW("KeyboardCleaner.UpdateFixture", name)
        if hwnd:
            user.SendMessageW(hwnd, 0x0010, 0, 0)
    time.sleep(.15)

for case, binary, expected in (
    ("success", "UpdateFixture.exe", "KeyboardCleaner.NewFixture"),
    ("rollback", "UpdateBrokenFixture.exe", "KeyboardCleaner.OldFixture"),
    ("tamper", "UpdateFixture.exe", "KeyboardCleaner.OldFixture"),
):
    assert not user.FindWindowW("KeyboardCleaner.UpdateFixture", None), "close previous test fixtures"
    folder = Path(tempfile.mkdtemp(prefix=f"Обновление '{case}' ", dir=workspace)).resolve()
    assert folder.is_relative_to(workspace), "all disposable targets stay inside the workspace"
    target = folder / "Keyboard Cleaner.exe"
    job = folder / "job"; job.mkdir()
    source = job / "new.exe"
    shutil.copyfile(build / "UpdateOldFixture.exe", target)
    shutil.copyfile(build / binary, source)
    old_hash, new_hash = digest(target), digest(source)
    try:
        mode = "--install-wait" if case == "tamper" else "--install-fixture"
        driver = subprocess.Popen([str(build / "UpdateTests.exe"), mode, str(target), str(source)])
        if case == "tamper":
            deadline = time.monotonic() + 5
            while not (job / "apply.ps1").exists():
                assert driver.poll() is None and time.monotonic() < deadline
                time.sleep(.01)
            assert driver.poll() is None, "installer waits for the running parent"
            with source.open("ab") as file:
                file.write(b"corrupted after C++ verification")
        assert driver.wait(timeout=5) == 0
        deadline = time.monotonic() + 20
        wanted = new_hash if case == "success" else old_hash
        while time.monotonic() < deadline:
            if (target.exists() and digest(target) == wanted and not job.exists()
                    and user.FindWindowW("KeyboardCleaner.UpdateFixture", expected)):
                break
            time.sleep(.1)
        else:
            print("Target hash:", digest(target) if target.exists() else "missing", "expected:", wanted)
            print("Stage files:", list(job.iterdir()) if job.exists() else "removed")
            print("Fixture windows:", user.FindWindowW("KeyboardCleaner.UpdateFixture", "KeyboardCleaner.NewFixture"),
                  user.FindWindowW("KeyboardCleaner.UpdateFixture", "KeyboardCleaner.OldFixture"))
            raise AssertionError(f"{case}: replacement/restart/cleanup did not complete")
        print(f"Installer {case} passed")
    finally:
        close_fixtures()
        # Delete only these verified disposable files; no recursive shell cleanup.
        for file in (job / "new.exe", job / "apply.ps1", job / "previous.exe", job / "failed.exe", target):
            file.unlink(missing_ok=True)
        if job.exists(): job.rmdir()
        folder.rmdir()
