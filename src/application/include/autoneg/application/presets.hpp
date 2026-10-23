#pragma once

#include "autoneg/application/services.hpp"
#include "autoneg/layout/contact_sheet.hpp"
#include "autoneg/model/json_value.hpp"

#include <string>

namespace autoneg::application {

/// Vorlagen für Export (LF-EXP-10) und Kontaktabzug (LF-KTA-10) als JsonValue; die
/// Ablage übernimmt ports::IPresetRepository. Unbekannte oder fehlende Felder behalten
/// den Standardwert (Vorlagen älterer Versionen bleiben lesbar).
[[nodiscard]] model::JsonValue exportToJson(const ExportSettings& s);
[[nodiscard]] ExportSettings exportFromJson(const model::JsonValue& j);

struct SheetPreset
{
    layout::ContactSheetSpec spec;
    std::string format = "Kleinbild 24×36";
};
/// Rollenbezogene Angaben (Kopfzeile) gehören nicht in die Vorlage.
[[nodiscard]] model::JsonValue sheetToJson(const SheetPreset& p);
[[nodiscard]] SheetPreset sheetFromJson(const model::JsonValue& j);

} // namespace autoneg::application
