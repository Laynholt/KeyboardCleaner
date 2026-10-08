#include "Update.h"
#include "AppVersion.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace update {
namespace {
constexpr std::uint64_t MaxExeSize = 128 * 1024 * 1024;
bool VersionParts(std::string_view version, std::array<unsigned, 4>& parts) {
    if (version.starts_with('v')) version.remove_prefix(1);
    for (unsigned i = 0; i < parts.size(); ++i) {
        if (version.empty()) return false;
        const auto dot = version.find('.');
        const auto part = version.substr(0, dot);
        if (part.empty()) return false;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
        if (result.ec != std::errc{} || result.ptr != part.data() + part.size()) return false;
        if (dot == std::string_view::npos) return true;
        version.remove_prefix(dot + 1);
    }
    return false;
}
bool ValidHash(std::string_view hash) {
    if (hash.size() != 64) return false;
    for (char ch : hash) if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')) return false;
    return true;
}
std::string Hex(const unsigned char* data, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result += digits[data[i] >> 4]; result += digits[data[i] & 15];
    }
    return result;
}
std::wstring WideAscii(std::string_view value) { return {value.begin(), value.end()}; }
void Canceled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("update canceled");
}

void Fetch(const std::string& url, std::ostream& output, std::uint64_t limit, std::stop_token stop,
           std::atomic<unsigned>* progress = nullptr) {
    Canceled(stop);
#ifdef KEYBOARD_CLEANER_UPDATE_TESTING
    std::array<wchar_t, 32768> fixtureFolder{};
    if (!GetEnvironmentVariableW(L"KEYBOARD_CLEANER_TEST_RELEASE", fixtureFolder.data(), static_cast<DWORD>(fixtureFolder.size())))
        throw std::runtime_error("test release fixture missing");
    const auto name = url.ends_with("/latest") ? L"release.json" : url.ends_with("/SHA256SUMS.txt") ? L"SHA256SUMS.txt" : L"KeyboardCleaner.exe";
    std::ifstream fixture(std::filesystem::path(fixtureFolder.data()) / name, std::ios::binary);
    if (!fixture) throw std::runtime_error("test release asset missing");
    std::array<char, 65536> fixtureBuffer{};
    std::uint64_t fixtureTotal{};
    while (fixture) {
        Canceled(stop); fixture.read(fixtureBuffer.data(), fixtureBuffer.size());
        fixtureTotal += fixture.gcount();
        if (fixtureTotal > limit) throw std::runtime_error("test release too large");
        output.write(fixtureBuffer.data(), fixture.gcount());
        if (!output) throw std::runtime_error("test release write failed");
        if (progress) progress->store(static_cast<unsigned>(fixtureTotal * 100 / limit));
    }
    if (!fixture.eof()) throw std::runtime_error("test release read failed");
    return;
#endif
    const auto address = WideAscii(url);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("invalid update URL");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
    Handle session(WinHttpOpen(L"KeyboardCleaner/" KEYBOARD_CLEANER_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
    if (!session || !WinHttpSetTimeouts(session.get(), 3000, 3000, 5000, 5000))
        throw std::runtime_error("WinHTTP initialization failed");
    Handle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0), WinHttpCloseHandle);
    if (!connection) throw std::runtime_error("update connection failed");
    Handle request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
    if (!request) throw std::runtime_error("update request failed");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
        throw std::runtime_error("update redirect policy failed");
    const auto deadline = GetTickCount64() + (progress ? 120000 : 15000);
    Canceled(stop);
    if (!WinHttpSendRequest(request.get(), L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr))
        throw std::runtime_error("update HTTP request failed");
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("update HTTP status failed");
    std::array<char, 65536> buffer{};
    std::uint64_t total{};
    while (true) {
        Canceled(stop);
        if (GetTickCount64() > deadline) throw std::runtime_error("update deadline exceeded");
        DWORD count{};
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count))
            throw std::runtime_error("update HTTP read failed");
        if (!count) break;
        total += count;
        if (total > limit) throw std::runtime_error("update response too large");
        output.write(buffer.data(), count);
        if (!output) throw std::runtime_error("update file write failed");
        if (progress) progress->store(static_cast<unsigned>(total * 100 / limit));
    }
}
std::string FetchText(const std::string& url, std::uint64_t limit, std::stop_token stop) {
    std::ostringstream output; Fetch(url, output, limit, stop); return output.str();
}
std::filesystem::path MakeJob() {
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("update temporary name failed");
    auto job = CurrentExecutable().parent_path() / (L".keyboard-cleaner-update-" + WideAscii(Hex(random.data(), random.size())));
    if (!std::filesystem::create_directory(job)) throw std::runtime_error("update folder already exists");
    return job;
}
std::wstring QuoteArgument(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes{};
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; result += ch;
    }
    result.append(slashes * 2, L'\\'); result += L'\"';
    return result;
}
}

