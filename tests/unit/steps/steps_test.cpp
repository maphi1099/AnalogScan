// Unit-Tests der Verarbeitungsschritte (Architektur 5, 9.5, 9.6). Rechen-Backend: skalare
// Referenz; Testbilder: synthetisch (tests/support/synthetic.hpp).

#include "autoneg/mocks/ports.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/reference/reference_backend.hpp"
#include "autoneg/steps/multigrade.hpp"
#include "autoneg/steps/other_steps.hpp"
#include "autoneg/steps/pointwise_steps.hpp"
#include "autoneg/steps/registry.hpp"
#include "autoneg/steps/step_ids.hpp"
#include "autoneg/testing/color_metrics.hpp"
#include "autoneg/testing/synthetic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace autoneg::steps {
namespace {

using model::EditList;
using model::FilmType;
using model::ParamSet;
using pipeline::RenderMode;
namespace t = autoneg::testing;

class Steps : public ::testing::Test
{
protected:
    Steps() { EXPECT_TRUE(registerStandardSteps(registry, flatFields).has_value()); }

    /// Eintrag für den registrierten Schritt id.
    model::EditEntry entry(std::string_view stepId, ParamSet p) const
    {
        const pipeline::IStep* s = registry.find(stepId_(stepId));
        EXPECT_NE(s, nullptr) << stepId;
        model::EditEntry e;
        e.step = stepId_(stepId);
        e.rank = s != nullptr ? s->descriptor().rank : 0;
        e.version = s != nullptr ? s->descriptor().version : 1;
        e.params = std::move(p);
        return e;
    }
    static model::StepId stepId_(std::string_view s) { return stepId(s); }

    EditList film(FilmType f) const
    {
        return EditList{}.with(
            entry(id::filmmode, ParamSet{}.with("type", std::string(FilmModeStep::toString(f)))));
    }

    /// Rechnet die Liste auf src (Export-Profil, am Stück).
    image::ImageBuffer apply(const EditList& l, const image::ImageBuffer& src,
                             FilmType f = FilmType::ColorNegative)
    {
        pipeline::RenderProfile p;
        p.mode = RenderMode::Export;
        const pipeline::CompiledPipeline cp = compiler.compile(l, f, p, src.spec());
        pipeline::RenderRequest r;
        r.list = std::make_shared<const EditList>(l);
        r.profile = p;
        auto out = executor.run(cp, src, r, pipeline::Execution::Whole);
        EXPECT_TRUE(out.has_value()) << (out ? "" : out.error().message);
        return out ? *out : image::ImageBuffer{};
    }

    std::size_t stageCount(const EditList& l, FilmType f, const image::ImageSpec& spec) const
    {
        pipeline::RenderProfile p;
        p.mode = RenderMode::Export;
        return compiler.compile(l, f, p, spec).stages.size();
    }

