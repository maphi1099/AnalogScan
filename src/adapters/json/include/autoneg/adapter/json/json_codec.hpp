#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/json_value.hpp"
#include "autoneg/model/roll.hpp"
#include "autoneg/ports/repositories.hpp"

#include <string>

namespace autoneg::adapter::json {

/// Text <-> JsonValue (nlohmann/json, Reihenfolge der Felder bleibt erhalten).
[[nodiscard]] base::Expected<model::JsonValue> parse(std::string_view text);
[[nodiscard]] std::string dump(const model::JsonValue& j);

/// Bearbeitungsliste <-> JSON-Feld "edits" (Architektur 8.3) über den Schrittkatalog.
/// Einträge unbekannter Schritte (oder einer neueren Version) bleiben als JSON erhalten.
class EditCodec
{
public:
    explicit EditCodec(ports::StepCatalog catalog)
        : catalog_(std::move(catalog))
    {}

    struct Decoded
    {
        model::EditList edits;
        model::JsonValue::Array unknown; ///< Einträge, die nicht gelesen werden konnten
    };

    [[nodiscard]] Decoded decode(const model::JsonValue& edits) const;
    [[nodiscard]] model::JsonValue encode(const model::EditList& edits,
                                          const model::JsonValue::Array& unknown) const;

    /// Begleitdatei (ohne Migration; Feld "schema" wird geschrieben). Unbekannte Felder
    /// und Einträge landen in SidecarData::extra (Objekt; unbekannte Einträge unter "edits").
    [[nodiscard]] base::Expected<ports::SidecarData> sidecarFromJson(const model::JsonValue& j) const;
    [[nodiscard]] model::JsonValue sidecarToJson(const ports::SidecarData& d, int schema) const;

    /// Rollendatei (Architektur 8.4); unbekannte Felder in Roll::extra.
    [[nodiscard]] base::Expected<model::Roll> rollFromJson(const model::JsonValue& j) const;
    [[nodiscard]] model::JsonValue rollToJson(const model::Roll& r, int schema) const;

private:
    [[nodiscard]] const ports::StepSchema* find(std::string_view id) const;

    ports::StepCatalog catalog_;
};

} // namespace autoneg::adapter::json
