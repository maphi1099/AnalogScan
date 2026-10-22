#include "autoneg/steps/pointwise_steps.hpp"

#include "describe.hpp"

#include "autoneg/steps/step_ids.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace autoneg::steps {

namespace {

using model::FilmType;
using pipeline::FilmTypeMask;
using pipeline::ParamType;
using pipeline::Phase;

const FilmTypeMask negatives{FilmType::ColorNegative, FilmType::BwNegative};
const FilmTypeMask bwOnly{FilmType::BwNegative};
const FilmTypeMask colorOnly{FilmType::ColorNegative};
const FilmTypeMask colorAndSlide{FilmType::ColorNegative, FilmType::Slide};

constexpr base::Vec3 luminanceWeights{0.2627, 0.6780, 0.0593};

double safe(double v)
{
    return std::max(v, 1e-6);
}

} // namespace

BwMixStep::BwMixStep()
    : DescribedPointwise(describe(id::bwmix, rank::bwmix, "S/W-Kanalmischung", Phase::Conversion,
                                  {vec3Param("weights", luminanceWeights)}, bwOnly,
                                  Options{.alwaysActive = true}))
{}

void BwMixStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                     const pipeline::StepContext& /*ctx*/) const
{
    base::Vec3 w = param(p, "weights", luminanceWeights);
    const double sum = w.x + w.y + w.z;
    if (std::abs(sum) > 1e-9) {
        w = {w.x / sum, w.y / sum, w.z / sum};
    }
    b.matrix(base::Mat3{{w.x, w.y, w.z, w.x, w.y, w.z, w.x, w.y, w.z}});
}

FilmBaseStep::FilmBaseStep()
    : DescribedPointwise(describe(id::filmbase, rank::filmbase, "Basiskorrektur", Phase::Conversion,
                                  {vec3Param("base", {1, 1, 1}), stringParam("source", "")}, negatives))
{}

void FilmBaseStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                        const pipeline::StepContext& /*ctx*/) const
{
    const base::Vec3 base = param(p, "base", base::Vec3{1, 1, 1});
    b.scale({1.0 / safe(base.x), 1.0 / safe(base.y), 1.0 / safe(base.z)});
}

DensityStep::DensityStep()
    : DescribedPointwise(describe(id::density, rank::density, "Dichte", Phase::Conversion, {}, negatives,
                                  Options{.visible = false, .alwaysActive = true}))
{}

void DensityStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& /*p*/,
                       const pipeline::StepContext& /*ctx*/) const
{
    b.log10().scale({-1, -1, -1});
}

DensityRangeStep::DensityRangeStep()
    : DescribedPointwise(describe(id::densityrange, rank::densityrange, "Tonwertgrenzen", Phase::Conversion,
                                  {vec3Param("dMin", {0, 0, 0}), vec3Param("dMax", {1, 1, 1}),
                                   doubleParam("clip", 0.001, 0.0, 0.02, "")},
                                  pipeline::FilmTypeMask::all(), Options{.alwaysActive = true}))
{}

void DensityRangeStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                            const pipeline::StepContext& ctx) const
{
    const base::Vec3 lo = param(p, "dMin", base::Vec3{0, 0, 0});
    const base::Vec3 hi = param(p, "dMax", base::Vec3{1, 1, 1});
    const auto span = [](double l, double h) { return std::max(h - l, 1e-4); };
    const base::Vec3 inv{1.0 / span(lo.x, hi.x), 1.0 / span(lo.y, hi.y), 1.0 / span(lo.z, hi.z)};
    b.offset({-lo.x, -lo.y, -lo.z});
    if (ctx.film == FilmType::Slide) {
        b.scale(inv); // linear, ohne Inversion
        return;
    }
    // v = (10^(R·(n − 1)) − 10^−R) / (1 − 10^−R), n = (D − dMin)/(dMax − dMin)
    constexpr double r = densityOutputRange;
    const double floor = std::pow(10.0, -r);
    b.scale({r * inv.x, r * inv.y, r * inv.z}).offset({-r, -r, -r}).exp10();
    b.offset({-floor, -floor, -floor}).scale({1.0 / (1.0 - floor), 1.0 / (1.0 - floor), 1.0 / (1.0 - floor)});
}

