#pragma once

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/ports/repositories.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace autoneg::adapter::json {

/// Programmeinstellungen (settings.json) und Vorlagen (presets/<Art>/<Name>.json) im
/// Benutzerverzeichnis (Architektur 8.2, LF-DAT-04, LF-BAT-07, LF-EXP-10, LF-KTA-10).
///
/// Einstellungen: Standardwerte kommen vom Programm und werden nicht in die Datei geschrieben;
/// ein auf den Standard gesetzter Wert verschwindet aus der Datei. Eine defekte settings.json
/// wird nach settings.json.defekt umbenannt; es gelten die Standardwerte (U-DAT-05).
/// Jede Änderung wird sofort geschrieben (AtomicFileWriter). Threadsicher.
class JsonConfigStore final : public ports::ISettingsStore, public ports::IPresetRepository
{
public:
    JsonConfigStore(std::filesystem::path root, const AtomicFileWriter& writer,
                    std::map<std::string, model::JsonValue> defaults);

    /// Meldung beim Laden (z. B. defekte Datei); leer, wenn alles in Ordnung war.
    [[nodiscard]] const std::string& loadMessage() const noexcept { return loadMessage_; }

    // ISettingsStore
    [[nodiscard]] std::optional<model::JsonValue> get(const std::string& key) override;
    void set(const std::string& key, const model::JsonValue& v) override;

    // IPresetRepository
    [[nodiscard]] std::vector<ports::PresetInfo> list(ports::PresetKind k) override;
    [[nodiscard]] base::Expected<model::JsonValue> load(const ports::PresetId& id) override;
    [[nodiscard]] base::Expected<void> save(ports::PresetKind k, const std::string& name,
                                            const model::JsonValue& j) override;
    [[nodiscard]] base::Expected<void> remove(const ports::PresetId& id) override;

private:
    [[nodiscard]] std::filesystem::path presetPath(ports::PresetKind k, const std::string& name) const;
    void persist(); ///< unter mutex_

    std::filesystem::path root_;
    const AtomicFileWriter& writer_;
    std::map<std::string, model::JsonValue> defaults_;
    std::mutex mutex_;
    model::JsonValue settings_; ///< nur Abweichungen vom Standard
    std::string loadMessage_;
};

} // namespace autoneg::adapter::json