    FlatFieldCache flatFields;
    pipeline::StepRegistry registry;
    pipeline::PipelineCompiler compiler{registry};
    reference::ReferenceBackend backend;
    pipeline::StageCache cache{std::size_t{1} << 28};
    mocks::MockRawDecoder decoder;
    mocks::MockImageReader reader;
    pipeline::SourceProvider sources{decoder, reader};
    pipeline::PipelineExecutor executor{compiler, backend, cache, sources};
};

image::ImageBuffer testImage(int w = 64, int h = 40)
{
    return t::imageFrom(w, h, 3, [](int x, int y, int c) {
        return static_cast<float>(0.05 + 0.85 * (0.5 + 0.5 * std::sin(x * 0.21 + y * 0.13 + c)));
    });
}

bool bitEqual(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    return a.spec() == b.spec() && std::ranges::equal(a.hostBytes(), b.hostBytes());
}

base::Vec3 pixel(const image::ImageBuffer& img, int x, int y)
{
    return t::meanRgb(img, x, y, 1, 1);
}

/// Steps ohne Identität als Neutralwert (Abweichung T-02): Filmtyp, Dichte, Tonwertgrenzen,
/// S/W-Mischung (bei S/W immer aktiv), Gradation (Stufe 2 ist eine Papierkennlinie).
bool hasIdentity(std::string_view s)
{
    return s != id::filmmode && s != id::density && s != id::densityrange && s != id::bwmix &&
           s != id::gradation;
}

FilmType activeFilm(const pipeline::StepDescriptor& d)
{
    for (const FilmType f : {FilmType::Slide, FilmType::ColorNegative, FilmType::BwNegative}) {
        if (d.films.contains(f)) {
            return f;
        }
    }
    return FilmType::Slide;
}

// NF-ARC-04, LF-LST-03
TEST_F(Steps, U_STP_01_NeutralValuesLeaveImageUnchanged)
{
    const image::ImageBuffer src = testImage();
    int checked = 0;
    for (const pipeline::IStep* s : registry.ordered()) {
        const auto& d = s->descriptor();
        if (!hasIdentity(d.id.value())) {
            continue;
        }
        SCOPED_TRACE(d.id.value());
        ParamSet neutral;
        for (const pipeline::ParamSpec& spec : d.params) {
            neutral = neutral.with(spec.name, spec.neutral);
        }
        if (d.id.value() == id::flatfield) {
            continue; // Neutralwert "" = keine Kalibrierung; ohne Profil keine Stufe (unten geprüft)
        }
        const FilmType f = activeFilm(d);
        const EditList base = film(f);
        const EditList withNeutral = base.with(entry(d.id.value(), neutral));
        EXPECT_TRUE(s->isNeutral(neutral));
        EXPECT_EQ(stageCount(withNeutral, f, src.spec()), stageCount(base, f, src.spec()));
        EXPECT_TRUE(bitEqual(apply(withNeutral, src, f), apply(base, src, f)));
        ++checked;
    }
    EXPECT_GE(checked, 14);
    // Leuchtfläche ohne Profil
    EXPECT_TRUE(registry.find(stepId(id::flatfield))->isNeutral(ParamSet{}.with("profile", std::string())));
}

// NF-REL-02
TEST_F(Steps, U_STP_02_ParameterLimitsProduceNoNanOrInf)
{
    const image::ImageBuffer src = t::imageFrom(16, 12, 3, [](int x, int y, int c) {
        const float vals[] = {0.0F, 1.0F, 1e-6F, 0.5F, 2.0F, 1e-12F};
        return vals[static_cast<std::size_t>((x + y * 3 + c) % 6)];
    });
    flatFields.add("kal", t::makeBlank(32, 24, 0.15));
    for (const pipeline::IStep* s : registry.ordered()) {
        const auto& d = s->descriptor();
        const FilmType f = activeFilm(d);
        for (const bool upper : {false, true}) {
            ParamSet p;
            for (const pipeline::ParamSpec& spec : d.params) {
                if (spec.min < spec.max) {
                    const double v = upper ? spec.max : spec.min;
                    p = spec.type == pipeline::ParamType::Int ? p.with(spec.name, static_cast<int>(v))
                                                              : p.with(spec.name, v);
                } else {
                    p = p.with(spec.name, spec.neutral);
                }
            }
            if (d.id.value() == id::flatfield) {
                p = p.with("profile", std::string("kal"));
            }
            if (d.id.value() == id::retouch) {
                p = p.with("spots", model::SpotList{{{0.5, 0.5}, {0.2, 0.0}, 0.1}});
            }
            if (d.id.value() == id::tonecurve) {
                p = p.with("master", model::Curve{{{0, 0}, {0.5, 0.7}, {1, 1}}});
            }
            SCOPED_TRACE(d.id.value() + (upper ? " max" : " min"));
            const image::ImageBuffer out = apply(film(f).with(entry(d.id.value(), p)), src, f);
            for (const float v : out.host()) {
                ASSERT_TRUE(std::isfinite(v));
            }
        }
    }
}

// NF-ARC-09
TEST_F(Steps, U_STP_03_IdsAndVersionsAreStable)
{
    std::ostringstream actual;
    for (const pipeline::IStep* s : registry.ordered()) {
        actual << s->descriptor().id.value() << " " << s->descriptor().version << "\n";
    }
    std::ifstream file(AUTONEG_STEP_IDS_FILE);
    ASSERT_TRUE(file.good()) << AUTONEG_STEP_IDS_FILE;
    std::stringstream expected;
    expected << file.rdbuf();
    EXPECT_EQ(actual.str(), expected.str()) << "Kennungen oder Versionen geändert? (NF-ARC-09)";
}

// LF-KON-02, LF-BAS-01
TEST_F(Steps, U_STP_04_ColorNegativeRoundTrip)
{
    t::NegativeSpec spec;
    spec.rebate = false;
    spec.holder = false;
    const t::SyntheticScan scan = t::makeNegative(spec);
    const EditList l =
        film(FilmType::ColorNegative)
            .with(entry(id::crop, ParamSet{}.with("rect", scan.truth.frame)))
            .with(entry(id::filmbase, ParamSet{}.with("base", scan.truth.base)))
            .with(entry(id::densityrange,
                        ParamSet{}.with("dMin", scan.truth.dMin).with("dMax", scan.truth.dMax)));
    const image::ImageBuffer out = apply(l, scan.scan);
    const image::ImageBuffer positive = t::positiveImage(spec.pattern, out.spec().width, out.spec().height);
    const t::DeltaStats d = t::compareImages(positive, out);
    EXPECT_LT(d.mean, 1.0);
    // Graufelder (obere Reihe) neutral
    const int gw = out.spec().width / 6;
    for (int i = 0; i < 6; ++i) {
        const base::Vec3 g =
            t::meanRgb(out, i * gw + gw / 4, out.spec().height / 20, gw / 2, out.spec().height / 10);
        EXPECT_LE(t::deltaToNeutral(g), 3.0) << "Graufeld " << i;
    }
}

// LF-KON-05
TEST_F(Steps, U_STP_05_ClipShareOfDensityRange)
{
    // Verlauf im Dichteraum; Grenzen so, dass je Ende 0,25 % beschnitten werden (clip 0,5 %)
    const int w = 4000;
    const image::ImageBuffer src = t::imageFrom(w, 1, 3, [](int x, int /*y*/, int /*c*/) {
        return static_cast<float>(std::pow(10.0, -(1.5 * (x + 0.5) / w))); // D 0 … 1,5
    });
    const double clip = 0.005;
    const double lo = 1.5 * (clip / 2.0);
    const double hi = 1.5 * (1.0 - clip / 2.0);
    const EditList l = film(FilmType::BwNegative)
                           .with(entry(id::densityrange, ParamSet{}
                                                             .with("dMin", base::Vec3{lo, lo, lo})
                                                             .with("dMax", base::Vec3{hi, hi, hi})
                                                             .with("clip", clip)));
    const image::ImageBuffer out = apply(l, src, FilmType::BwNegative);
    std::size_t clipped = 0;
    const auto v = out.host();
    for (std::size_t i = 0; i < v.size(); i += 3) {
        clipped += (v[i] <= 0.0F || v[i] >= 1.0F) ? 1U : 0U;
    }
    EXPECT_NEAR(static_cast<double>(clipped) / w, clip, 0.001);
}

// LF-KON-03
TEST_F(Steps, U_STP_06_BwMixSelectsChannel)
{
    const image::ImageBuffer src = testImage();
    for (const auto& [weights, channel] :
         {std::pair{base::Vec3{1, 0, 0}, 0}, std::pair{base::Vec3{0, 1, 0}, 1}}) {
        const EditList l =
            film(FilmType::BwNegative).with(entry(id::bwmix, ParamSet{}.with("weights", weights)));
        // nur die Mischung prüfen: Abgriff vor der Dichte
        pipeline::RenderProfile p;
        p.mode = RenderMode::Export;
        pipeline::CompileOptions o;
        o.stopAfter = stepId(id::bwmix);
        const auto cp = compiler.compile(l, FilmType::BwNegative, p, src.spec(), 1.0, o);
        pipeline::RenderRequest r;
        r.list = std::make_shared<const EditList>(l);
        const image::ImageBuffer out = executor.run(cp, src, r, pipeline::Execution::Whole).value();
        const auto s = src.host();
        const auto o2 = out.host();
        for (std::size_t i = 0; i < o2.size(); i += 3) {
            ASSERT_EQ(o2[i], o2[i + 1]);
            ASSERT_EQ(o2[i], o2[i + 2]);
            ASSERT_NEAR(o2[i], s[i + static_cast<std::size_t>(channel)], 1e-6);
        }
    }
}

// LF-KON-04
TEST_F(Steps, U_STP_07_SlideWithoutInversion)
{
    const image::ImageBuffer src = testImage();
    const EditList l = film(FilmType::Slide)
                           .with(entry(id::filmbase, ParamSet{}.with("base", base::Vec3{0.5, 0.4, 0.3})))
                           .with(entry(id::densityrange, ParamSet{}
                                                             .with("dMin", base::Vec3{0.05, 0.05, 0.05})
                                                             .with("dMax", base::Vec3{0.95, 0.95, 0.95})));
    const image::ImageBuffer out = apply(l, src, FilmType::Slide);
    const auto s = src.host();
    const auto o = out.host();
    for (std::size_t i = 0; i < o.size(); ++i) {
        ASSERT_NEAR(o[i], (s[i] - 0.05) / 0.9, 1e-5) << i; // linear, Basis wirkungslos
    }
}

// LF-KON-09
TEST(Multigrade, U_STP_08_IsoRangePerGrade)
{
    const MultigradeModel m;
    for (const GradeData& g : m.grades()) {
        SCOPED_TRACE(g.grade);
        EXPECT_NEAR(m.curve(g.grade).isoRange(), g.isoR, 10.0);
        EXPECT_NEAR(m.curve(g.grade).isoRange(), g.isoR, 0.5); // Modell trifft den Wert genau
    }
    // Datenblattwerte der ganzen Stufen
    EXPECT_EQ(m.isoR(-1.0), 180);
    EXPECT_EQ(m.isoR(0.0), 160);
    EXPECT_EQ(m.isoR(1.0), 130);
    EXPECT_EQ(m.isoR(2.0), 110);
    EXPECT_EQ(m.isoR(3.0), 90);
    EXPECT_EQ(m.isoR(4.0), 60);
    EXPECT_EQ(m.isoR(5.0), 40);
}

// LF-KON-10
TEST_F(Steps, U_STP_09_MidToneConstantAcrossGrades)
{
    const image::ImageBuffer mid = t::imageFrom(4, 4, 3, [](int, int, int) { return 0.18F; });
    const auto densityOf = [&](double grade) {
        // Gradation wirkt auf den linearen Wert nach den Tonwertgrenzen: nur diese Stufe rechnen
        const auto* g = static_cast<const GradationStep*>(registry.find(stepId(id::gradation)));
        pipeline::PixelProgramBuilder b;
        g->emit(b, ParamSet{}.with("grade", grade).with("compensate", true), {});
        const auto out = backend.runPixel({}, b.build(), mid).value();
        return -std::log10(out.host()[0]);
    };
    const double ref = densityOf(2.0);
    for (const double g : {-1.0, 0.0, 0.5, 1.0, 1.5, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0}) {
        EXPECT_NEAR(densityOf(g), ref, 0.05) << "Stufe " << g;
    }
    EXPECT_NEAR(ref, MultigradeModel::midDensity, 0.01); // Mittelgrau bleibt 0,18
}

// LF-KON-11
TEST(Multigrade, U_STP_10_IntermediateGradesAreContinuous)
{
    const MultigradeModel m;
    // Steilheit ~ 1/ISO R: stetig heißt hier relativ kleiner Zuwachs je Zehntelstufe und kein Knick
    double previousGamma = 0.0;
    double previousStep = 0.0;
    for (int i = 0; i <= 50; ++i) {
        const double g = i / 10.0;
        const PaperCurve c = m.curve(g);
        const image::Lut1D lut = c.toLut(256);
        for (std::size_t k = 1; k < lut.values.size(); ++k) {
            ASSERT_GE(lut.values[k], lut.values[k - 1]) << "Stufe " << g; // monoton
        }
        const double gamma = c.gamma(0.0);
        EXPECT_GT(gamma, previousGamma) << "Stufe " << g; // Steilheit wächst stetig
        if (i > 0) {
            EXPECT_LT(gamma / previousGamma, 1.10) << "Sprung bei Stufe " << g;
            const double step = gamma - previousGamma;
            if (i > 1) {
                EXPECT_LT(step / previousStep, 2.0) << "Knick bei Stufe " << g;
                EXPECT_GT(step / previousStep, 0.5) << "Knick bei Stufe " << g;
            }
            previousStep = step;
        }
        previousGamma = gamma;
    }
}

// LF-KON-12
TEST(Multigrade, U_STP_11_SplitGradeLimits)
{
    const MultigradeModel m;
    const PaperCurve soft = m.split({1.0, 0.0});
    const PaperCurve hard = m.split({0.0, 1.0});
    const PaperCurve both = m.split({0.5, 0.5});
    for (double x = -1.5; x <= 1.5; x += 0.1) {
        EXPECT_NEAR(soft.density(x), m.curve(-1.0).density(x), 1e-12);
        EXPECT_NEAR(hard.density(x), m.curve(5.0).density(x), 1e-12);
    }
    EXPECT_GT(both.isoRange(), hard.isoRange());
    EXPECT_LT(both.isoRange(), soft.isoRange());
}

// LF-KON-13
TEST_F(Steps, U_STP_12_ColorGradeThreeLevels)
{
    const image::ImageBuffer ramp =
        t::imageFrom(256, 1, 3, [](int x, int, int) { return (static_cast<float>(x) + 0.5F) / 256.0F; });
    // nur diese Stufe rechnen (vor ihr steht in der Pipeline die Negativ-Inversion)
    const auto* step = registry.find(stepId(id::colorgrade));
    std::vector<image::ImageBuffer> outs;
    for (const int level : {1, 2, 3}) {
        pipeline::PixelProgramBuilder b;
        static_cast<const pipeline::PointwiseStep*>(step)->emit(b, ParamSet{}.with("level", level), {});
        outs.push_back(backend.runPixel({}, b.build(), ramp).value());
        const auto v = outs.back().host();
        for (std::size_t i = 3; i < v.size(); i += 3) {
            ASSERT_GE(v[i], v[i - 3]) << "Stufe " << level;
        }
    }
    EXPECT_FALSE(bitEqual(outs[0], outs[1]));
    EXPECT_FALSE(bitEqual(outs[1], outs[2]));
}

// LF-EDT-04
TEST_F(Steps, U_STP_13_ExposureScalesByPowerOfTwo)
{
    const image::ImageBuffer gray = t::imageFrom(8, 8, 3, [](int, int, int) { return 0.18F; });
    for (const double ev : {-3.0, 0.0, 3.0}) {
        const image::ImageBuffer out =
            apply(film(FilmType::Slide).with(entry(id::exposure, ParamSet{}.with("ev", ev))), gray,
                  FilmType::Slide);
        EXPECT_NEAR(out.host()[0], 0.18 * std::exp2(ev), 1e-5) << ev;
    }
}

// LF-EDT-05
TEST_F(Steps, U_STP_14_ContrastAroundPivot)
{
    const image::ImageBuffer ramp =
        t::imageFrom(200, 1, 3, [](int x, int, int) { return 0.01F + static_cast<float>(x) * 0.005F; });
    const image::ImageBuffer out = apply(
        film(FilmType::Slide).with(entry(id::contrast, ParamSet{}.with("amount", 0.5).with("pivot", 0.18))),
        ramp, FilmType::Slide);
    const auto in = ramp.host();
    const auto o = out.host();
    // Pivot 0,18 liegt bei x = 34: unverändert
    EXPECT_NEAR(o[34 * 3], in[34 * 3], 1e-5);
    const double slopeIn = (in[36 * 3] - in[32 * 3]);
    const double slopeOut = (o[36 * 3] - o[32 * 3]);
    EXPECT_GT(slopeOut, 1.4 * slopeIn); // Steigung am Pivot = 1 + amount
}

// LF-EDT-06
TEST_F(Steps, U_STP_15_WhiteBalancePipetteNeutralizes)
{
    const base::Vec3 cast{0.30, 0.40, 0.52};
    const image::ImageBuffer field = t::imageFrom(8, 8, 3, [&](int, int, int c) {
        return static_cast<float>(c == 0 ? cast.x : c == 1 ? cast.y : cast.z);
    });
    const auto [temperature, tint] = WhiteBalanceStep::fromNeutral(cast);
    const image::ImageBuffer out = apply(
        film(FilmType::Slide)
            .with(entry(id::whitebalance, ParamSet{}.with("temperature", temperature).with("tint", tint))),
        field, FilmType::Slide);
    EXPECT_LE(t::deltaToNeutral(pixel(out, 3, 3)), 1.0);
}

// LF-EDT-08
TEST_F(Steps, U_STP_16_ToneCurveWithSixteenPoints)
{
    model::Curve master;
    model::Curve red;
    for (int i = 0; i < 16; ++i) {
        const double x = i / 15.0;
        master.points.push_back({x, std::pow(x, 0.8)});
        red.points.push_back({x, std::min(1.0, x * 1.1)});
    }
    for (std::size_t i = 0; i < master.points.size(); ++i) {
        EXPECT_NEAR(evaluateCurve(master, master.points[i].x), master.points[i].y, 1e-12);
        EXPECT_NEAR(evaluateCurve(red, red.points[i].x), red.points[i].y, 1e-12);
    }
    // monoton, ohne Überschwinger zwischen den Punkten (auch bei einem Knick)
    model::Curve kink{{{0, 0}, {0.3, 0.1}, {0.35, 0.8}, {1, 1}}};
    double prev = -1.0;
    for (int i = 0; i <= 1000; ++i) {
        const double v = evaluateCurve(kink, i / 1000.0);
        EXPECT_GE(v, prev - 1e-12);
        EXPECT_LE(v, 1.0 + 1e-12);
        prev = v;
    }
    // in der Pipeline: Tabelle trifft die Stützpunkte
    const image::ImageBuffer ramp =
        t::imageFrom(16, 1, 3, [](int x, int, int) { return static_cast<float>(x) / 15.0F; });
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::tonecurve, ParamSet{}.with("master", master))), ramp,
              FilmType::Slide);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(out.host()[static_cast<std::size_t>(i) * 3], master.points[static_cast<std::size_t>(i)].y,
                    1e-3);
    }
}