ExposureStep::ExposureStep()
    : DescribedPointwise(describe(id::exposure, rank::exposure, "Belichtung", Phase::Print,
                                  {doubleParam("ev", 0.0, -5.0, 5.0, "EV")}))
{}

void ExposureStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                        const pipeline::StepContext& /*ctx*/) const
{
    const double f = std::exp2(param(p, "ev", 0.0));
    b.scale({f, f, f});
}

WhiteBalanceStep::WhiteBalanceStep()
    : DescribedPointwise(describe(
          id::whitebalance, rank::whitebalance, "Weißabgleich", Phase::Print,
          {doubleParam("temperature", 0.0, -300.0, 300.0, ""), doubleParam("tint", 0.0, -300.0, 300.0, "")},
          colorAndSlide))
{}

void WhiteBalanceStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                            const pipeline::StepContext& /*ctx*/) const
{
    const double t = param(p, "temperature", 0.0) / 100.0;
    const double u = param(p, "tint", 0.0) / 100.0;
    b.scale({std::exp2(t), std::exp2(-u), std::exp2(-t)});
}

std::pair<double, double> WhiteBalanceStep::fromNeutral(const base::Vec3& rgb)
{
    // r·2^t = g·2^−u = b·2^−t
    const double lr = std::log2(safe(rgb.x));
    const double lg = std::log2(safe(rgb.y));
    const double lb = std::log2(safe(rgb.z));
    const double t = (lb - lr) / 2.0;
    const double u = lg - (lr + t);
    return {100.0 * t, 100.0 * u};
}

GradationStep::GradationStep()
    : DescribedPointwise(
          describe(id::gradation, rank::gradation, "Gradation", Phase::Print,
                   {doubleParam("grade", 2.0, -1.0, 5.0, ""), doubleParam("splitSoft", 0.0, 0.0, 1.0, ""),
                    doubleParam("splitHard", 0.0, 0.0, 1.0, ""), boolParam("compensate", true)},
                   bwOnly))
{}

PaperCurve GradationStep::curve(const model::ParamSet& p) const
{
    const SplitGrade split{param(p, "splitSoft", 0.0), param(p, "splitHard", 0.0)};
    if (split.soft > 0.0 || split.hard > 0.0) {
        return model_.split(split);
    }
    return model_.curve(param(p, "grade", 2.0), param(p, "compensate", true));
}

void GradationStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                         const pipeline::StepContext& /*ctx*/) const
{
    b.lut(curve(p).toLut());
}

ColorGradeStep::ColorGradeStep()
    : DescribedPointwise(describe(id::colorgrade, rank::colorgrade, "Gradation Farbe", Phase::Print,
                                  {intParam("level", 0, 0, 3)}, colorOnly, Options{.cacheAfter = true}))
{}

void ColorGradeStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                          const pipeline::StepContext& /*ctx*/) const
{
    const int level = param(p, "level", 0);
    if (level <= 0) {
        return;
    }
    // 1 weich (wie Stufe 1, R130), 2 normal (Stufe 2, R110), 3 hart (Stufe 3, R90)
    b.lut(model_.curve(static_cast<double>(level), true).toLut());
}

ContrastStep::ContrastStep()
    : DescribedPointwise(
          describe(id::contrast, rank::contrast, "Kontrast", Phase::Finishing,
                   {doubleParam("amount", 0.0, -1.0, 1.0, ""), doubleParam("pivot", 0.18, 0.01, 1.0, "")}))
{}

bool ToneCurveStep::isNeutral(const model::ParamSet& p) const
{
    const auto flat = [&](const char* name) { return param(p, name, model::Curve{}).points.size() < 2; };
    return param(p, "black", 0.0) == 0.0 && param(p, "white", 1.0) == 1.0 && flat("master") && flat("r") &&
           flat("g") && flat("b");
}

void ContrastStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                        const pipeline::StepContext& /*ctx*/) const
{
    const double k = 1.0 + param(p, "amount", 0.0);
    const double pivot = param(p, "pivot", 0.18);
    // pivot·(v/pivot)^k = 10^(k·log10 v + (1 − k)·log10 pivot)
    const double o = (1.0 - k) * std::log10(pivot);
    b.log10().scale({k, k, k}).offset({o, o, o}).exp10();
}

ToneCurveStep::ToneCurveStep()
    : DescribedPointwise(
          describe(id::tonecurve, rank::tonecurve, "Tonwerte und Kurven", Phase::Finishing,
                   {doubleParam("black", 0.0, 0.0, 0.5, ""), doubleParam("white", 1.0, 0.5, 2.0, ""),
                    curveParam("master"), curveParam("r"), curveParam("g"), curveParam("b")}))
{}

void ToneCurveStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                         const pipeline::StepContext& /*ctx*/) const
{
    const double black = param(p, "black", 0.0);
    const double white = std::max(param(p, "white", 1.0), black + 1e-3);
    const double s = 1.0 / (white - black);
    b.offset({-black, -black, -black}).scale({s, s, s});
    const char* names[] = {"master", "r", "g", "b"};
    for (int i = 0; i < 4; ++i) {
        const model::Curve c = param(p, names[i], model::Curve{});
        if (c.points.size() >= 2) {
            b.lut(curveLut(c, i - 1));
        }
    }
}

SaturationStep::SaturationStep()
    : DescribedPointwise(describe(
          id::saturation, rank::saturation, "Sättigung", Phase::Finishing,
          {doubleParam("saturation", 0.0, -1.0, 1.0, ""), doubleParam("vibrance", 0.0, -1.0, 1.0, "")},
          colorAndSlide, Options{.cacheAfter = true}))
{}

void SaturationStep::emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
                          const pipeline::StepContext& /*ctx*/) const
{
    b.saturate(static_cast<float>(param(p, "saturation", 0.0)),
               static_cast<float>(param(p, "vibrance", 0.0)));
}

double evaluateCurve(const model::Curve& c, double x)
{
    std::vector<model::NormPoint> pts = c.points;
    if (pts.size() < 2) {
        return x;
    }
    std::ranges::sort(pts, {}, &model::NormPoint::x);
    if (x <= pts.front().x) {
        return pts.front().y;
    }
    if (x >= pts.back().x) {
        return pts.back().y;
    }
    const std::size_t n = pts.size();
    // Sekanten und Tangenten nach Fritsch–Carlson (monoton, ohne Überschwinger)
    std::vector<double> d(n - 1);
    std::vector<double> m(n);
    for (std::size_t k = 0; k + 1 < n; ++k) {
        const double dx = std::max(pts[k + 1].x - pts[k].x, 1e-12);
        d[k] = (pts[k + 1].y - pts[k].y) / dx;
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (std::size_t k = 1; k + 1 < n; ++k) {
        m[k] = d[k - 1] * d[k] <= 0.0 ? 0.0 : 0.5 * (d[k - 1] + d[k]);
    }
    for (std::size_t k = 0; k + 1 < n; ++k) {
        if (d[k] == 0.0) {
            m[k] = 0.0;
            m[k + 1] = 0.0;
            continue;
        }
        const double a = m[k] / d[k];
        const double bb = m[k + 1] / d[k];
        const double s = a * a + bb * bb;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m[k] = t * a * d[k];
            m[k + 1] = t * bb * d[k];
        }
    }
    std::size_t k = 0;
    while (k + 2 < n && x > pts[k + 1].x) {
        ++k;
    }
    const double h = pts[k + 1].x - pts[k].x;
    const double t = (x - pts[k].x) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * pts[k].y + (t3 - 2 * t2 + t) * h * m[k] +
           (-2 * t3 + 3 * t2) * pts[k + 1].y + (t3 - t2) * h * m[k + 1];
}

image::Lut1D curveLut(const model::Curve& c, int channel, int n)
{
    image::Lut1D lut;
    lut.channel = channel;
    lut.values.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        lut.values[static_cast<std::size_t>(i)] =
            static_cast<float>(evaluateCurve(c, static_cast<double>(i) / (n - 1)));
    }
    return lut;
}

} // namespace autoneg::steps
