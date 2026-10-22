// Unit-Tests StepRegistry, PixelProgramBuilder, WarpBuilder, PipelineCompiler (Architektur 5, 9.3, 9.4).

#include "autoneg/mocks/ports.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pixel_program_builder.hpp"
#include "autoneg/pipeline/warp_builder.hpp"
#include "autoneg/reference/kernels.hpp"
#include "autoneg/testing/edit_fixtures.hpp"
#include "autoneg/testing/test_steps.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace autoneg::pipeline {
namespace {

using autoneg::testing::entry;
using autoneg::testing::id;
using model::EditList;
using model::FilmType;
namespace rank = autoneg::testing::rank;

const image::ImageSpec source{120, 80, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear};

RenderProfile exportProfile()
{
    RenderProfile p;
    p.mode = RenderMode::Export;
    return p;
}

RenderProfile previewProfile()
{
    RenderProfile p;
    p.mode = RenderMode::Preview;
    p.useCache = true;
    return p;
}

/// Alle Schritte aller Stufen, in Reihenfolge.
std::vector<std::string> stepsOf(const CompiledPipeline& cp)
{
    std::vector<std::string> out;
    for (const auto& s : cp.stages) {
        for (const auto& id : s->steps()) {
            out.push_back(id.value().substr(std::string("autoneg.").size()));
        }
    }
    return out;
}

/// Kurzform der Stufen, z. B. "P(filmbase,density) N(sharpen)".
std::string shape(const CompiledPipeline& cp)
{
    std::string out;
    for (const auto& s : cp.stages) {
        const char* k = s->kind() == StageKind::Pixel    ? "P"
                        : s->kind() == StageKind::Warp   ? "G"
                        : s->kind() == StageKind::Region ? "N"
                                                         : "O";
        out += out.empty() ? "" : " ";
        out += k;
        out += "(";
        bool first = true;
        for (const auto& id : s->steps()) {
            out += first ? "" : ",";
            out += id.value().substr(8);
            first = false;
        }
        out += ")";
        out += s->cacheAfter() ? "|" : "";
    }
    return out;
}

EditList withFilm(EditList l, const std::string& type)
{
    return l.with(entry("filmmode", rank::filmmode, "type", type));
}

// NF-ARC-04
TEST(StepRegistry, U_PIP_01_RegisterFindOrder)
{
    StepRegistry reg;
    using autoneg::testing::descriptor;
    using autoneg::testing::TestPointwise;
    const auto noop = [](PixelProgramBuilder& /*b*/, const model::ParamSet& /*p*/) {};
    ASSERT_TRUE(
        reg.add(std::make_unique<TestPointwise>(descriptor("sharpen", rank::sharpen, {}), noop)).has_value());
    ASSERT_TRUE(
        reg.add(std::make_unique<TestPointwise>(descriptor("crop", rank::crop, {}), noop)).has_value());
    ASSERT_TRUE(reg.add(std::make_unique<TestPointwise>(descriptor("exposure", rank::exposure, {}), noop))
                    .has_value());

    const auto dup = reg.add(std::make_unique<TestPointwise>(descriptor("crop", 999, {}), noop));
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error().code, base::ErrorCode::InvalidArgument);
    EXPECT_EQ(reg.size(), 3U);

    std::vector<std::string> order;
    for (const IStep* s : reg.ordered()) {
        order.push_back(s->descriptor().id.value());
    }
    EXPECT_EQ(order, (std::vector<std::string>{"autoneg.crop", "autoneg.exposure", "autoneg.sharpen"}));
    for (const char* name : {"crop", "exposure", "sharpen"}) {
        ASSERT_NE(reg.find(id(name)), nullptr) << name;
        EXPECT_EQ(reg.find(id(name))->descriptor().id, id(name));
    }
    EXPECT_EQ(reg.find(id("fehlt")), nullptr);
}

// NF-ARC-04, NF-PERF-01
TEST(PipelineCompiler, U_PIP_02_NeutralEntriesCreateNoStage)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "slide")
                              .with(entry("exposure", rank::exposure, "ev", 0.0))
                              .with(entry("contrast", rank::contrast, "amount", 0.0))
                              .with(entry("crop", rank::crop, "rect", model::NormRect::full()));

    const CompiledPipeline cp = compiler.compile(list, FilmType::Slide, exportProfile(), source);
    EXPECT_TRUE(cp.stages.empty()) << shape(cp);
}