// LF-EDT-09
TEST_F(Steps, U_STP_17_SaturationAndVibrance)
{
    const image::ImageBuffer src = t::imageFrom(2, 1, 3, [](int x, int, int c) {
        const base::Vec3 strong{0.8, 0.2, 0.1};
        const base::Vec3 weak{0.45, 0.40, 0.38};
        const base::Vec3 v = x == 0 ? strong : weak;
        return static_cast<float>(c == 0 ? v.x : c == 1 ? v.y : v.z);
    });
    const image::ImageBuffer gray =
        apply(film(FilmType::Slide).with(entry(id::saturation, ParamSet{}.with("saturation", -1.0))), src,
              FilmType::Slide);
    EXPECT_LE(t::deltaToNeutral(pixel(gray, 0, 0)), 0.5);
    EXPECT_LE(t::deltaToNeutral(pixel(gray, 1, 0)), 0.5);

    const auto chroma = [](const base::Vec3& v) {
        return t::labFromRec2020(v).a * t::labFromRec2020(v).a +
               t::labFromRec2020(v).b * t::labFromRec2020(v).b;
    };
    const image::ImageBuffer vib =
        apply(film(FilmType::Slide).with(entry(id::saturation, ParamSet{}.with("vibrance", 0.5))), src,
              FilmType::Slide);
    const double gainStrong = std::sqrt(chroma(pixel(vib, 0, 0)) / chroma(pixel(src, 0, 0)));
    const double gainWeak = std::sqrt(chroma(pixel(vib, 1, 0)) / chroma(pixel(src, 1, 0)));
    EXPECT_GT(gainWeak, gainStrong); // Dynamik hebt wenig gesättigte Farben stärker
}

