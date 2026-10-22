#pragma once

// Hilfen zum Anlegen der StepDescriptoren (nur innerhalb von autoneg::steps).

#include "autoneg/pipeline/step.hpp"
#include "autoneg/steps/step_ids.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace autoneg::steps {

struct Options
{
    bool visible = true;
    bool alwaysActive = false;
    bool cacheAfter = false;
};

inline pipeline::StepDescriptor describe(std::string_view stepId, int stepRank, std::string name,
                                         pipeline::Phase phase, std::vector<pipeline::ParamSpec> params,
                                         pipeline::FilmTypeMask films = pipeline::FilmTypeMask::all(),
                                         Options o = {})
{
    pipeline::StepDescriptor d;
    d.id = model::StepId(std::string(stepId));
    d.version = 1;
    d.rank = stepRank;
    d.name = std::move(name);
    d.phase = phase;
    d.params = std::move(params);
    d.films = films;
    d.visible = o.visible;
    d.alwaysActive = o.alwaysActive;
    d.cacheAfter = o.cacheAfter;
    return d;
}

inline pipeline::ParamSpec doubleParam(std::string name, double neutral, double min, double max,
                                       std::string unit)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Double, neutral, min, max,
                               std::move(unit)};
}

inline pipeline::ParamSpec intParam(std::string name, int neutral, int min, int max)
{
    return pipeline::ParamSpec{std::move(name),          pipeline::ParamType::Int, neutral,
                               static_cast<double>(min), static_cast<double>(max), ""};
}

inline pipeline::ParamSpec boolParam(std::string name, bool neutral)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Bool, neutral, 0, 0, ""};
}

inline pipeline::ParamSpec stringParam(std::string name, std::string neutral)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::String, std::move(neutral), 0, 0, ""};
}

inline pipeline::ParamSpec vec3Param(std::string name, base::Vec3 neutral)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Vec3, neutral, 0, 0, ""};
}

inline pipeline::ParamSpec rectParam(std::string name)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Rect, model::NormRect::full(), 0, 0, ""};
}

inline pipeline::ParamSpec curveParam(std::string name)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Curve, model::Curve{}, 0, 0, ""};
}

inline pipeline::ParamSpec spotsParam(std::string name)
{
    return pipeline::ParamSpec{std::move(name), pipeline::ParamType::Spots, model::SpotList{}, 0, 0, ""};
}

} // namespace autoneg::steps
