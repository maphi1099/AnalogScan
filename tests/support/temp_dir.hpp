#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace autoneg::testing {

/// Eigenes temporäres Verzeichnis je Test; wird im Destruktor samt Inhalt gelöscht.
/// Tests, die schreiben, arbeiten nur hier (Testdaten bleiben unverändert, NF-REL-01).
class TempDir
{
public:
    TempDir()
    {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("autoneg-test-" + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace autoneg::testing
