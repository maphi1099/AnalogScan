#pragma once

#include "autoneg/application/jobs.hpp"
#include "autoneg/model/roll.hpp"
#include "autoneg/ports/repositories.hpp"
#include "autoneg/ports/services.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace autoneg::application {

/// Ein Bild der geöffneten Rolle mit seinem gespeicherten Stand.
struct OpenedFrame
{
    model::Frame frame;
    ports::SidecarData data;
    /// Problem beim Lesen der Begleitdatei (Corrupt, Conflict …): das Bild startet mit den
    /// Standardwerten der Rolle, die Oberfläche meldet es (LF-DAT-06, NF-REL-02).
    std::optional<base::Error> problem;
    /// true: Stand aus der Begleitdatei; false: neu, Einträge aus dem Rollen-Standard
    /// (Herkunft RollDefault).
    bool stored = false;
};

struct OpenedRoll
{
    model::Roll roll;
    std::vector<OpenedFrame> frames; ///< in Aufnahmereihenfolge (Dateiname)
    /// Eine andere laufende Instanz hält die Rolle: nur lesend öffnen (Architektur 8.8).
    bool readOnly = false;
    /// Begleitdateien ohne Scan, die keinem Bild zugeordnet werden konnten (LF-DAT-06).
    std::vector<ports::SidecarRef> orphans;
};

/// Öffnet einen Scan-Ordner als Rolle (Architektur 9.13, LF-IMP-07, LF-DAT-02, LF-DAT-06).
///
/// Unterstützte Dateien nach Endung (RAW-Formate aus LF-IMP-01, TIFF, JPEG), sortiert nach
/// Dateiname. Je Bild Prüfsumme (FrameId) und Begleitdatei; verwaiste Begleitdateien werden
/// über die Prüfsumme umbenannten Scans zugeordnet. Die Rollendatei liefert Film und
/// Rollen-Standard; die Bildliste ergibt sich aus dem Ordner.
class RollService
{
public:
    RollService(JobScheduler& jobs, ports::IRollRepository& rolls, ports::IEditRepository& edits,
                ports::IContentHasher& hasher)
        : jobs_(jobs)
        , rolls_(rolls)
        , edits_(edits)
        , hasher_(hasher)
    {}

    [[nodiscard]] static bool isScanFile(const std::filesystem::path& p);
    /// Unterstützte Dateien des Ordners, sortiert nach Dateiname (schnell, ohne Lesen).
    [[nodiscard]] static std::vector<std::filesystem::path> scanFiles(const std::filesystem::path& folder);

    /// Öffnet synchron (für Jobs und die Kommandozeile).
    [[nodiscard]] base::Expected<OpenedRoll> open(const std::filesystem::path& folder,
                                                  const image::CancelToken& cancel = {},
                                                  const std::function<void(int, int)>& progress = {});
    /// Öffnet im Hintergrund; done wird im Job-Thread aufgerufen.
    JobHandle openFolder(std::filesystem::path folder, std::function<void(base::Expected<OpenedRoll>)> done);

    /// Gibt die Sperre der Rolle frei (beim Schließen).
    void close(const std::filesystem::path& folder) { rolls_.unlock(folder); }

private:
    JobScheduler& jobs_;
    ports::IRollRepository& rolls_;
    ports::IEditRepository& edits_;
    ports::IContentHasher& hasher_;
};

} // namespace autoneg::application
