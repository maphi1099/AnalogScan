#pragma once

// Test-Schritte für Pipeline-Tests (M2), bevor die echten Schritte (M3) existieren.
// Kennungen, Ränge, Arten, Filmtypen und Cache-Punkte folgen Architektur 5.2 und Tabelle 1.
// Die Rechenvorschriften sind vereinfacht, aber eindeutig (Tests vergleichen Ergebnisse).

#include "autoneg/pipeline/pixel_program_builder.hpp"
#include "autoneg/pipeline/step.hpp"
#include "autoneg/pipeline/step_registry.hpp"
#include "autoneg/pipeline/warp_builder.hpp"
#include "autoneg/testing/edit_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace autoneg::testing {

using model::FilmType;
using pipeline::FilmTypeMask;
using pipeline::ParamSpec;
using pipeline::ParamType;
using pipeline::StepDescriptor;

inline StepDescriptor descriptor(const std::string& name, int rankValue, std::vector<ParamSpec> params,
                                 FilmTypeMask films = FilmTypeMask::all(), bool cacheAfter = false,
                                 bool visible = true)
{
    StepDescriptor d;
    d.id = model::StepId("autoneg." + name);
    d.rank = rankValue;
    d.name = name;
    d.params = std::move(params);
    d.films = films;
    d.cacheAfter = cacheAfter;
    d.visible = visible;
    return d;
}

inline ParamSpec dbl(const std::string& name, double neutral, double min = 0.0, double max = 0.0)
{
    return ParamSpec{name, ParamType::Double, neutral, min, max, ""};
}

inline double paramOr(const model::ParamSet& p, const std::string& name, double fallback)
{
    return p.get<double>(name).value_or(fallback);
}

class TestPointwise final : public pipeline::PointwiseStep
{
public:
    using Emit = std::function<void(pipeline::PixelProgramBuilder&, const model::ParamSet&)>;
    TestPointwise(StepDescriptor d, Emit emit, bool alwaysActive = false)
        : d_(std::move(d))
        , emit_(std::move(emit))
        , alwaysActive_(alwaysActive)
    {}
    [[nodiscard]] const StepDescriptor& descriptor() const noexcept override { return d_; }
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override
    {
        return !alwaysActive_ && PointwiseStep::isNeutral(p);
    }
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& /*ctx*/) const override
    {
        emit_(b, p);
    }

private:
    StepDescriptor d_;
    Emit emit_;
    bool alwaysActive_;
};

class TestGeometric final : public pipeline::GeometricStep
{
public:
    using Emit = std::function<void(pipeline::WarpBuilder&, const model::ParamSet&)>;
    TestGeometric(StepDescriptor d, Emit emit)
        : d_(std::move(d))
        , emit_(std::move(emit))
    {}
    [[nodiscard]] const StepDescriptor& descriptor() const noexcept override { return d_; }
    void emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& /*ctx*/) const override
    {
        emit_(b, p);
    }

private:
    StepDescriptor d_;
    Emit emit_;
};

class TestRegion final : public pipeline::RegionStep
{
public:
    using Process = std::function<base::Expected<image::ImageBuffer>(
        pipeline::ExecContext&, const image::ImageBuffer&, const model::ParamSet&)>;
    using Halo = std::function<int(const model::ParamSet&, double)>;
    TestRegion(StepDescriptor d, Halo halo, Process process)
        : d_(std::move(d))
        , halo_(std::move(halo))
        , process_(std::move(process))
    {}
    [[nodiscard]] const StepDescriptor& descriptor() const noexcept override { return d_; }
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size /*image*/) const override
    {
        return halo_(p, scale);
    }
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& ctx, const image::ImageBuffer& in, const model::ParamSet& p) const override
    {
        return process_(ctx, in, p);
    }

private:
    StepDescriptor d_;
    Halo halo_;
    Process process_;
};

/// Filmtyp-Steuerstufe: Parameter "type" = "color" | "bw" | "slide".
class TestFilmMode final : public pipeline::ControlStep
{
public:
    TestFilmMode()
        : d_(autoneg::testing::descriptor("filmmode", rank::filmmode,
                                          {ParamSpec{"type", ParamType::String, std::string(), 0, 0, ""}}))
    {}
    [[nodiscard]] const StepDescriptor& descriptor() const noexcept override { return d_; }
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    void configure(pipeline::StepContext& ctx, const model::ParamSet& p) const override
    {
        const std::string t = p.get<std::string>("type").value_or("color");
        ctx.film = t == "bw"      ? FilmType::BwNegative
                   : t == "slide" ? FilmType::Slide
                                  : FilmType::ColorNegative;
    }

private:
    StepDescriptor d_;
};

