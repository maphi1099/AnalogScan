#pragma once

#include "autoneg/base/error.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>

namespace autoneg::adapter::json {

/// Schreibt Dateien atomar (Architektur 8.5, LF-DAT-03): temporäre Datei im selben Ordner
/// (<Ziel>.tmp), Daten auf den Datenträger erzwingen (fsync bzw. FlushFileBuffers), Ziel
/// atomar ersetzen (rename bzw. ReplaceFileW); unter Linux danach den Ordner synchronisieren.
/// Ein Abbruch in jedem Schritt hinterlässt den alten oder den neuen Stand, nie eine halbe
/// Datei. Übrig gebliebene .tmp-Dateien entfernt der Aufrufer beim nächsten Öffnen.
/// Threadsicher für verschiedene Zieldateien.
class AtomicFileWriter
{
public:
    /// Prüfpunkte für Fehlereinspeisung in Tests (U-DAT-06).
    enum class Stage
    {
        Written,      ///< temporäre Datei geschrieben
        Synced,       ///< temporäre Datei synchronisiert
        BeforeReplace ///< unmittelbar vor dem Ersetzen
    };
    /// Liefert true, wenn an diesem Punkt ein Absturz simuliert werden soll (Abbruch ohne
    /// Aufräumen, wie bei einem echten Absturz).
    using FaultHook = std::function<bool(Stage)>;

    AtomicFileWriter() = default;
    explicit AtomicFileWriter(FaultHook hook)
        : hook_(std::move(hook))
    {}

    [[nodiscard]] base::Expected<void> write(const std::filesystem::path& p,
                                             std::span<const std::byte> bytes) const;
    [[nodiscard]] base::Expected<void> write(const std::filesystem::path& p, std::string_view text) const;

    [[nodiscard]] static std::filesystem::path tempPath(const std::filesystem::path& p);

private:
    FaultHook hook_;
};

} // namespace autoneg::adapter::json