// LF-EDT-10
TEST_F(Steps, U_STP_18_SharpenWithThreshold)
{
    std::mt19937 rng(18);
    const image::ImageBuffer src = t::imageFrom(80, 40, 3, [&](int x, int y, int) {
        const float noise = static_cast<float>(rng() % 1000) / 1000.0F * 0.004F; // ±0,002
        return (x < 40 ? 0.2F : 0.7F) + (y > 20 && x > 50 ? noise : 0.0F);
    });
    const ParamSet p = ParamSet{}.with("amount", 1.0).with("radius", 1.5).with("threshold", 0.01);
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::sharpen, p)), src, FilmType::Slide);
    // Kante verstärkt
    EXPECT_GT(pixel(out, 40, 5).x - pixel(out, 39, 5).x, pixel(src, 40, 5).x - pixel(src, 39, 5).x);
    // Rauschfläche unverändert
    for (int y = 25; y < 38; ++y) {
        for (int x = 55; x < 78; ++x) {
            ASSERT_FLOAT_EQ(static_cast<float>(pixel(out, x, y).x), static_cast<float>(pixel(src, x, y).x));
        }
    }
    const auto* step = static_cast<const SharpenStep*>(registry.find(stepId(id::sharpen)));
    EXPECT_GE(step->halo(p, 1.0, {80, 40}), static_cast<int>(std::ceil(3 * 1.5)));
}

