#pragma once

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/adapter/json/json_codec.hpp"
#include "autoneg/adapter/json/schema_migrator.hpp"
#include "autoneg/ports/repositories.hpp"
#include "autoneg/ports/services.hpp"

#include <filesystem>
#include <functional>
#include <mutex>
#include <set>

namespace autoneg::adapter::json {

/// Begleit- und Rollendateien als JSON neben den Scans (Architektur 8.2–8.8, LF-DAT-01…06).
///
/// - Begleitdatei: <Scan mit voller Endung>.autoneg.json; Rollendatei roll.autoneg.json.
/// - Schreibgeschützter Ordner: Ersatzablage <fallbackDir>/<Prüfsumme>.autoneg.json bzw.
///   roll-<Ordnerkennung>.autoneg.json; beim Lesen hat die Datei neben dem Scan Vorrang.
/// - Zuordnung: Stimmen Größe und Änderungszeit, wird keine Prüfsumme berechnet; sonst
///   Prüfsumme: gleich → nur size/mtime aktualisiert, verschieden → Fehler Conflict.
/// - Defekte Datei → umbenannt nach *.autoneg.json.defekt, Fehler Corrupt.
/// - Neueres Schema → nur lesend; save meldet ReadOnly und schreibt nicht.
/// - Reste *.autoneg.json.tmp entfernt load(folder) der Rolle.
/// - Sperre .autoneg.lock mit Prozesskennung und Rechnername; verwaiste Sperren werden übernommen.
///
/// Lesen ist threadsicher; schreibende Aufrufe nur aus dem Persistenz-Thread.
class JsonSidecarStore final : public ports::IEditRepository, public ports::IRollRepository
{
public:
    /// writable: Prüfung, ob im Ordner geschrieben werden darf (Standard: Probe-Datei anlegen);
    /// austauschbar für Tests (U-DAT-09).
    using WritableProbe = std::function<bool(const std::filesystem::path&)>;

    JsonSidecarStore(ports::StepCatalog catalog, const AtomicFileWriter& writer,
                     const SchemaMigrator& migrator, ports::IContentHasher& hasher,
                     std::filesystem::path fallbackDir, WritableProbe writable = {});

    // IEditRepository
    [[nodiscard]] base::Expected<std::optional<ports::SidecarData>> load(const model::Frame& f) override;
    [[nodiscard]] base::Expected<void> save(const model::Frame& f, const ports::SidecarData& d) override;
    [[nodiscard]] std::vector<ports::SidecarRef> orphans(const std::filesystem::path& folder) override;
    [[nodiscard]] base::Expected<void> relink(const ports::SidecarRef& orphan,
                                              const model::Frame& f) override;

    // IRollRepository
    [[nodiscard]] base::Expected<std::optional<model::Roll>>
    load(const std::filesystem::path& folder) override;
    [[nodiscard]] base::Expected<void> save(const model::Roll& r) override;
    [[nodiscard]] base::Expected<bool> lock(const std::filesystem::path& folder) override;
    void unlock(const std::filesystem::path& folder) override;

    [[nodiscard]] static std::filesystem::path sidecarPath(const model::Frame& f);
    [[nodiscard]] bool writable(const std::filesystem::path& folder) const;

    /// Plattformhilfen (öffentlich für Tests): eigene Prozesskennung, Rechnername, Lebenszeichen.
    [[nodiscard]] static std::int64_t currentProcessId();
    [[nodiscard]] static std::string hostName();
    [[nodiscard]] static bool processAlive(std::int64_t pid);

private:
    [[nodiscard]] std::filesystem::path fallbackPath(const model::FrameId& hash) const;
    [[nodiscard]] std::filesystem::path rollPath(const std::filesystem::path& folder) const;
    [[nodiscard]] base::Expected<model::JsonValue> readJson(const std::filesystem::path& p);
    [[nodiscard]] base::Expected<model::FrameId> hashOf(const model::Frame& f);

    EditCodec codec_;
    const AtomicFileWriter& writer_;
    const SchemaMigrator& migrator_;
    ports::IContentHasher& hasher_;
    std::filesystem::path fallbackDir_;
    WritableProbe writable_;
    mutable std::mutex mutex_;
    std::set<std::filesystem::path> readOnly_; ///< Dateien mit neuerem Schema
};

} // namespace autoneg::adapter::json