bool IsNewer(std::string_view candidate, std::string_view current) {
    std::array<unsigned, 4> next{}, installed{};
    return VersionParts(candidate, next) && VersionParts(current, installed) && next > installed;
}

Release ParseRelease(const std::string& text, std::string_view current) {
    try {
        const auto json = nlohmann::json::parse(text);
        if (!json.is_object() || json.value("draft", false) || json.value("prerelease", false)) return {};
        const auto tag = json.value("tag_name", std::string{});
        if (!IsNewer(tag, current) || !json.contains("assets") || !json["assets"].is_array()) return {};
        const auto prefix = std::string("https://github.com/") + Repository + "/releases/download/" + tag + "/";
        Release release; release.version = tag.starts_with('v') ? tag.substr(1) : tag;
        for (const auto& asset : json["assets"]) {
            if (!asset.is_object()) continue;
            const auto name = asset.value("name", std::string{});
            if (name != "KeyboardCleaner.exe" && name != "SHA256SUMS.txt") continue;
            const auto url = asset.value("browser_download_url", std::string{});
            if (url != prefix + name) return {};
            if (name == "KeyboardCleaner.exe") {
                if (!release.exeUrl.empty() || !asset.contains("size") || !asset["size"].is_number_unsigned()) return {};
                release.size = asset["size"].get<std::uint64_t>();
                if (!release.size || release.size > MaxExeSize) return {};
                release.exeUrl = url;
            } else {
                if (!release.sumsUrl.empty()) return {};
                release.sumsUrl = url;
            }
        }
        return release ? release : Release{};
    } catch (...) { return {}; }
}

std::string ChecksumForExe(const std::string& sums) {
    std::istringstream lines(sums); std::string line, result;
    while (std::getline(lines, line)) {
        std::istringstream row(line); std::string hash, name, extra;
        if (!(row >> hash >> name) || (row >> extra)) continue;
        if (name.starts_with('*')) name.erase(0, 1);
        if (name != "KeyboardCleaner.exe") continue;
        for (char& ch : hash) if (ch >= 'A' && ch <= 'F') ch += 'a' - 'A';
        if (!ValidHash(hash) || !result.empty()) return {};
        result = hash;
    }
    return result;
}

std::string FileSha256(const std::filesystem::path& path) {
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        ~Handles() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0) != 0)
        throw std::runtime_error("SHA256 initialization failed");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("SHA256 file open failed");
    std::array<char, 65536> buffer{};
    while (file) {
        file.read(buffer.data(), buffer.size());
        if (const auto count = file.gcount(); count &&
            BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) != 0)
            throw std::runtime_error("SHA256 computation failed");
    }
    if (!file.eof()) throw std::runtime_error("SHA256 file read failed");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(handles.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0)
        throw std::runtime_error("SHA256 finish failed");
    return Hex(digest.data(), digest.size());
}