// LF-EDT-07
TEST_F(Steps, U_STP_19_ShadowsHighlightsWithoutHalo)
{
    const image::ImageBuffer src =
        t::imageFrom(200, 20, 3, [](int x, int, int) { return x < 100 ? 0.1F : 0.8F; });
    const ParamSet p = ParamSet{}.with("shadows", 0.5).with("highlights", 0.5); // Radius Standard 30
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::shadowshighlights, p)), src, FilmType::Slide);
    const double darkFar = pixel(out, 10, 10).x;
    const double brightFar = pixel(out, 190, 10).x;
    const double range = brightFar - darkFar;
    double worst = 0.0;
    for (int x = 0; x < 200; ++x) {
        const double v = pixel(out, x, 10).x;
        worst = std::max(worst, std::abs(v - (x < 100 ? darkFar : brightFar)));
    }
    EXPECT_LT(worst, 0.02 * range);
}

// LF-EDT-11
TEST_F(Steps, U_STP_20_DenoiseKeepsEdges)
{
    std::mt19937 rng(20);
    const image::ImageBuffer src = t::imageFrom(80, 40, 3, [&](int x, int, int) {
        const float noise = (static_cast<float>(rng() % 1000) / 1000.0F - 0.5F) * 0.04F;
        return (x < 40 ? 0.25F : 0.65F) + noise;
    });
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::denoise, ParamSet{}.with("strength", 0.3))), src,
              FilmType::Slide);
    const auto stddev = [](const image::ImageBuffer& img) {
        double s = 0;
        double s2 = 0;
        int n = 0;
        for (int y = 5; y < 35; ++y) {
            for (int x = 5; x < 30; ++x) {
                const double v = pixel(img, x, y).x;
                s += v;
                s2 += v * v;
                ++n;
            }
        }
        return std::sqrt(s2 / n - (s / n) * (s / n));
    };
    EXPECT_LT(stddev(out), 0.7 * stddev(src));
    const double edgeIn = t::meanRgb(src, 41, 5, 2, 30).x - t::meanRgb(src, 37, 5, 2, 30).x;
    const double edgeOut = t::meanRgb(out, 41, 5, 2, 30).x - t::meanRgb(out, 37, 5, 2, 30).x;
    EXPECT_GE(edgeOut, 0.9 * edgeIn);
}

