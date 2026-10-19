#include "autoneg/model/edit_history.hpp"

namespace autoneg::model {

EditHistory::EditHistory(EditList initial)
{
    revisions_.push_back(Revision{nextSeq_++, std::move(initial), {}, {}, std::chrono::system_clock::now()});
}

const EditList& EditHistory::current() const noexcept
{
    return revisions_[cur_].list;
}

const Revision& EditHistory::currentRevision() const noexcept
{
    return revisions_[cur_];
}

Revision EditHistory::commit(EditList list, std::string label, UndoGroupId group)
{
    revisions_.resize(cur_ + 1); // Wiederholen-Zweig verwerfen
    revisions_.push_back(
        Revision{nextSeq_++, std::move(list), std::move(label), group, std::chrono::system_clock::now()});
    cur_ = revisions_.size() - 1;
    return revisions_[cur_];
}

std::optional<Revision> EditHistory::undo()
{
    if (!canUndo()) {
        return std::nullopt;
    }
    --cur_;
    return revisions_[cur_];
}

std::optional<Revision> EditHistory::redo()
{
    if (!canRedo()) {
        return std::nullopt;
    }
    ++cur_;
    return revisions_[cur_];
}

bool EditHistory::canUndo() const noexcept
{
    return cur_ > 0;
}

bool EditHistory::canRedo() const noexcept
{
    return cur_ + 1 < revisions_.size();
}

} // namespace autoneg::model