// LF-KON-01, LF-KON-02, LF-KON-03, LF-KON-04
TEST(PipelineCompiler, U_PIP_03_ActiveStagesPerFilmType)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = EditList{}
                              .with(entry("bwmix", rank::bwmix, "weights", base::Vec3{0.3, 0.6, 0.1}))
                              .with(entry("filmbase", rank::filmbase, "base", base::Vec3{0.7, 0.4, 0.3}))
                              .with(entry("exposure", rank::exposure, "ev", 0.5))
                              .with(entry("whitebalance", rank::whitebalance, "temperature", 300.0))
                              .with(entry("gradation", rank::gradation, "grade", 3.0))
                              .with(entry("colorgrade", rank::colorgrade, "level", 2))
                              .with(entry("saturation", rank::saturation, "saturation", 0.2))
                              .with(entry("sharpen", rank::sharpen, "amount", 0.5));

    using V = std::vector<std::string>;
    // Tabelle 1 der Architektur
    EXPECT_EQ(
        stepsOf(compiler.compile(withFilm(list, "color"), FilmType::BwNegative, exportProfile(), source)),
        (V{"filmbase", "density", "exposure", "whitebalance", "colorgrade", "saturation", "sharpen"}));
    EXPECT_EQ(
        stepsOf(compiler.compile(withFilm(list, "bw"), FilmType::ColorNegative, exportProfile(), source)),
        (V{"bwmix", "filmbase", "density", "exposure", "gradation", "sharpen"}));
    EXPECT_EQ(
        stepsOf(compiler.compile(withFilm(list, "slide"), FilmType::ColorNegative, exportProfile(), source)),
        (V{"exposure", "whitebalance", "saturation", "sharpen"}));
    // ohne filmmode-Eintrag gilt der Filmtyp der Rolle
    EXPECT_EQ(stepsOf(compiler.compile(list, FilmType::Slide, exportProfile(), source)),
              (V{"exposure", "whitebalance", "saturation", "sharpen"}));
}

// LF-LST-06
TEST(PipelineCompiler, U_PIP_04_DisabledEntryCreatesNoStage)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList base = withFilm(EditList{}, "slide").with(entry("exposure", rank::exposure, "ev", 1.0));
    const EditList withSharpenOff =
        base.with(entry("sharpen", rank::sharpen, "amount", 0.8)).enabled(id("sharpen"), false);

    const CompiledPipeline a = compiler.compile(withSharpenOff, FilmType::Slide, exportProfile(), source);
    const CompiledPipeline b = compiler.compile(base, FilmType::Slide, exportProfile(), source);
    EXPECT_EQ(shape(a), "P(exposure)");
    EXPECT_EQ(a.keys, b.keys); // gleiches Ergebnis wie ohne Eintrag
}

// NF-PERF-01, NF-PERF-03
// Hinweis: Das Szenario erwartet "PixelPass(saturation, Ausgabe)" nach dem Schärfen. Laut
// Architektur 5.2 liegt saturation vor denoise/sharpen; geprüft wird nach der Architektur
// (Abweichung T-01 in Planning/abweichungen_planung.txt).
TEST(PipelineCompiler, U_PIP_05_PointwiseStagesAreFused)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "color")
                              .with(entry("filmbase", rank::filmbase, "base", base::Vec3{0.7, 0.4, 0.3}))
                              .with(entry("densityrange", rank::densityrange, "black", 0.1))
                              .with(entry("exposure", rank::exposure, "ev", 0.3))
                              .with(entry("contrast", rank::contrast, "amount", 0.2))
                              .with(entry("sharpen", rank::sharpen, "amount", 0.6))
                              .with(entry("saturation", rank::saturation, "saturation", 0.1));

    EXPECT_EQ(shape(compiler.compile(list, FilmType::ColorNegative, exportProfile(), source)),
              "P(filmbase,density,densityrange,exposure,contrast,saturation) N(sharpen)");

    // Mit Ausgabetransformation folgt eine Ausgabestufe.
    RenderProfile withOutput = exportProfile();
    withOutput.output.transform = std::make_shared<mocks::MockColorTransform>();
    EXPECT_EQ(shape(compiler.compile(list, FilmType::ColorNegative, withOutput, source)),
              "P(filmbase,density,densityrange,exposure,contrast,saturation) N(sharpen) O()");

    // Vorschau mit Cache: Cache-Punkte C3 (nach colorgrade) und C4 (nach saturation) trennen.
    EXPECT_EQ(shape(compiler.compile(list, FilmType::ColorNegative, previewProfile(), source)),
              "P(filmbase,density,densityrange,exposure)| P(contrast,saturation)| N(sharpen)");
}

// LF-EDT-02, LF-EDT-03
TEST(WarpBuilder, U_PIP_07_GeometryComposesToOneMapping)
{
    WarpBuilder turns({120, 80});
    for (int i = 0; i < 4; ++i) {
        turns.orient(1, false, false);
    }
    EXPECT_TRUE(turns.isIdentity());

    WarpBuilder flips({120, 80});
    flips.orient(0, true, false).orient(0, true, false).orient(0, false, true).orient(0, false, true);
    EXPECT_TRUE(flips.isIdentity());

    WarpBuilder quarter({120, 80});
    quarter.orient(1, false, false);
    EXPECT_FALSE(quarter.isIdentity());
    EXPECT_EQ(quarter.build().out, (base::Size{80, 120}));

    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "slide")
                              .with(entry("rotation", rank::rotation, "angle", 2.5))
                              .with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.1, 0.8, 0.7}));
    const CompiledPipeline cp = compiler.compile(list, FilmType::Slide, exportProfile(), source);
    EXPECT_EQ(shape(cp), "G(rotation,crop)");
    ASSERT_EQ(cp.stages.size(), 1U);
    EXPECT_EQ(static_cast<const WarpPass&>(*cp.stages[0]).plan().out, (base::Size{96, 56}));
}

