#pragma once

#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/param_set.hpp"

#include <cstdint>
#include <optional>

namespace autoneg::model {

/// Ein Eintrag der Bearbeitungsliste: genau eine Einstellung genau einer Stufe (LF-LST-02).
struct EditEntry
{
    StepId step;
    int version = 1; ///< Version des Schritts (NF-ARC-09)
    int rank = 0;    ///< Position in der Stufenfolge, aus dem StepDescriptor
    ParamSet params;
    bool enabled = true; ///< ausgeschaltet bleibt der Wert erhalten (LF-LST-06)
    Origin origin = Origin::Manual;
    std::optional<FrameId> copiedFrom; ///< bei Origin::Copied: Quellbild (LF-LST-05)

    [[nodiscard]] std::uint64_t hash() const noexcept;

    friend bool operator==(const EditEntry&, const EditEntry&) = default;
};

} // namespace autoneg::model
