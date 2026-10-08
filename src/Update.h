#pragma once
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

namespace update {
inline constexpr char Repository[] = "Laynholt/KeyboardCleaner";
struct Release {
    std::string version, exeUrl, sumsUrl;
    std::uint64_t size{};
    explicit operator bool() const { return !exeUrl.empty() && !sumsUrl.empty(); }
};
bool IsNewer(std::string_view candidate, std::string_view current);
Release ParseRelease(const std::string& json, std::string_view current);
std::string ChecksumForExe(const std::string& sums);
std::string FileSha256(const std::filesystem::path& path);
void VerifyExecutable(const std::filesystem::path& path, const std::string& expectedHash);
std::filesystem::path CurrentExecutable();
void StartInstaller(const std::filesystem::path& target, const std::filesystem::path& source,
                    const std::string& expectedHash);

class Updater {
public:
    enum class Status { Checking, None, Available, Downloading, Ready };
    ~Updater();
    void Check();
    void Download();
    void Install();
    Status State() const { return status_.load(); }
    const std::string& Version() const { return release_.version; }
    std::atomic<unsigned> progress{};
    std::atomic<bool> failed{};
private:
    std::atomic<Status> status_{Status::None};
    std::jthread worker_;
    Release release_;
    std::filesystem::path job_, source_;
    std::string expectedHash_;
    bool handedOff_{};
    void Cleanup();
};
}
