#include "ui/ui_controller.hpp"

#include <type_traits>

namespace autoneg::ui {

bool UiController::apply(std::unique_ptr<application::ICommand> c)
{
    if (bus_ == nullptr || !session_->roll || c == nullptr) {
        return false;
    }
    const auto targets = sync_->targets(*session_);
    if (targets.empty()) {
        return false;
    }
    const model::UndoGroupId g = bus_->execute(*c, targets, *session_->roll);
    if (preview_) {
        preview_(currentList());
    }
    return g != model::UndoGroupId{};
}

void UiController::preview(const model::EditList& l)
{
    if (preview_) {
        preview_(l);
    }
}

model::EditList UiController::currentList() const
{
    if (bus_ == nullptr || session_->current == model::FrameId{}) {
        return {};
    }
    return bus_->history(session_->current).current();
}

bool UiController::undo()
{
    const bool done =
        bus_ != nullptr && (batchScope_ ? bus_->undoBatch() : bus_->undoFrame(session_->current));
    if (done && preview_) {
        preview_(currentList());
    }
    return done;
}

bool UiController::redo()
{
    const bool done =
        bus_ != nullptr && (batchScope_ ? bus_->redoBatch() : bus_->redoFrame(session_->current));
    if (done && preview_) {
        preview_(currentList());
    }
    return done;
}

std::string stepName(const pipeline::StepRegistry& r, const model::StepId& id)
{
    if (const pipeline::IStep* s = r.find(id)) {
        return s->descriptor().name;
    }
    return id.value();
}

std::string originName(model::Origin o)
{
    switch (o) {
    case model::Origin::Manual:
        return "manuell";
    case model::Origin::Automatic:
        return "automatisch";
    case model::Origin::Copied:
        return "kopiert";
    case model::Origin::RollDefault:
        return "Rollen-Standard";
    case model::Origin::Preset:
        return "Vorlage";
    }
    return "?";
}

} // namespace autoneg::ui
