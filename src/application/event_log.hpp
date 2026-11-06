#pragma once

#include "ports/services.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>

namespace autoneg::application {

/// Optionales Protokoll interner Ereignisse (Aufträge, Befehle, Rendern, Speichern). Der Logger
/// wird nach dem Aufbau gesetzt (setLogger) und von beliebigen Threads gelesen; ohne Logger
/// (Tests) wird nichts protokolliert. Uhrzeit und Thread schreibt der Logger selbst.
class EventLog
{
public:
    void set(ports::ILogger* l) { logger_.store(l); }
    void info(std::string_view msg) const { write(ports::LogLevel::Info, msg); }
    void warning(std::string_view msg) const { write(ports::LogLevel::Warning, msg); }
    void error(std::string_view msg) const { write(ports::LogLevel::Error, msg); }
    [[nodiscard]] bool enabled() const { return logger_.load() != nullptr; }

    /// Millisekunden seit start (für Laufzeiten im Protokoll).
    [[nodiscard]] static long long msSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();
    }

private:
    void write(ports::LogLevel lvl, std::string_view msg) const
    {
        if (ports::ILogger* l = logger_.load()) {
            l->log(lvl, msg);
        }
    }
    std::atomic<ports::ILogger*> logger_{nullptr};
};

} // namespace autoneg::application
