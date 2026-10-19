#pragma once

#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/ids.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace autoneg::model {

/// Ein Stand im Verlauf eines Bildes.
struct Revision
{
    std::int64_t seq = 0; ///< fortlaufend, auch nach verworfenem Wiederholen-Zweig eindeutig
    EditList list;
    std::string label;
    UndoGroupId group;
    std::chrono::system_clock::time_point time;
};

/// Rückgängig-Verlauf eines Bildes (LF-EDT-15). Unbegrenzt, nur im Arbeitsspeicher (AE-16).
///
/// Einzige veränderliche Klasse des Pakets; gehört ausschließlich dem UI-Thread und ist
/// nicht threadsicher.
class EditHistory
{
public:
    /// Verlauf mit dem Ausgangsstand als erster Revision.
    explicit EditHistory(EditList initial = {});

    [[nodiscard]] const EditList& current() const noexcept;
    [[nodiscard]] const Revision& currentRevision() const noexcept;

    /// Neuer Stand; ein vorhandener Wiederholen-Zweig wird verworfen.
    Revision commit(EditList list, std::string label, UndoGroupId group = {});
    /// Einen Schritt zurück; liefert den nun aktuellen Stand.
    std::optional<Revision> undo();
    /// Einen Schritt vor; liefert den nun aktuellen Stand.
    std::optional<Revision> redo();

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

    /// Anzahl der Revisionen einschließlich Ausgangsstand und Wiederholen-Zweig.
    [[nodiscard]] std::size_t size() const noexcept { return revisions_.size(); }
    /// Index des aktuellen Stands.
    [[nodiscard]] std::size_t position() const noexcept { return cur_; }

private:
    std::vector<Revision> revisions_;
    std::size_t cur_ = 0;
    std::int64_t nextSeq_ = 0;
};

} // namespace autoneg::model
