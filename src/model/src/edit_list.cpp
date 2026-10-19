#include "autoneg/model/edit_list.hpp"

#include "autoneg/base/hash.hpp"

#include <algorithm>

namespace autoneg::model {

namespace {

using Entries = std::vector<EditEntry>;

bool pipelineOrder(const EditEntry& a, const EditEntry& b) noexcept
{
    if (a.rank != b.rank) {
        return a.rank < b.rank;
    }
    return a.step < b.step;
}

/// Gemeinsame leere Liste. Konstant, daher kein globaler veränderlicher Zustand.
const std::shared_ptr<const Entries>& emptyEntries()
{
    static const std::shared_ptr<const Entries> empty = std::make_shared<const Entries>();
    return empty;
}

Entries::const_iterator findStep(const Entries& entries, const StepId& id)
{
    return std::ranges::find(entries, id, &EditEntry::step);
}

void insertSorted(Entries& entries, EditEntry entry)
{
    const auto pos = std::ranges::upper_bound(entries, entry, pipelineOrder);
    entries.insert(pos, std::move(entry));
}

} // namespace

EditList::EditList()
    : entries_(emptyEntries())
{}

EditList::EditList(std::shared_ptr<const std::vector<EditEntry>> entries)
    : entries_(std::move(entries))
{}

EditList EditList::with(EditEntry entry) const
{
    Entries next = *entries_;
    std::erase_if(next, [&](const EditEntry& e) { return e.step == entry.step; });
    insertSorted(next, std::move(entry));
    return EditList(std::make_shared<const Entries>(std::move(next)));
}

EditList EditList::without(const StepId& id) const
{
    if (findStep(*entries_, id) == entries_->end()) {
        return *this;
    }
    Entries next = *entries_;
    std::erase_if(next, [&](const EditEntry& e) { return e.step == id; });
    return EditList(std::make_shared<const Entries>(std::move(next)));
}

EditList EditList::enabled(const StepId& id, bool on) const
{
    const auto it = findStep(*entries_, id);
    if (it == entries_->end() || it->enabled == on) {
        return *this;
    }
    Entries next = *entries_;
    next[static_cast<std::size_t>(it - entries_->begin())].enabled = on;
    return EditList(std::make_shared<const Entries>(std::move(next)));
}

EditList EditList::merge(const EditList& src, const std::set<StepId>& selection) const
{
    EditList result = *this;
    for (const EditEntry& e : src.entries()) {
        if (selection.contains(e.step)) {
            result = result.with(e);
        }
    }
    return result;
}

const EditEntry* EditList::get(const StepId& id) const noexcept
{
    const auto it = findStep(*entries_, id);
    return it == entries_->end() ? nullptr : &*it;
}

std::uint64_t EditList::hash() const noexcept
{
    base::Hasher h;
    h.add(static_cast<std::uint64_t>(entries_->size()));
    for (const EditEntry& e : *entries_) {
        h.add(e.hash());
    }
    return h.value();
}

bool operator==(const EditList& a, const EditList& b) noexcept
{
    return a.entries_ == b.entries_ || *a.entries_ == *b.entries_;
}

} // namespace autoneg::model
