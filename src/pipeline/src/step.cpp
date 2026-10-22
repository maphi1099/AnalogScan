#include "autoneg/pipeline/step.hpp"

#include <type_traits>
#include <variant>

namespace autoneg::pipeline {

namespace {

bool typeMatches(ParamType t, const model::ParamValue& v)
{
    switch (t) {
    case ParamType::Double:
        return std::holds_alternative<double>(v);
    case ParamType::Int:
        return std::holds_alternative<int>(v);
    case ParamType::Bool:
        return std::holds_alternative<bool>(v);
    case ParamType::String:
        return std::holds_alternative<std::string>(v);
    case ParamType::Vec3:
        return std::holds_alternative<base::Vec3>(v);
    case ParamType::Rect:
        return std::holds_alternative<model::NormRect>(v);
    case ParamType::Curve:
        return std::holds_alternative<model::Curve>(v);
    case ParamType::Spots:
        return std::holds_alternative<model::SpotList>(v);
    }
    return false;
}

} // namespace

bool IStep::isNeutral(const model::ParamSet& p) const
{
    return paramsNeutral(descriptor(), p);
}

base::Expected<void> IStep::validate(const model::ParamSet& p) const
{
    return validateParams(descriptor(), p);
}

base::Expected<void> validateParams(const StepDescriptor& d, const model::ParamSet& p)
{
    for (const ParamSpec& spec : d.params) {
        const model::ParamValue* v = p.find(spec.name);
        if (v == nullptr) {
            continue;
        }
        if (!typeMatches(spec.type, *v)) {
            return base::makeError(base::ErrorCode::TypeMismatch,
                                   d.id.value() + ": Parameter '" + spec.name + "' hat den falschen Typ");
        }
        if (spec.min < spec.max) {
            double value = 0.0;
            if (const auto* dv = std::get_if<double>(v)) {
                value = *dv;
            } else if (const auto* iv = std::get_if<int>(v)) {
                value = *iv;
            } else {
                continue;
            }
            if (!(value >= spec.min && value <= spec.max)) { // auch NaN abweisen
                return base::makeError(base::ErrorCode::InvalidArgument,
                                       d.id.value() + ": Parameter '" + spec.name +
                                           "' liegt außerhalb des Bereichs");
            }
        }
    }
    return {};
}

bool paramsNeutral(const StepDescriptor& d, const model::ParamSet& p)
{
    for (const ParamSpec& spec : d.params) {
        const model::ParamValue* v = p.find(spec.name);
        if (v != nullptr && *v != spec.neutral) {
            return false;
        }
    }
    return true;
}

} // namespace autoneg::pipeline
