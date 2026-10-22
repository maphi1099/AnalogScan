#include "autoneg/application/command_bus.hpp"

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
                                       const model::Roll& roll)
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
    if (group.changes.empty()) {
        return {};
    }
    ++nextGroup_;
    const model::UndoGroupId id = group.id;
    undo_.push(std::move(group));
    return id;
}

bool CommandBus::undo()
{
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
