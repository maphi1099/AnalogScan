// Komponententests der Kern-Pipeline mit allen Verarbeitungsschritten und dem echten
// CpuBackend (Highway + oneTBB). Bilder synthetisch im Speicher (tests/support/synthetic.hpp).

#include "autoneg/backend/cpu/cpu_backend.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/steps/other_steps.hpp"
#include "autoneg/steps/registry.hpp"
#include "autoneg/steps/step_ids.hpp"
#include "autoneg/testing/color_metrics.hpp"
#include "autoneg/testing/synthetic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace autoneg::steps {
namespace {

using model::EditList;
using model::FilmType;
using model::ParamSet;
using pipeline::RenderMode;
namespace t = autoneg::testing;

/// Dekoder-Attrappe für ein Bild im Speicher; halbe Auflösung als 2 × 2-Mittel (wie LibRaw half_size).
class MemoryDecoder final : public ports::IRawDecoder
{
public:
    explicit MemoryDecoder(image::ImageBuffer img)
        : full_(std::move(img))
        , half_(halve(full_))
    {}
    base::Expected<model::SourceInfo> probe(const std::filesystem::path& /*p*/) override
    {
        return model::SourceInfo{full_.spec().width, full_.spec().height, "Test", "synthetisch",
                                 model::SourceFormat::Raw};
    }
    base::Expected<image::ImageBuffer> decode(const std::filesystem::path& /*p*/,
                                              const ports::DecodeOptions& o) override
    {
        return o.half ? half_ : full_;
    }
    base::Expected<image::ImageBuffer> preview(const std::filesystem::path& /*p*/) override { return half_; }

private:
    static image::ImageBuffer halve(const image::ImageBuffer& in)
    {
        const auto& s = in.spec();
        const auto v = in.host();
        const auto at = [&](int x, int y, int c) {
            return v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                      static_cast<std::size_t>(x)) *
                         static_cast<std::size_t>(s.channels) +
                     static_cast<std::size_t>(c)];
        };
        return t::imageFrom(s.width / 2, s.height / 2, s.channels, [&](int x, int y, int c) {
            return 0.25F * (at(2 * x, 2 * y, c) + at(2 * x + 1, 2 * y, c) + at(2 * x, 2 * y + 1, c) +
                            at(2 * x + 1, 2 * y + 1, c));
        });
    }

    image::ImageBuffer full_;
    image::ImageBuffer half_;
};

