#include "autoneg/steps/registry.hpp"

#include "autoneg/steps/pointwise_steps.hpp"

#include <memory>

namespace autoneg::steps {

base::Expected<void> registerStandardSteps(pipeline::StepRegistry& registry, FlatFieldCache& flatFields)
{
    std::unique_ptr<pipeline::IStep> all[] = {
        std::make_unique<FlatFieldStep>(flatFields),
        std::make_unique<OrientationStep>(),
        std::make_unique<RotationStep>(),
        std::make_unique<CropStep>(),
        std::make_unique<RetouchStep>(),
        std::make_unique<FilmModeStep>(),
        std::make_unique<BwMixStep>(),
        std::make_unique<FilmBaseStep>(),
        std::make_unique<DensityStep>(),
        std::make_unique<DensityRangeStep>(),
        std::make_unique<ExposureStep>(),
        std::make_unique<WhiteBalanceStep>(),
        std::make_unique<GradationStep>(),
        std::make_unique<ColorGradeStep>(),
        std::make_unique<ShadowsHighlightsStep>(),
        std::make_unique<ContrastStep>(),
        std::make_unique<ToneCurveStep>(),
        std::make_unique<SaturationStep>(),
        std::make_unique<DenoiseStep>(),
        std::make_unique<SharpenStep>(),
    };
    for (auto& step : all) {
        if (auto ok = registry.add(std::move(step)); !ok) {
            return ok;
        }
    }
    return {};
}

} // namespace autoneg::steps