inline int gaussHalo(double sigma)
{
    return std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
}

/// Registry mit allen Stufen der Architektur 5.2 als Test-Schritte.
inline std::unique_ptr<pipeline::StepRegistry> standardRegistry()
{
    using pipeline::ExecContext;
    using pipeline::PixelProgramBuilder;
    using pipeline::WarpBuilder;
    const FilmTypeMask neg{FilmType::ColorNegative, FilmType::BwNegative};
    const FilmTypeMask bw{FilmType::BwNegative};
    const FilmTypeMask cn{FilmType::ColorNegative};
    const FilmTypeMask cnSlide{FilmType::ColorNegative, FilmType::Slide};

    auto reg = std::make_unique<pipeline::StepRegistry>();
    const auto add = [&](std::unique_ptr<pipeline::IStep> s) { (void)reg->add(std::move(s)); };

    add(std::make_unique<TestRegion>(
        descriptor("flatfield", rank::flatfield, {dbl("sigma", 0)}, FilmTypeMask::all(), true),
        [](const auto& p, double s) { return gaussHalo(paramOr(p, "sigma", 0) * s); },
        [](ExecContext& x, const image::ImageBuffer& in, const auto& p) {
            return x.backend.gaussian(x.kernel(), in, static_cast<float>(paramOr(p, "sigma", 0) * x.scale));
        }));
    add(std::make_unique<TestGeometric>(descriptor("orientation", rank::orientation,
                                                   {ParamSpec{"quarterTurns", ParamType::Int, 0, 0, 3, ""},
                                                    ParamSpec{"flipH", ParamType::Bool, false, 0, 0, ""},
                                                    ParamSpec{"flipV", ParamType::Bool, false, 0, 0, ""}}),
                                        [](WarpBuilder& b, const auto& p) {
                                            b.orient(p.template get<int>("quarterTurns").value_or(0),
                                                     p.template get<bool>("flipH").value_or(false),
                                                     p.template get<bool>("flipV").value_or(false));
                                        }));
    add(std::make_unique<TestGeometric>(
        descriptor("rotation", rank::rotation, {dbl("angle", 0, -45, 45)}),
        [](WarpBuilder& b, const auto& p) { b.rotate(paramOr(p, "angle", 0)); }));
    add(std::make_unique<TestGeometric>(
        descriptor("crop", rank::crop,
                   {ParamSpec{"rect", ParamType::Rect, model::NormRect::full(), 0, 0, ""}}),
        [](WarpBuilder& b, const auto& p) {
            b.crop(p.template get<model::NormRect>("rect").value_or(model::NormRect{}));
        }));
    add(std::make_unique<TestRegion>(
        descriptor("retouch", rank::retouch,
                   {ParamSpec{"spots", ParamType::Spots, model::SpotList{}, 0, 0, ""}}, FilmTypeMask::all(),
                   true),
        [](const auto& /*p*/, double /*s*/) { return 0; },
        [](ExecContext& x, const image::ImageBuffer& in, const auto& p) {
            return x.backend.inpaint(x.kernel(), in,
                                     p.template get<model::SpotList>("spots").value_or(model::SpotList{}));
        }));
    add(std::make_unique<TestFilmMode>());
    add(std::make_unique<TestPointwise>(
        descriptor("bwmix", rank::bwmix, {ParamSpec{"weights", ParamType::Vec3, base::Vec3{}, 0, 0, ""}}, bw),
        [](PixelProgramBuilder& b, const auto& p) {
            const auto w = p.template get<base::Vec3>("weights").value_or(base::Vec3{1, 1, 1});
            b.matrix(base::Mat3{{w.x, w.y, w.z, w.x, w.y, w.z, w.x, w.y, w.z}});
        }));
    add(std::make_unique<TestPointwise>(
        descriptor("filmbase", rank::filmbase,
                   {ParamSpec{"base", ParamType::Vec3, base::Vec3{1, 1, 1}, 0, 0, ""}}, neg),
        [](PixelProgramBuilder& b, const auto& p) {
            const auto v = p.template get<base::Vec3>("base").value_or(base::Vec3{1, 1, 1});
            b.scale({1.0 / v.x, 1.0 / v.y, 1.0 / v.z});
        }));
    add(std::make_unique<TestPointwise>(
        descriptor("density", rank::density, {}, neg, false, false),
        [](PixelProgramBuilder& b, const auto& /*p*/) { b.log10().scale({-1, -1, -1}); }, true));
    add(std::make_unique<TestPointwise>(
        descriptor("densityrange", rank::densityrange, {dbl("black", 0), dbl("white", 1)}),
        [](PixelProgramBuilder& b, const auto& p) {
            const double lo = paramOr(p, "black", 0);
            const double hi = paramOr(p, "white", 1);
            b.offset({-lo, -lo, -lo}).scale({1 / (hi - lo), 1 / (hi - lo), 1 / (hi - lo)});
        }));
    add(std::make_unique<TestPointwise>(descriptor("exposure", rank::exposure, {dbl("ev", 0, -5, 5)}),
                                        [](PixelProgramBuilder& b, const auto& p) {
                                            const double f = std::exp2(paramOr(p, "ev", 0));
                                            b.scale({f, f, f});
                                        }));
    add(std::make_unique<TestPointwise>(
        descriptor("whitebalance", rank::whitebalance, {dbl("temperature", 0)}, cnSlide),
        [](PixelProgramBuilder& b, const auto& p) {
            const double t = paramOr(p, "temperature", 0) / 10000.0;
            b.scale({1 + t, 1.0, 1 - t});
        }));
    add(std::make_unique<TestPointwise>(descriptor("gradation", rank::gradation, {dbl("grade", -99)}, bw),
                                        [](PixelProgramBuilder& b, const auto& p) {
                                            const double g = paramOr(p, "grade", 2);
                                            image::Lut1D l;
                                            for (int i = 0; i < 64; ++i) {
                                                l.values.push_back(
                                                    static_cast<float>(std::pow(i / 63.0, 1 + g / 5)));
                                            }
                                            b.lut(std::move(l));
                                        }));
    add(std::make_unique<TestPointwise>(descriptor("colorgrade", rank::colorgrade,
                                                   {ParamSpec{"level", ParamType::Int, 0, 0, 3, ""}}, cn,
                                                   true),
                                        [](PixelProgramBuilder& b, const auto& p) {
                                            const int level = p.template get<int>("level").value_or(0);
                                            b.scale({1 + 0.1 * level, 1 + 0.1 * level, 1 + 0.1 * level});
                                        }));
    add(std::make_unique<TestRegion>(
        descriptor("shadowshighlights", rank::shadowshighlights, {dbl("shadows", 0), dbl("radius", 0)}),
        [](const auto& p, double s) { // Guided Filter: zweimal Box mit Radius q
            return 2 * std::max(1,
                                static_cast<int>(std::lround(std::max(1.0, paramOr(p, "radius", 0)) * s))) +
                   1;
        },
        [](ExecContext& x, const image::ImageBuffer& in, const auto& p) {
            return x.backend.localTone(x.kernel(), in, static_cast<float>(paramOr(p, "shadows", 0)), 0.0F,
                                       static_cast<float>(std::max(1.0, paramOr(p, "radius", 0)) * x.scale));
        }));
    add(std::make_unique<TestPointwise>(
        descriptor("contrast", rank::contrast, {dbl("amount", 0, -1, 1)}),
        [](PixelProgramBuilder& b, const auto& p) {
            const double a = 1 + paramOr(p, "amount", 0);
            b.offset({-0.18, -0.18, -0.18}).scale({a, a, a}).offset({0.18, 0.18, 0.18});
        }));
    add(std::make_unique<TestPointwise>(
        descriptor("saturation", rank::saturation, {dbl("saturation", 0)}, cnSlide, true),
        [](PixelProgramBuilder& b, const auto& p) {
            const double s = 1 + paramOr(p, "saturation", 0);
            const double k = (1 - s) / 3;
            b.matrix(base::Mat3{{k + s, k, k, k, k + s, k, k, k, k + s}});
        }));
    add(std::make_unique<TestRegion>(
        descriptor("denoise", rank::denoise, {dbl("strength", 0)}),
        [](const auto& /*p*/, double /*s*/) { return 2; },
        [](ExecContext& x, const image::ImageBuffer& in, const auto& p) {
            return x.backend.denoise(x.kernel(), in, static_cast<float>(paramOr(p, "strength", 0)));
        }));
    add(std::make_unique<TestRegion>(
        descriptor("sharpen", rank::sharpen, {dbl("amount", 0), dbl("radius", 1.0)}),
        [](const auto& p, double s) { return gaussHalo(paramOr(p, "radius", 1.0) * s); },
        [](ExecContext& x, const image::ImageBuffer& in, const auto& p) {
            return x.backend.unsharp(x.kernel(), in, static_cast<float>(paramOr(p, "amount", 0)),
                                     static_cast<float>(paramOr(p, "radius", 1.0) * x.scale), 0.0F);
        }));
    return reg;
}

} // namespace autoneg::testing