class NoReader final : public ports::IImageReader
{
public:
    base::Expected<model::SourceInfo> probe(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
    base::Expected<image::ImageBuffer> read(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
    base::Expected<ports::IccProfile> profile(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
};

/// Pipeline mit allen Standardschritten und echtem CpuBackend für einen Scan im Speicher.
struct Rig
{
    explicit Rig(image::ImageBuffer scan, backend::cpu::CpuBackend::Options o = {})
        : backend(o)
        , decoder(std::move(scan))
    {
        EXPECT_TRUE(registerStandardSteps(registry, flatFields).has_value());
    }

    model::EditEntry entry(std::string_view stepId, ParamSet p) const
    {
        const pipeline::IStep* s = registry.find(steps::stepId(stepId));
        EXPECT_NE(s, nullptr) << stepId;
        model::EditEntry e;
        e.step = steps::stepId(stepId);
        e.rank = s != nullptr ? s->descriptor().rank : 0;
        e.version = s != nullptr ? s->descriptor().version : 1;
        e.params = std::move(p);
        return e;
    }

    EditList film(FilmType f) const
    {
        return EditList{}.with(
            entry(id::filmmode, ParamSet{}.with("type", std::string(FilmModeStep::toString(f)))));
    }

    image::ImageBuffer render(const EditList& l, FilmType f, RenderMode mode, base::Size target = {})
    {
        pipeline::RenderRequest r;
        r.frame = model::FrameId({4, 5, 6});
        r.path = "synthetisch.raw";
        r.list = std::make_shared<const EditList>(l);
        r.film = f;
        r.profile.mode = mode;
        r.profile.target = target;
        auto out = executor.execute(r);
        EXPECT_TRUE(out.has_value()) << (out ? "" : out.error().message);
        return out ? *out : image::ImageBuffer{};
    }

    FlatFieldCache flatFields;
    pipeline::StepRegistry registry;
    pipeline::PipelineCompiler compiler{registry};
    backend::cpu::CpuBackend backend;
    pipeline::StageCache cache{std::size_t{1} << 28};
    MemoryDecoder decoder;
    NoReader reader;
    pipeline::SourceProvider sources{decoder, reader};
    pipeline::PipelineExecutor executor{compiler, backend, cache, sources};
};

double luminance(const base::Vec3& v)
{
    return 0.2627 * v.x + 0.6780 * v.y + 0.0593 * v.z;
}

/// Sollbild des Positivs; bei S/W die Luminanz in allen drei Kanälen.
image::ImageBuffer expectedPositive(FilmType f, t::Pattern p, int w, int h)
{
    if (f != FilmType::BwNegative) {
        return t::positiveImage(p, w, h);
    }
    return t::imageFrom(w, h, 3, [&](int x, int y, int /*c*/) {
        return static_cast<float>(luminance(t::positiveAt(p, (x + 0.5) / w, (y + 0.5) / h)));
    });
}

/// Bearbeitung mit Standardeinstellungen: Bildfeld, Basis und Dichtegrenzen so, wie sie die
/// Automatiken (ab M4) als Einträge mit Herkunft „automatisch“ liefern; hier aus den Sollwerten.
EditList standardEdits(const Rig& rig, FilmType f, const t::NegativeTruth& truth)
{
    EditList l = rig.film(f).with(rig.entry(id::crop, ParamSet{}.with("rect", truth.frame)));
    if (f != FilmType::Slide) {
        l = l.with(rig.entry(id::filmbase, ParamSet{}.with("base", truth.base)))
                .with(rig.entry(id::densityrange,
                                ParamSet{}.with("dMin", truth.dMin).with("dMax", truth.dMax)));
    }
    return l;
}

/// Anspruchsvolle Bearbeitung mit allen Stufenarten (punktweise, geometrisch, regional).
EditList fullEdits(const Rig& rig, const t::NegativeTruth& truth)
{
    const model::SpotList spots{{{0.4, 0.5}, {0.05, 0.0}, 0.01}, {{0.7, 0.3}, {0.0, 0.04}, 0.015}};
    return standardEdits(rig, FilmType::ColorNegative, truth)
        .with(rig.entry(id::rotation, ParamSet{}.with("angle", 1.7).with("autoFill", true)))
        .with(rig.entry(id::retouch, ParamSet{}.with("spots", spots)))
        .with(rig.entry(id::exposure, ParamSet{}.with("ev", 0.25)))
        .with(rig.entry(id::whitebalance, ParamSet{}.with("temperature", 8.0).with("tint", -3.0)))
        .with(rig.entry(id::colorgrade, ParamSet{}.with("level", 2)))
        .with(rig.entry(id::shadowshighlights, ParamSet{}.with("shadows", 0.3).with("highlights", 0.2)))
        .with(rig.entry(id::contrast, ParamSet{}.with("amount", 0.15)))
        .with(rig.entry(id::saturation, ParamSet{}.with("saturation", 0.1).with("vibrance", 0.2)))
        .with(rig.entry(id::denoise, ParamSet{}.with("strength", 0.2)))
        .with(rig.entry(id::sharpen, ParamSet{}.with("amount", 0.5).with("radius", 1.0)));
}

bool bitEqual(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    return a.spec() == b.spec() && std::ranges::equal(a.hostBytes(), b.hostBytes());
}

// LF-KON-02, LF-KON-03, LF-KON-04
TEST(PipelineComponent, K_08_ConversionPerFilmTypeEndToEnd)
{
    for (const FilmType f : {FilmType::ColorNegative, FilmType::BwNegative, FilmType::Slide}) {
        SCOPED_TRACE(std::string(FilmModeStep::toString(f)));
        t::NegativeSpec spec;
        spec.width = 1200;
        spec.height = 800;
        spec.film = f;
        const t::SyntheticScan scan = t::makeNegative(spec);
        Rig rig(scan.scan);
        const image::ImageBuffer out = rig.render(standardEdits(rig, f, scan.truth), f, RenderMode::Export);
        ASSERT_EQ(out.spec().width, static_cast<int>(std::lround(scan.truth.frame.w * spec.width)));
        ASSERT_EQ(out.spec().height, static_cast<int>(std::lround(scan.truth.frame.h * spec.height)));

        const t::DeltaStats d =
            t::compareImages(expectedPositive(f, spec.pattern, out.spec().width, out.spec().height), out);
        EXPECT_LT(d.mean, 1.0);
        // Graufelder (obere Reihe des Testbilds) neutral
        const int gw = out.spec().width / 6;
        for (int i = 0; i < 6; ++i) {
            const base::Vec3 g =
                t::meanRgb(out, i * gw + gw / 4, out.spec().height / 20, gw / 2, out.spec().height / 10);
            EXPECT_LE(t::deltaToNeutral(g), 2.0) << "Graufeld " << i;
        }
    }
}

// NF-ARC-16, NF-QUAL-02
TEST(PipelineComponent, K_09_ResultIndependentOfThreadCount)
{
    t::NegativeSpec spec;
    spec.width = 1500;
    spec.height = 1000;
    spec.dust = 20;
    const t::SyntheticScan scan = t::makeNegative(spec);
    std::optional<image::ImageBuffer> first;
    for (const int threads : {1, 2, 8, 16}) {
        SCOPED_TRACE(threads);
        Rig rig(scan.scan, backend::cpu::CpuBackend::Options{.threads = threads});
        const image::ImageBuffer out =
            rig.render(fullEdits(rig, scan.truth), FilmType::ColorNegative, RenderMode::Export);
        ASSERT_GT(out.spec().width, 0);
        if (!first) {
            first = out;
        } else {
            EXPECT_TRUE(bitEqual(*first, out));
        }
    }
}

// NF-QUAL-02
TEST(PipelineComponent, K_10_PreviewMatchesExport)
{
    t::NegativeSpec spec;
    spec.width = 2400;
    spec.height = 1600;
    spec.dust = 10;
    const t::SyntheticScan scan = t::makeNegative(spec);
    Rig rig(scan.scan);
    const EditList l = fullEdits(rig, scan.truth);
    const image::ImageBuffer preview =
        rig.render(l, FilmType::ColorNegative, RenderMode::Preview, {600, 400});
    const image::ImageBuffer exported = rig.render(l, FilmType::ColorNegative, RenderMode::Export);
    ASSERT_GT(preview.spec().width, 0);
    const base::Size proxy{preview.spec().width, preview.spec().height};
    const auto shrunk = rig.backend.resample({}, exported, proxy);
    ASSERT_TRUE(shrunk.has_value());
    const t::DeltaStats d = t::compareImages(*shrunk, preview);
    EXPECT_LT(d.mean, 1.0) << "größte Abweichung " << d.max;
}

} // namespace
} // namespace autoneg::steps