void VerifyExecutable(const std::filesystem::path& path, const std::string& expectedHash) {
    if (!ValidHash(expectedHash) || FileSha256(path) != expectedHash) throw std::runtime_error("update checksum mismatch");
    std::ifstream file(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    const auto size = std::filesystem::file_size(path);
    if (!file || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        static_cast<std::uint64_t>(dos.e_lfanew) + sizeof(nt) > size)
        throw std::runtime_error("invalid update executable");
    file.seekg(dos.e_lfanew); file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    if (!file || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !(nt.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) ||
        (nt.FileHeader.Characteristics & IMAGE_FILE_DLL)) throw std::runtime_error("wrong update executable type");
}

std::filesystem::path CurrentExecutable() {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("executable path failed");
    return std::wstring(path.data(), length);
}

void StartInstaller(const std::filesystem::path& target, const std::filesystem::path& source, const std::string& hash) {
    VerifyExecutable(source, hash);
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(2), RT_RCDATA);
    const auto loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const auto data = loaded ? LockResource(loaded) : nullptr;
    const auto size = resource ? SizeofResource(nullptr, resource) : 0;
    if (!data || !size) throw std::runtime_error("installer resource missing");
    const auto script = source.parent_path() / L"apply.ps1";
    {
        std::ofstream out(script, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), size);
        out.close(); if (!out) throw std::runtime_error("installer script write failed");
    }
    std::array<wchar_t, MAX_PATH> windows{};
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (!length || length >= windows.size()) throw std::runtime_error("Windows folder failed");
    const auto powershell = std::filesystem::path(windows.data()) / L"System32/WindowsPowerShell/v1.0/powershell.exe";
    const auto backup = source.parent_path() / L"previous.exe";
    std::wstring command = QuoteArgument(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        QuoteArgument(script.wstring()) + L" -ParentPid " + std::to_wstring(GetCurrentProcessId()) +
        L" -Source " + QuoteArgument(source.wstring()) + L" -Target " + QuoteArgument(target.wstring()) +
        L" -Backup " + QuoteArgument(backup.wstring()) + L" -Sha256 " + WideAscii(hash);
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, target.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("installer launch failed");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}

void Updater::Cleanup() {
    if (job_.empty() || handedOff_) return;
    std::error_code ignored;
    std::filesystem::remove(source_, ignored);
    std::filesystem::remove(job_ / L"apply.ps1", ignored);
    std::filesystem::remove(job_, ignored);
    job_.clear(); source_.clear();
}
Updater::~Updater() {
    worker_.request_stop(); if (worker_.joinable()) worker_.join(); Cleanup();
}
void Updater::Check() {
    status_ = Status::Checking;
    try {
        worker_ = std::jthread([this](std::stop_token stop) {
            try {
                const auto json = FetchText(std::string("https://api.github.com/repos/") + Repository + "/releases/latest", 2 * 1024 * 1024, stop);
                release_ = ParseRelease(json, KEYBOARD_CLEANER_VERSION);
                status_ = release_ && !stop.stop_requested() ? Status::Available : Status::None;
            } catch (...) { status_ = Status::None; }
        });
    } catch (...) { status_ = Status::None; }
}
void Updater::Download() {
    if (State() != Status::Available) return;
    if (worker_.joinable()) worker_.join();
    Cleanup(); failed = false; progress = 0; status_ = Status::Downloading;
    try {
        worker_ = std::jthread([this](std::stop_token stop) {
            try {
                const auto sums = FetchText(release_.sumsUrl, 65536, stop);
                expectedHash_ = ChecksumForExe(sums);
                if (expectedHash_.empty()) throw std::runtime_error("update checksum missing");
                job_ = MakeJob(); source_ = job_ / L"new.exe";
                {
                    std::ofstream file(source_, std::ios::binary | std::ios::trunc);
                    if (!file) throw std::runtime_error("update file creation failed");
                    Fetch(release_.exeUrl, file, release_.size, stop, &progress);
                    file.close(); if (!file) throw std::runtime_error("update file flush failed");
                }
                Canceled(stop);
                if (std::filesystem::file_size(source_) != release_.size) throw std::runtime_error("truncated update");
                VerifyExecutable(source_, expectedHash_);
                Canceled(stop); status_ = Status::Ready;
            } catch (...) {
                Cleanup(); failed = !stop.stop_requested(); status_ = stop.stop_requested() ? Status::None : Status::Available;
            }
        });
    } catch (...) { failed = true; status_ = Status::Available; }
}
void Updater::Install() {
    if (State() != Status::Ready) return;
    if (worker_.joinable()) worker_.join();
    try { StartInstaller(CurrentExecutable(), source_, expectedHash_); handedOff_ = true; }
    catch (...) { Cleanup(); failed = true; status_ = Status::Available; throw; }
}
}
