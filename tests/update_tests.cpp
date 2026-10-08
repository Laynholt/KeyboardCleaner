#include "Update.h"
#include "AppVersion.h"
#include <fstream>
#include <iostream>
#include <cstdlib>

void Check(bool value, const char* text) {
    if (!value) { std::cerr << "FAILED: " << text << '\n'; std::exit(1); }
}
bool Rejected(const std::filesystem::path& path, const std::string& hash) {
    try { update::VerifyExecutable(path, hash); return false; }
    catch (...) { return true; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && (std::wstring_view(argv[1]) == L"--install-fixture" || std::wstring_view(argv[1]) == L"--install-wait")) {
        const std::filesystem::path target = argv[2], source = argv[3];
        update::StartInstaller(target, source, update::FileSha256(source));
        if (std::wstring_view(argv[1]) == L"--install-wait") Sleep(2000);
        return 0;
    }
    Check(update::IsNewer("v1.10.0", "1.9.9"), "numeric version ordering");
    Check(!update::IsNewer("1.0.1", "v1.0.1"), "equal versions have no update");
    Check(!update::IsNewer("0.9.9", "1.0.1"), "older releases are ignored");
    Check(!update::IsNewer("2.0.0-beta", "1.0.1"), "prerelease tags rejected");
    Check(!update::IsNewer("9999999999999999.0", "1.0.1"), "overflowing version rejected");
    Check(!update::IsNewer("2..0", "1.0.1"), "empty version component rejected");
    const std::string release = R"({"tag_name":"v2.0.0","draft":false,"prerelease":false,"assets":[
        {"name":"KeyboardCleaner.exe","size":1234,"browser_download_url":"https://github.com/Laynholt/KeyboardCleaner/releases/download/v2.0.0/KeyboardCleaner.exe"},
        {"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/Laynholt/KeyboardCleaner/releases/download/v2.0.0/SHA256SUMS.txt"}]})";
    auto parsed = update::ParseRelease(release, KEYBOARD_CLEANER_VERSION);
    Check(bool(parsed) && parsed.size == 1234 && parsed.version == "2.0.0", "exe and checksum pinned to the same release");
    Check(!update::ParseRelease("{invalid", KEYBOARD_CLEANER_VERSION), "invalid JSON silently ignored");
    Check(!update::ParseRelease(release, "2.0.0"), "current version hides the update arrow");
    auto bad = release;
    bad.replace(bad.find("https://"), 5, "http:");
    Check(!update::ParseRelease(bad, "1.0.0"), "insecure asset URL rejected");
    bad = release;
    bad.replace(bad.find("SHA256SUMS.txt"), 13, "OTHERFILE.txt");
    Check(!update::ParseRelease(bad, "1.0.0"), "missing checksum asset hides the arrow");
    bad = release;
    bad.replace(bad.find("\"draft\":false"), 13, "\"draft\":true");
    Check(!update::ParseRelease(bad, "1.0.0"), "draft release ignored");
    const std::string hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    Check(update::ChecksumForExe(hash + "  KeyboardCleaner.exe\r\n") == hash, "SHA256SUMS parsing");
    Check(update::ChecksumForExe(hash + " *KeyboardCleaner.exe\n") == hash, "binary checksum format");
    Check(update::ChecksumForExe(hash + "  Other.exe\n").empty(), "checksum matches the exact exe filename");
    Check(update::ChecksumForExe("bad  KeyboardCleaner.exe").empty(), "bad checksum is rejected");
    Check(update::ChecksumForExe(hash + "  KeyboardCleaner.exe\n" + std::string(64, '0') + "  KeyboardCleaner.exe\n").empty(),
          "ambiguous checksum is rejected");
    const auto file = std::filesystem::temp_directory_path() / (L"KeyboardCleaner-hash-test-" + std::to_wstring(GetCurrentProcessId()));
    { std::ofstream out(file, std::ios::binary); out << "abc"; }
    Check(update::FileSha256(file) == hash, "SHA256 implementation matches known vector");
    Check(Rejected(file, std::string(64, '0')), "hash mismatch stops installation");
    Check(Rejected(file, hash), "matching hash is insufficient for a non-executable file");
    std::filesystem::remove(file);
    const auto self = update::CurrentExecutable();
    Check(!Rejected(self, update::FileSha256(self)), "actual x64 executable passes verification");
    std::cout << "Update version, release, checksum and executable checks passed\n";
}
