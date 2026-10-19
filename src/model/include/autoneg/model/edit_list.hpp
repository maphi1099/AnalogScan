#pragma once

#include "autoneg/model/edit_entry.hpp"
#include "autoneg/model/ids.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace autoneg::model {

/// Unveränderliche Bearbeitungsliste eines Bildes (AE-02).
///
/// Jede Schrittart kommt höchstens einmal vor (LF-LST-02). Die Einträge stehen immer in
/// Pipeline-Reihenfolge, sortiert nach Rang und bei gleichem Rang nach Kennung (LF-LST-04).
/// Änderungen liefern eine neue Liste; Kopien teilen sich den Speicher und dürfen ohne
/// Sperre zwischen Threads weitergegeben werden.
class EditList
{
public:
    EditList();

    /// Neuer Eintrag oder Ersatz des Eintrags derselben Schrittart.
    [[nodiscard]] EditList with(EditEntry entry) const;
    /// Ohne den Eintrag; unverändert, wenn er fehlt.
    [[nodiscard]] EditList without(const StepId& id) const;
    /// Eintrag ein- oder ausschalten, Werte bleiben erhalten; unverändert, wenn er fehlt.
    [[nodiscard]] EditList enabled(const StepId& id, bool on) const;
    /// Übernimmt aus src die Einträge der gewählten Schrittarten (LF-BAT-02/-03).
    /// Nicht gewählte Einträge bleiben unverändert; gewählte, die in src fehlen, ebenso.
    [[nodiscard]] EditList merge(const EditList& src, const std::set<StepId>& selection) const;

    [[nodiscard]] const EditEntry* get(const StepId& id) const noexcept;
    [[nodiscard]] std::span<const EditEntry> entries() const noexcept { return *entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_->size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_->empty(); }

    /// Hängt nur vom Inhalt ab (U-MOD-07).
    [[nodiscard]] std::uint64_t hash() const noexcept;

    friend bool operator==(const EditList& a, const EditList& b) noexcept;

private:
    explicit EditList(std::shared_ptr<const std::vector<EditEntry>> entries);

    std::shared_ptr<const std::vector<EditEntry>> entries_;
};

} // namespace autoneg::model
