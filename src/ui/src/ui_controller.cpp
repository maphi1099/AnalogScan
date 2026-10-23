#include "autoneg/ui/ui_controller.hpp"

#include <cstdio>
#include <type_traits>
#include <variant>

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
    const bool done = bus_ != nullptr && bus_->undo();
    if (done && preview_) {
        preview_(currentList());
    }
    return done;
}

bool UiController::redo()
{
    const bool done = bus_ != nullptr && bus_->redo();
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

namespace {

std::string number(double v, bool sign)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, sign ? "%+.2f" : "%.2f", v);
    std::string s = buf;
    for (char& c : s) {
        if (c == '.') {
            c = ','; // deutsche Schreibweise
        }
    }
    return s;
}

} // namespace

std::string describeParams(const model::EditEntry& e)
{
    std::string out;
    for (const auto& [name, value] : e.params.entries()) {
        std::string v = std::visit(
            [](const auto& x) -> std::string {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, double>) {
                    return number(x, true);
                } else if constexpr (std::is_same_v<T, int>) {
                    return std::to_string(x);
                } else if constexpr (std::is_same_v<T, bool>) {
                    return x ? "an" : "aus";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return x;
                } else if constexpr (std::is_same_v<T, model::NormRect>) {
                    return number(x.w, false) + "×" + number(x.h, false);
                } else if constexpr (std::is_same_v<T, model::SpotList>) {
                    return std::to_string(x.size()) + " Stellen";
                } else {
                    return "…";
                }
            },
            value);
        if (!out.empty()) {
            out += ", ";
        }
        out += name + " " + v;
    }
    return out;
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