// LF-EDT-12
TEST_F(Steps, U_STP_21_RetouchRemovesSpotWithoutEdge)
{
    // Strukturierte Fläche: Muster mit Periode 12 px senkrecht (passend zum Quellversatz) auf einem
    // Helligkeitsverlauf; dunkler Fleck bei (60, 30). Reines Klonen hinterließe am Rand die
    // Helligkeitsstufe des Verlaufs (12 px · 0,003), Heilen gleicht sie aus.
    const auto texture = [](int x, int y) {
        return 0.3 + 0.003 * y + 0.05 * std::sin(x * 0.7) * std::cos(y * 2.0 * std::numbers::pi / 12.0);
    };
    const image::ImageBuffer src = t::imageFrom(120, 60, 3, [&](int x, int y, int) {
        const bool spot = std::hypot(x - 60.0, y - 30.0) < 3.0;
        return static_cast<float>(spot ? 0.05 : texture(x, y));
    });
    const model::SpotList spots{{{60.5 / 120.0, 30.5 / 60.0}, {0.0, 12.0 / 60.0}, 6.0 / 120.0}};
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::retouch, ParamSet{}.with("spots", spots))), src,
              FilmType::Slide);
    EXPECT_NEAR(pixel(out, 60, 30).x, texture(60, 30), 0.005); // Fleck entfernt
    // Gradient am Rand der Stelle nicht höher als in der Umgebung
    const auto grad = [&](const image::ImageBuffer& img, int x, int y) {
        return std::abs(pixel(img, x + 1, y).x - pixel(img, x, y).x) +
               std::abs(pixel(img, x, y + 1).x - pixel(img, x, y).x);
    };
    double surround = 0.0;
    for (int y = 5; y < 50; ++y) {
        for (int x = 10; x < 40; ++x) {
            surround = std::max(surround, grad(src, x, y));
        }
    }
    double border = 0.0;
    for (int y = 20; y < 41; ++y) {
        for (int x = 50; x < 71; ++x) {
            const double d = std::hypot(x + 0.5 - 60.5, y + 0.5 - 30.5);
            if (d >= 3.0 && d < 8.0) { // weicher Rand der Stelle (4,2 … 6 px) samt Umgebung
                border = std::max(border, grad(out, x, y));
            }
        }
    }
    EXPECT_LE(border, surround * 1.1);
}

