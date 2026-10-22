#include "autoneg/pipeline/step_registry.hpp"

#include <algorithm>

namespace autoneg::pipeline {

base::Expected<void> StepRegistry::add(std::unique_ptr<IStep> s)
{
    if (s == nullptr) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Leerer Schritt");
    }
    const model::StepId& id = s->descriptor().id;
    if (find(id) != nullptr) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Schritt " + id.value() + " ist bereits registriert");
    }
    steps_.push_back(std::move(s));
    ordered_.clear();
    for (const auto& step : steps_) {
        ordered_.push_back(step.get());
    }
    std::ranges::sort(ordered_, [](const IStep* a, const IStep* b) {
        const auto& da = a->descriptor();
        const auto& db = b->descriptor();
        return da.rank != db.rank ? da.rank < db.rank : da.id < db.id;
    });
    return {};
}

const IStep* StepRegistry::find(const model::StepId& id) const noexcept
{
    const auto it = std::ranges::find_if(steps_, [&](const auto& s) { return s->descriptor().id == id; });
    return it == steps_.end() ? nullptr : it->get();
}

} // namespace autoneg::pipeline
