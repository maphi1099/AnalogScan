#pragma once

#include "autoneg/base/bytes.hpp"
#include "autoneg/base/error.hpp"
#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/json_value.hpp"
#include "autoneg/model/roll.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace autoneg::ports {

/// Kennzeichen des Scans in der Begleitdatei (Kapitel 8.3/8.6).
struct SourceStamp
{
    std::string file; ///< Dateiname ohne Ordner
    std::uint64_t size = 0;
    std::int64_t mtime = 0; ///< Sekunden seit 1970 (UTC)
    model::FrameId hash;

    friend bool operator==(const SourceStamp&, const SourceStamp&) = default;
};

/// Inhalt einer Begleitdatei *.autoneg.json.
struct SidecarData
{
    SourceStamp source;
    int frameNo = 0;
    model::Rating rating = model::Rating::None;
    model::EditList edits;
    /// Unbekannte Felder und Einträge unbekannter Schritte, unverändert erhalten (NF-ARC-09).
    model::JsonValue extra;

    friend bool operator==(const SidecarData&, const SidecarData&) = default;
};

/// Begleitdatei ohne passenden Scan (nach Umbenennen, LF-DAT-06).
struct SidecarRef
{
    std::filesystem::path path;
    SourceStamp source;

    friend bool operator==(const SidecarRef&, const SidecarRef&) = default;
};

/// Rollendatei roll.autoneg.json. Adapter: JsonSidecarStore.
/// Schreibende Aufrufe nur aus dem Persistenz-Thread; lesen beliebig.
class IRollRepository
{
public:
    virtual ~IRollRepository() = default;

    /// Leer, wenn der Ordner keine Rollendatei hat.
    [[nodiscard]] virtual base::Expected<std::optional<model::Roll>>
    load(const std::filesystem::path& folder) = 0;
    [[nodiscard]] virtual base::Expected<void> save(const model::Roll& r) = 0;
};

/// Begleitdateien je Bild. Adapter: JsonSidecarStore.
/// Schreibende Aufrufe nur aus dem Persistenz-Thread; lesen beliebig.
class IEditRepository
{
public:
    virtual ~IEditRepository() = default;

    /// Leer, wenn das Bild keine Begleitdatei hat.
    [[nodiscard]] virtual base::Expected<std::optional<SidecarData>> load(const model::Frame& f) = 0;
    [[nodiscard]] virtual base::Expected<void> save(const model::Frame& f, const SidecarData& d) = 0;
    [[nodiscard]] virtual std::vector<SidecarRef> orphans(const std::filesystem::path& folder) = 0;
};

/// Arten von Vorlagen im Benutzerverzeichnis (presets/<Art>/<Name>.json).
enum class PresetKind
{
    Edit,
    Export,
    ContactSheet,
    Base,
    FlatField
};

struct PresetId
{
    PresetKind kind = PresetKind::Edit;
    std::string name;

    friend auto operator<=>(const PresetId&, const PresetId&) = default;
};

struct PresetInfo
{
    PresetId id;
    std::int64_t mtime = 0;

    friend bool operator==(const PresetInfo&, const PresetInfo&) = default;
};

/// Vorlagen (LF-BAT-07, LF-EXP-10, LF-KTA-10). Adapter: JsonConfigStore.
class IPresetRepository
{
public:
    virtual ~IPresetRepository() = default;

    [[nodiscard]] virtual std::vector<PresetInfo> list(PresetKind k) = 0;
    [[nodiscard]] virtual base::Expected<model::JsonValue> load(const PresetId& id) = 0;
    [[nodiscard]] virtual base::Expected<void> save(PresetKind k, const std::string& name,
                                                    const model::JsonValue& j) = 0;
    [[nodiscard]] virtual base::Expected<void> remove(const PresetId& id) = 0;
};

/// Programmeinstellungen settings.json (LF-DAT-04). Adapter: JsonConfigStore.
/// Nur im UI-Thread; gespeichert wird über den Persistenz-Thread.
class ISettingsStore
{
public:
    virtual ~ISettingsStore() = default;

    [[nodiscard]] virtual std::optional<model::JsonValue> get(const std::string& key) = 0;
    virtual void set(const std::string& key, const model::JsonValue& v) = 0;
};

/// Miniaturen-Cache (Kapitel 8.8). Adapter: FileThumbnailCache. Threadsicher.
class ICacheStore
{
public:
    virtual ~ICacheStore() = default;

    [[nodiscard]] virtual std::optional<base::Bytes> thumbnail(const model::FrameId& h, int size,
                                                               std::uint64_t editHash) = 0;
    virtual void put(const model::FrameId& h, int size, std::uint64_t editHash, const base::Bytes& jpeg) = 0;
};

} // namespace autoneg::ports