// LF-EDT-01
TEST(CropStepTest, U_STP_22_AspectRatiosAndNormalizedCoordinates)
{
    const std::pair<double, double> ratios[] = {{3, 2}, {4, 3}, {1, 1}, {5, 4}, {7, 6}, {65, 24}};
    for (const auto& [aw, ah] : ratios) {
        for (const base::Size canvas : {base::Size{600, 400}, base::Size{6000, 4000}}) {
            const model::NormRect r = CropStep::fitAspect({0.05, 0.1, 0.9, 0.8}, aw, ah, canvas);
            const double pw = r.w * canvas.width;
            const double ph = r.h * canvas.height;
            EXPECT_NEAR(pw / ph, aw / ah, 1e-9) << aw << ":" << ah;
        }
        // gleiche normierte Angabe auf Proxy und voller Auflösung
        const model::NormRect proxy = CropStep::fitAspect({0.05, 0.1, 0.9, 0.8}, aw, ah, {600, 400});
        const model::NormRect full = CropStep::fitAspect({0.05, 0.1, 0.9, 0.8}, aw, ah, {6000, 4000});
        EXPECT_NEAR(proxy.x, full.x, 1e-12);
        EXPECT_NEAR(proxy.w, full.w, 1e-12);
    }
}

// LF-EDT-02
TEST_F(Steps, U_STP_23_RotationWithoutEmptyCorners)
{
    const image::ImageBuffer src = testImage(120, 80);
    const image::ImageBuffer tiny =
        apply(film(FilmType::Slide)
                  .with(entry(id::rotation, ParamSet{}.with("angle", 0.05).with("autoFill", false))),
              src, FilmType::Slide);
    EXPECT_FALSE(bitEqual(tiny, src)); // 0,05° ist wirksam
    for (const double angle : {0.05, 7.3, -7.3}) {
        const image::ImageBuffer out =
            apply(film(FilmType::Slide)
                      .with(entry(id::rotation, ParamSet{}.with("angle", angle).with("autoFill", true))),
                  src, FilmType::Slide);
        for (const float v : out.host()) {
            ASSERT_GT(v, 0.01F) << "leere Ecke bei " << angle; // Testbild hat keine Werte unter 0,05
        }
    }
    const image::ImageBuffer empty =
        apply(film(FilmType::Slide)
                  .with(entry(id::rotation, ParamSet{}.with("angle", 7.3).with("autoFill", false))),
              src, FilmType::Slide);
    EXPECT_EQ(pixel(empty, 0, 0).x, 0.0); // ohne autoFill leere Ecken
}

// LF-EDT-03
TEST_F(Steps, U_STP_24_FlipTwiceIsLossless)
{
    const image::ImageBuffer src = testImage(31, 17);
    for (const char* flip : {"flipH", "flipV"}) {
        const EditList once = film(FilmType::Slide).with(entry(id::orientation, ParamSet{}.with(flip, true)));
        const image::ImageBuffer a = apply(once, src, FilmType::Slide);
        EXPECT_FALSE(bitEqual(a, src));
        EXPECT_TRUE(bitEqual(apply(once, a, FilmType::Slide), src)) << flip;
    }
}