// NF-PERF-01
TEST(PixelProgramBuilder, U_PIP_08_MatricesAndOffsetsAreFolded)
{
    const base::Mat3 m1{{1.1, 0.1, 0.0, 0.0, 0.9, 0.2, 0.05, 0.0, 1.2}};
    const base::Mat3 m2{{0.8, 0.0, 0.1, 0.2, 1.0, 0.0, 0.0, 0.3, 0.9}};
    const base::Vec3 o1{0.1, -0.2, 0.05};
    const base::Vec3 o2{-0.03, 0.04, 0.2};

    PixelProgramBuilder b;
    b.matrix(m1).matrix(m2).offset(o1).offset(o2);
    const image::PixelProgram folded = b.build();
    ASSERT_EQ(folded.ops.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<image::Matrix3>(folded.ops[0]));
    EXPECT_TRUE(std::holds_alternative<image::Offset3>(folded.ops[1]));

    image::PixelProgram plain;
    plain.ops = {image::Matrix3{m1}, image::Matrix3{m2}, image::Offset3{o1}, image::Offset3{o2}};
    std::mt19937 rng(7);
    for (int i = 0; i < 1000; ++i) {
        const std::array<float, 3> v{static_cast<float>(rng() % 1000) / 500.0F,
                                     static_cast<float>(rng() % 1000) / 500.0F,
                                     static_cast<float>(rng() % 1000) / 500.0F};
        const auto a = reference::applyProgram(folded, v, 3);
        const auto e = reference::applyProgram(plain, v, 3);
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(a[c], e[c], 1e-5F);
        }
    }

    PixelProgramBuilder s;
    s.scale({2, 3, 4}).scale({0.5, 0.5, 0.5}).clamp(0, 2).clamp(-1, 1);
    const image::PixelProgram sc = s.build();
    ASSERT_EQ(sc.ops.size(), 2U);
    EXPECT_EQ(std::get<image::Scale3>(sc.ops[0]).v, (base::Vec3{1, 1.5, 2}));
    EXPECT_EQ(std::get<image::Clamp>(sc.ops[1]), (image::Clamp{0, 1}));
}

// NF-PERF-01
TEST(CompiledPipeline, U_PIP_09_FirstDiffFindsChangedStage)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList base = withFilm(EditList{}, "color")
                              .with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.1, 0.8, 0.8}))
                              .with(entry("exposure", rank::exposure, "ev", 0.3))
                              .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    const CompiledPipeline a = compiler.compile(base, FilmType::ColorNegative, exportProfile(), source);
    const CompiledPipeline b = compiler.compile(base.with(entry("sharpen", rank::sharpen, "amount", 0.9)),
                                                FilmType::ColorNegative, exportProfile(), source);
    ASSERT_EQ(shape(a), "G(crop) P(density,exposure) N(sharpen)");
    EXPECT_EQ(a.firstDiff(b), 2U);
    EXPECT_EQ(a.firstDiff(a), a.stages.size());
}

// NF-ARC-09
TEST(PipelineCompiler, U_PIP_17_UnknownStepIsSkippedAndReported)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "slide")
                              .with(entry("exposure", rank::exposure, "ev", 1.0))
                              .with(entry("unbekannt", 105, "x", 1.0));

    const CompiledPipeline cp = compiler.compile(list, FilmType::Slide, exportProfile(), source);
    EXPECT_EQ(shape(cp), "P(exposure)");
    ASSERT_EQ(cp.warnings.size(), 1U);
    EXPECT_NE(cp.warnings[0].find("autoneg.unbekannt"), std::string::npos);
}

TEST(PipelineCompiler, M2_InvalidParameterIsSkippedAndReported)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "slide")
                              .with(entry("exposure", rank::exposure, "ev", 99.0)) // Bereich -5..5
                              .with(entry("contrast", rank::contrast, "amount", 0.5));

    const CompiledPipeline cp = compiler.compile(list, FilmType::Slide, exportProfile(), source);
    EXPECT_EQ(shape(cp), "P(contrast)");
    EXPECT_EQ(cp.warnings.size(), 1U);
}

TEST(PipelineCompiler, M2_UnfusedCompilesOneStagePerStep)
{
    const auto reg = autoneg::testing::standardRegistry();
    const PipelineCompiler compiler(*reg);
    const EditList list = withFilm(EditList{}, "slide")
                              .with(entry("exposure", rank::exposure, "ev", 1.0))
                              .with(entry("contrast", rank::contrast, "amount", 0.5));
    CompileOptions o;
    o.fuse = false;
    EXPECT_EQ(shape(compiler.compile(list, FilmType::Slide, exportProfile(), source, 1.0, o)),
              "P(exposure) P(contrast)");
}

} // namespace
} // namespace autoneg::pipeline
