#include "application/command_bus.hpp"

#include <algorithm>
#include <stdexcept>

namespace autoneg::application {

void UndoStack::push(UndoGroup g)
{
    groups_.resize(pos_); // Wiederholen-Zweig verwerfen
    groups_.push_back(std::move(g));
    pos_ = groups_.size();
}

std::optional<UndoGroup> UndoStack::undo()
{
    if (pos_ == 0) {
        return std::nullopt;
    }
    return groups_[--pos_];
}

std::optional<UndoGroup> UndoStack::redo()
{
    if (pos_ >= groups_.size()) {
        return std::nullopt;
    }
    return groups_[pos_++];
}

std::vector<model::FrameId> SyncController::targets(const Session& s) const
{
    if (!enabled_ || s.selection.empty()) {
        return {s.current};
    }
    std::vector<model::FrameId> out = s.selection;
    if (std::ranges::find(out, s.current) == out.end()) {
        out.insert(out.begin(), s.current);
    }
    return out;
}

void CommandBus::open(const model::Frame& f, ports::SidecarData data)
{
    model::EditHistory history(data.edits);
    open_.insert_or_assign(f.id, Open{f, std::move(data), std::move(history)});
}

void CommandBus::close(const model::FrameId& f)
{
    open_.erase(f);
}

const model::EditHistory& CommandBus::history(const model::FrameId& f) const
{
    const auto it = open_.find(f);
    if (it == open_.end()) {
        static const model::EditHistory empty;
        return empty;
    }
    return it->second.history;
}

void CommandBus::subscribe(ISessionObserver* o)
{
    observers_.push_back(o);
}

bool CommandBus::setRating(const model::FrameId& f, model::Rating r)
{
    const auto it = open_.find(f);
    if (it == open_.end()) {
        return false;
    }
    if (it->second.data.rating != r) {
        it->second.data.rating = r;
        it->second.frame.rating = r;
        persist(it->second);
    }
    return true;
}

bool CommandBus::setStars(const model::FrameId& f, int stars)
{
    const auto it = open_.find(f);
    if (it == open_.end()) {
        return false;
    }
    stars = std::clamp(stars, 0, 5);
    if (it->second.data.stars != stars) {
        it->second.data.stars = stars;
        it->second.frame.stars = stars;
        persist(it->second);
    }
    return true;
}

void CommandBus::persist(Open& o)
{
    o.data.edits = o.history.current();
    queue_.enqueue(o.frame, o.data);
}

void CommandBus::notify(const model::FrameId& f, const model::Revision& rev)
{
    for (ISessionObserver* o : observers_) {
        o->onEditChanged(f, rev);
    }
}

model::UndoGroupId CommandBus::execute(const ICommand& c, std::span<const model::FrameId> targets,
                                       const model::Roll& roll, bool batch)
{
    UndoGroup group;
    group.id = model::UndoGroupId{nextGroup_};
    group.label = c.label();
    for (const model::FrameId& f : targets) {
        const auto it = open_.find(f);
        if (it == open_.end()) {
            continue;
        }
        Open& o = it->second;
        const CommandContext ctx{f, roll, registry_};
        model::EditList next = c.apply(o.history.current(), ctx);
        if (next == o.history.current()) {
            continue; // keine Änderung, keine Revision
        }
        const model::Revision rev = o.history.commit(std::move(next), group.label, group.id);
        group.changes.emplace_back(f, rev.seq);
        persist(o);
        notify(f, rev);
    }
    if (log_.enabled()) {
        std::string frames;
        for (const auto& [f, seq] : group.changes) {
            const auto it = open_.find(f);
            frames += (frames.empty() ? "" : ", ") +
                      (it != open_.end() ? it->second.frame.path.filename().string() : std::string("?"));
        }
        log_.info("Befehl \"" + group.label + "\"" + (batch ? " (Stapel)" : "") + ": " +
                  (group.changes.empty() ? std::string("keine Änderung") : frames));
    }
    if (group.changes.empty()) {
        return {};
    }
    ++nextGroup_;
    const model::UndoGroupId id = group.id;
    if (batch) {
        batch_.push(group);
    }
    undo_.push(std::move(group));
    return id;
}

bool CommandBus::undoFrame(const model::FrameId& f)
{
    log_.info("Rückgängig (aktives Bild)");
    const auto it = open_.find(f);
    if (it == open_.end()) {
        return false;
    }
    const auto rev = it->second.history.undo();
    if (rev) {
        persist(it->second);
        notify(f, *rev);
    }
    return rev.has_value();
}

bool CommandBus::redoFrame(const model::FrameId& f)
{
    log_.info("Wiederholen (aktives Bild)");
    const auto it = open_.find(f);
    if (it == open_.end()) {
        return false;
    }
    const auto rev = it->second.history.redo();
    if (rev) {
        persist(it->second);
        notify(f, *rev);
    }
    return rev.has_value();
}

bool CommandBus::undoBatch()
{
    log_.info("Rückgängig (Leuchttisch)");
    const auto g = batch_.undo();
    if (!g) {
        return false;
    }
    for (const auto& [f, seq] : g->changes) {
        const auto it = open_.find(f);
        // nur, wenn das Bild seitdem nicht einzeln geändert wurde
        if (it == open_.end() || it->second.history.currentRevision().seq != seq) {
            continue;
        }
        if (const auto rev = it->second.history.undo()) {
            persist(it->second);
            notify(f, *rev);
        }
    }
    return true;
}

bool CommandBus::redoBatch()
{
    log_.info("Wiederholen (Leuchttisch)");
    const auto g = batch_.redo();
    if (!g) {
        return false;
    }
    for (const auto& [f, seq] : g->changes) {
        const auto it = open_.find(f);
        if (it == open_.end() || !it->second.history.canRedo()) {
            continue;
        }
        const auto rev = it->second.history.redo();
        if (!rev) {
            continue;
        }
        if (rev->seq != seq) {
            (void)it->second.history.undo(); // anderer Schritt: zurück
            continue;
        }
        persist(it->second);
        notify(f, *rev);
    }
    return true;
}

bool CommandBus::undo()
{
    log_.info("Rückgängig");
    const auto g = undo_.undo();
    if (!g) {
        return false;
    }
    for (const auto& [f, seq] : g->changes) {
        const auto it = open_.find(f);
        if (it == open_.end()) {
            continue;
        }
        if (const auto rev = it->second.history.undo()) {
            persist(it->second);
            notify(f, *rev);
        }
    }
    return true;
}

bool CommandBus::redo()
{
    log_.info("Wiederholen");
    const auto g = undo_.redo();
    if (!g) {
        return false;
    }
    for (const auto& [f, seq] : g->changes) {
        const auto it = open_.find(f);
        if (it == open_.end()) {
            continue;
        }
        if (const auto rev = it->second.history.redo()) {
            persist(it->second);
            notify(f, *rev);
        }
    }
    return true;
}

} // namespace autoneg::application