// LF-BAS-07
TEST_F(Steps, U_STP_25_FlatFieldCorrection)
{
    const image::ImageBuffer blank = t::makeBlank(160, 120, 0.15);
    flatFields.add("leuchtplatte", blank);
    const image::ImageBuffer out =
        apply(film(FilmType::Slide)
                  .with(entry(id::flatfield, ParamSet{}.with("profile", std::string("leuchtplatte")))),
              blank, FilmType::Slide);
    const double center = t::meanRgb(out, 75, 55, 10, 10).y;
    double worst = 0.0;
    for (const auto& [x, y] :
         {std::pair{2, 2}, std::pair{150, 2}, std::pair{2, 110}, std::pair{150, 110}, std::pair{75, 2}}) {
        worst = std::max(worst, std::abs(t::meanRgb(out, x, y, 6, 6).y - center) / center);
    }
    EXPECT_LE(worst, 0.02);
    // Lichtfarbe ausgeglichen: Kanäle gleich
    const base::Vec3 c = t::meanRgb(out, 75, 55, 10, 10);
    EXPECT_NEAR(c.x, c.z, 0.01);
}

// NF-QUAL-01
TEST_F(Steps, U_STP_26_NoBandingIn16Bit)
{
    const int w = 20000;
    const image::ImageBuffer ramp = t::imageFrom(w, 1, 3, [](int x, int, int) {
        return static_cast<float>(0.2 + 0.1 * (x + 0.5) / w); // weicher Verlauf
    });
    // Standardstufen eines Farbnegativs nach der Konvertierung
    const EditList l =
        film(FilmType::Slide)
            .with(entry(id::exposure, ParamSet{}.with("ev", 0.3)))
            .with(entry(id::contrast, ParamSet{}.with("amount", 0.2)))
            .with(entry(id::tonecurve, ParamSet{}.with("master", model::Curve{{{0, 0}, {0.5, 0.6}, {1, 1}}})))
            .with(entry(id::saturation, ParamSet{}.with("saturation", 0.1)));
    const image::ImageBuffer out = apply(l, ramp, FilmType::Slide);
    const auto v = out.host();
    std::vector<int> q;
    for (std::size_t i = 0; i < v.size(); i += 3) {
        q.push_back(static_cast<int>(std::lround(std::clamp(v[i], 0.0F, 1.0F) * 65535.0F)));
    }
    int maxJump = 0;
    std::size_t distinct = 1;
    for (std::size_t i = 1; i < q.size(); ++i) {
        ASSERT_GE(q[i], q[i - 1]); // monoton
        maxJump = std::max(maxJump, q[i] - q[i - 1]);
        distinct += q[i] != q[i - 1] ? 1U : 0U;
    }
    EXPECT_LE(maxJump, 1);
    EXPECT_GE(static_cast<double>(distinct), 0.99 * (q.back() - q.front() + 1));
}

// LF-KON-10
TEST(Multigrade, U_STP_27_RealBehaviourOfGradesFourToFive)
{
    const MultigradeModel m;
    // ohne Ausgleich: Stufe 3½ unverändert, ab Stufe 4 doppelte Belichtung für gleichen Mittelton
    EXPECT_NEAR(m.curve(3.5, false).density(0.0), m.curve(3.5, true).density(0.0), 1e-12);
    const double shift = std::log10(2.0);
    for (const double g : {4.0, 5.0}) {
        EXPECT_NEAR(m.curve(g, false).density(shift), m.curve(g, true).density(0.0), 1e-12) << g;
        EXPECT_LT(m.curve(g, false).density(0.0), m.curve(g, true).density(0.0)) << "ohne Ausgleich heller";
    }
}

// LF-GEO-04
TEST_F(Steps, U_STP_28_SafetyInsetShrinksAllEdgesEqually)
{
    const model::NormRect rect{0.1, 0.1, 0.8, 0.6};
    const base::Point2 canvas{600, 400};
    for (const double inset : {0.0, 0.05, 0.10}) {
        const model::NormRect r = CropStep::applyInset(rect, inset, canvas);
        const double left = (r.x - rect.x) * canvas.x;
        const double top = (r.y - rect.y) * canvas.y;
        const double right = ((rect.x + rect.w) - (r.x + r.w)) * canvas.x;
        const double bottom = ((rect.y + rect.h) - (r.y + r.h)) * canvas.y;
        EXPECT_NEAR(left, top, 1e-9);
        EXPECT_NEAR(left, right, 1e-9);
        EXPECT_NEAR(left, bottom, 1e-9);
        EXPECT_NEAR(left, inset * std::min(0.8 * 600, 0.6 * 400), 1e-9);
    }
    // in der Pipeline: Ausgabe schrumpft entsprechend
    const image::ImageBuffer src = testImage(600, 400);
    const image::ImageBuffer out =
        apply(film(FilmType::Slide).with(entry(id::crop, ParamSet{}.with("rect", rect).with("inset", 0.05))),
              src, FilmType::Slide);
    EXPECT_EQ(out.spec().width, 480 - 24);
    EXPECT_EQ(out.spec().height, 240 - 24);
}

} // namespace
} // namespace autoneg::steps
