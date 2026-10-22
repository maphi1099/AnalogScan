#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/pipeline/step.hpp"

#include <memory>
#include <span>
#include <vector>

namespace autoneg::pipeline {

/// Verzeichnis aller Verarbeitungsschritte (NF-ARC-04). Die Pipeline kennt Schritte nur
/// über die Registry. Wird beim Start gefüllt und danach nur noch gelesen (threadsicher lesbar).
class StepRegistry
{
public:
    /// Fügt einen Schritt hinzu; Fehler bei doppelter Kennung (U-PIP-01).
    base::Expected<void> add(std::unique_ptr<IStep> s);

    [[nodiscard]] const IStep* find(const model::StepId& id) const noexcept;
    /// Alle Schritte in Pipeline-Reihenfolge (Rang, bei Gleichstand Kennung).
    [[nodiscard]] std::span<const IStep* const> ordered() const noexcept { return ordered_; }
    [[nodiscard]] std::size_t size() const noexcept { return steps_.size(); }

private:
    std::vector<std::unique_ptr<IStep>> steps_;
    std::vector<const IStep*> ordered_;
};

} // namespace autoneg::pipeline
