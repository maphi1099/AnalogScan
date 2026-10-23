#pragma once

#include "autoneg/ports/services.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace autoneg::adapter::spdlog {

/// Protokoll in eine Datei mit Rotation (NF-REL-03, Architektur 9.11): Zeit, Ebene, Text;
/// beim Überschreiten von maxBytes wird die Datei umbenannt (autoneg.1.log …), höchstens
/// maxFiles alte Dateien bleiben. Threadsicher.
class SpdLogger final : public ports::ILogger
{
public:
    static constexpr std::size_t defaultMaxBytes = std::size_t{5} << 20U;
    static constexpr std::size_t defaultMaxFiles = 3;

    explicit SpdLogger(const std::filesystem::path& file, std::size_t maxBytes = defaultMaxBytes,
                       std::size_t maxFiles = defaultMaxFiles,
                       ports::LogLevel minimum = ports::LogLevel::Info);
    ~SpdLogger() override;
    SpdLogger(const SpdLogger&) = delete;
    SpdLogger& operator=(const SpdLogger&) = delete;

    void log(ports::LogLevel l, std::string_view msg) override;
    /// Schreibt gepufferte Einträge sofort (Warnungen und Fehler werden immer sofort geschrieben).
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace autoneg::adapter::spdlog
