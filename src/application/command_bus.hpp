#pragma once

#include "application/commands.hpp"
#include "application/event_log.hpp"
#include "application/persistence_queue.hpp"
#include "model/edit_history.hpp"
#include "model/roll.hpp"
#include "ports/repositories.hpp"

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace autoneg::application {

/// Eine Rückgängig-Gruppe: alle Bilder, die ein Befehl geändert hat (LF-BAT-06).
struct UndoGroup
{
    model::UndoGroupId id;
    std::string label;
    std::vector<std::pair<model::FrameId, std::int64_t>> changes; ///< Bild, Revision
};

/// Rückgängig-Stapel über Gruppen (Architektur 9.12). Nur im UI-Thread.
class UndoStack
{
public:
    void push(UndoGroup g);
    [[nodiscard]] std::optional<UndoGroup> undo();
    [[nodiscard]] std::optional<UndoGroup> redo();
    [[nodiscard]] bool canUndo() const noexcept { return pos_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return pos_ < groups_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return groups_.size(); }

private:
    std::vector<UndoGroup> groups_;
    std::size_t pos_ = 0;
};

/// Beobachter von Änderungen (Oberfläche, Vorschau). Aufrufe im UI-Thread.
class ISessionObserver
{
public:
    virtual ~ISessionObserver() = default;
    virtual void onEditChanged(const model::FrameId& f, const model::Revision& rev) = 0;
};

/// Arbeitsstand der Oberfläche (Architektur 9.12). Nur im UI-Thread.
struct Session
{
    std::shared_ptr<const model::Roll> roll;
    model::FrameId current;
    std::vector<model::FrameId> selection;
};

/// Synchronisieren (LF-BAT-05): Änderungen am aktiven Bild wirken auf die ganze Auswahl.
class SyncController
{
public:
    void setEnabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    /// Ziele eines Befehls: bei Sync die Auswahl (mit dem aktiven Bild), sonst nur das aktive.
    [[nodiscard]] std::vector<model::FrameId> targets(const Session& s) const;

private:
    bool enabled_ = false;
};

/// Führt Befehle aus (Architektur 9.12, NF-ARC-10). Nur im UI-Thread.
///
/// Je Bild ein Verlauf (EditHistory); ein Befehl auf mehrere Bilder ist genau eine
/// Rückgängig-Gruppe. Jeder neue Stand geht sofort an die PersistenceQueue (LF-DAT-03).
class CommandBus
{
public:
    /// Protokoll: jeder Befehl, Rückgängig und Wiederholen; optional.
    void setLogger(ports::ILogger* l) { log_.set(l); }
    CommandBus(const pipeline::StepRegistry& registry, PersistenceQueue& queue)
        : registry_(registry)
        , queue_(queue)
    {}

    /// Bild aufnehmen: Ausgangsstand aus der Begleitdatei (oder Rollen-Standard).
    void open(const model::Frame& f, ports::SidecarData data);
    void close(const model::FrameId& f);

    /// Führt c auf allen Zielen aus; Bilder ohne Änderung erzeugen keine Revision.
    /// Liefert die Gruppe (leer, wenn sich nichts geändert hat).
    model::UndoGroupId execute(const ICommand& c, std::span<const model::FrameId> targets,
                               const model::Roll& roll, bool batch = false);
    bool undo();
    bool redo();
    /// Rückgängig nur für ein Bild (Entwickeln): sein letzter Schritt, egal wie er entstand.
    bool undoFrame(const model::FrameId& f);
    bool redoFrame(const model::FrameId& f);
    /// Rückgängig für Aktionen auf mehrere Bilder vom Leuchttisch (execute mit batch). Bilder,
    /// die danach einzeln weiterbearbeitet wurden, bleiben unverändert.
    bool undoBatch();
    bool redoBatch();
    [[nodiscard]] bool canUndoBatch() const noexcept { return batch_.canUndo(); }
    [[nodiscard]] bool canRedoBatch() const noexcept { return batch_.canRedo(); }
    /// Bewertung behalten/verwerfen (LF-BAT-10); wird sofort gespeichert, nicht im Verlauf.
    /// false, wenn das Bild nicht offen ist.
    bool setRating(const model::FrameId& f, model::Rating r);
    /// Sterne 0–5 setzen und speichern.
    bool setStars(const model::FrameId& f, int stars);
    [[nodiscard]] bool isOpen(const model::FrameId& f) const { return open_.contains(f); }

    [[nodiscard]] const model::EditHistory& history(const model::FrameId& f) const;
    [[nodiscard]] const UndoStack& undoStack() const noexcept { return undo_; }
    void subscribe(ISessionObserver* o);

private:
    struct Open
    {
        model::Frame frame;
        ports::SidecarData data; ///< Kennzeichen, Bewertung, Unbekanntes; edits = aktueller Stand
        model::EditHistory history;
    };

    void persist(Open& o);
    void notify(const model::FrameId& f, const model::Revision& rev);

    const pipeline::StepRegistry& registry_;
    PersistenceQueue& queue_;
    EventLog log_;
    std::map<model::FrameId, Open> open_;
    UndoStack undo_;
    UndoStack batch_; ///< nur Aktionen vom Leuchttisch
    std::vector<ISessionObserver*> observers_;
    std::uint64_t nextGroup_ = 1;
};

} // namespace autoneg::application
