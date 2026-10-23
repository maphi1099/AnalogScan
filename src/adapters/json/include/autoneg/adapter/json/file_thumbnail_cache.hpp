#pragma once

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/ports/repositories.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>

namespace autoneg::adapter::json {

/// Miniaturen-Cache im Benutzerverzeichnis (Architektur 8.8, 9.11): eine JPEG-Datei je
/// Miniatur, Dateiname aus Prüfsumme des Scans, Größenklasse und Prüfsumme der
/// Bearbeitungsliste (<hash>_<size>_<editHash>.jpg). Jederzeit löschbar.
/// Oberhalb der Grenze (Standard 2 GB) werden die am längsten nicht benutzten Dateien entfernt.
/// Threadsicher.
class FileThumbnailCache final : public ports::ICacheStore
{
public:
    static constexpr std::uintmax_t defaultLimit = 2ULL * 1024 * 1024 * 1024;

    explicit FileThumbnailCache(std::filesystem::path dir, std::uintmax_t limit = defaultLimit,
                                AtomicFileWriter writer = {});

    [[nodiscard]] std::optional<base::Bytes> thumbnail(const model::FrameId& h, int size,
                                                       std::uint64_t editHash) override;
    void put(const model::FrameId& h, int size, std::uint64_t editHash, const base::Bytes& jpeg) override;

    /// Belegter Platz in Byte (für Tests und Einstellungen).
    [[nodiscard]] std::uintmax_t usage() const;
    [[nodiscard]] static std::string fileName(const model::FrameId& h, int size, std::uint64_t editHash);

private:
    struct Entry
    {
        std::uintmax_t bytes = 0;
        std::uint64_t lastUse = 0; ///< logischer Zeitstempel, größer = jünger
    };
    void evictLru(); ///< unter mtx_

    std::filesystem::path dir_;
    std::uintmax_t limit_;
    AtomicFileWriter writer_;
    mutable std::mutex mtx_;
    std::map<std::string, Entry> entries_;
    std::uintmax_t total_ = 0;
    std::uint64_t clock_ = 0;
};

} // namespace autoneg::adapter::json
