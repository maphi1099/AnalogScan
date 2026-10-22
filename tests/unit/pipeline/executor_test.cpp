// Unit-Tests PipelineExecutor, StageCache, SourceProvider, Backend-Auswahl (Architektur 6, 9.4).
// Rechen-Backend ist die skalare Referenz (tests/reference), Quellen kommen aus Attrappen.

#include "autoneg/mocks/ports.hpp"
#include "autoneg/pipeline/backend_selector.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/reference/reference_backend.hpp"
#include "autoneg/testing/edit_fixtures.hpp"
#include "autoneg/testing/test_steps.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace autoneg::pipeline {
namespace {

using autoneg::testing::entry;
using autoneg::testing::id;
using model::EditList;
using model::FilmType;
namespace rank = autoneg::testing::rank;

/// Glattes Testbild mit Feinstruktur, Werte 0,05…0,9.
image::ImageBuffer synthetic(int w, int h)
{
    std::vector<float> v(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                const double s =
                    0.5 + 0.4 * std::sin(x * 0.031 + y * 0.017 + c) * std::cos(x * 0.007 - y * 0.023);
                v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) *
                      3 +
                  static_cast<std::size_t>(c)] =
                    static_cast<float>(0.05 + 0.85 * s + 0.01 * ((x * 7 + y * 3) % 5));
            }
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v)
        .value();
}

/// Größte Abweichung relativ zu max(1, |a|).
double maxRelDiff(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    EXPECT_EQ(a.spec(), b.spec());
    const auto x = a.host();
    const auto y = b.host();
    double d = 0.0;
    for (std::size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
        d = std::max(d, std::abs(static_cast<double>(x[i]) - y[i]) /
                            std::max(1.0, std::abs(static_cast<double>(x[i]))));
    }
    return d;
}

bool bitEqual(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    return a.spec() == b.spec() && std::ranges::equal(a.hostBytes(), b.hostBytes());
}

/// Dekoder-Attrappe: liefert immer dasselbe Bild, zählt Aufrufe, optional mit Verzögerung.
class FakeDecoder final : public ports::IRawDecoder
{
public:
    explicit FakeDecoder(image::ImageBuffer img, std::chrono::milliseconds delay = {})
        : img_(std::move(img))
        , delay_(delay)
    {}
    base::Expected<model::SourceInfo> probe(const std::filesystem::path& /*p*/) override
    {
        return model::SourceInfo{img_.spec().width, img_.spec().height, "Test", "Attrappe",
                                 model::SourceFormat::Raw};
    }
    base::Expected<image::ImageBuffer> decode(const std::filesystem::path& /*p*/,
                                              const ports::DecodeOptions& /*o*/) override
    {
        ++decodes;
        std::this_thread::sleep_for(delay_);
        return img_;
    }
    base::Expected<image::ImageBuffer> preview(const std::filesystem::path& /*p*/) override { return img_; }

    std::atomic<int> decodes{0};

private:
    image::ImageBuffer img_;
    std::chrono::milliseconds delay_;
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
};

RenderProfile profile(RenderMode mode, bool cache = false)
{
    RenderProfile p;
    p.mode = mode;
    p.useCache = cache;
    return p;
}

class Executor : public ::testing::Test
{
protected:
    explicit Executor(image::ImageBuffer src = synthetic(64, 48))
        : decoder(std::move(src))
    {}

    RenderRequest request(EditList list, RenderProfile p, FilmType film = FilmType::ColorNegative)
    {
        RenderRequest r;
        r.frame = model::FrameId({1, 2, 3});
        r.path = "scan.raw";
        r.list = std::make_shared<const EditList>(std::move(list));
        r.film = film;
        r.profile = std::move(p);
        return r;
    }

    std::unique_ptr<StepRegistry> registry = autoneg::testing::standardRegistry();
    PipelineCompiler compiler{*registry};
    reference::ReferenceBackend backend;
    StageCache cache{std::size_t{1} << 30};
    FakeDecoder decoder;
    NoReader reader;
    SourceProvider sources{decoder, reader};
    PipelineExecutor executor{compiler, backend, cache, sources, TilePlanner(256)};
};

model::EditEntry shadowsHighlights(double shadows, double radius)
{
    model::EditEntry e = entry("shadowshighlights", rank::shadowshighlights, "shadows", shadows);
    e.params = e.params.with("radius", radius);
    return e;
}

EditList film(const std::string& type)
{
    return EditList{}.with(entry("filmmode", rank::filmmode, "type", type));
}

// NF-QUAL-02
TEST_F(Executor, U_PIP_06_FusedEqualsSeparate)
{
    const image::ImageBuffer src = synthetic(48, 32);
    std::mt19937_64 rng(20261022);
    const auto pick = [&rng](int n) { return static_cast<int>(rng() % static_cast<std::uint64_t>(n)); };
    const auto val = [&rng](double lo, double hi) {
        return lo + (hi - lo) * static_cast<double>(rng() % 10000) / 10000.0;
    };
    const char* types[] = {"color", "bw", "slide"};

    for (int i = 0; i < 25; ++i) {
        SCOPED_TRACE("Seed 20261022, Liste " + std::to_string(i));
        EditList l = film(types[pick(3)]);
        if (pick(2) == 0) {
            l = l.with(entry("filmbase", rank::filmbase, "base",
                             base::Vec3{val(0.5, 1), val(0.3, 0.8), val(0.2, 0.6)}));
        }
        if (pick(2) == 0) {
            l = l.with(entry("densityrange", rank::densityrange, "black", val(0, 0.2)));
        }
        if (pick(2) == 0) {
            l = l.with(entry("exposure", rank::exposure, "ev", val(-2, 2)));
        }
        if (pick(2) == 0) {
            l = l.with(entry("whitebalance", rank::whitebalance, "temperature", val(-800, 800)));
        }
        if (pick(2) == 0) {
            l = l.with(entry("gradation", rank::gradation, "grade", val(0, 5)));
        }
        if (pick(2) == 0) {
            l = l.with(entry("contrast", rank::contrast, "amount", val(-0.5, 0.5)));
        }
        if (pick(2) == 0) {
            l = l.with(entry("saturation", rank::saturation, "saturation", val(-0.5, 0.5)));
        }
        if (pick(3) == 0) {
            l = l.with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.05, 0.8, 0.9}));
        }
        if (pick(3) == 0) {
            l = l.with(entry("sharpen", rank::sharpen, "amount", val(0.1, 1)));
        }

        CompileOptions separate;
        separate.fuse = false;
        const RenderProfile p = profile(RenderMode::Export);
        const CompiledPipeline fused = compiler.compile(l, FilmType::ColorNegative, p, src.spec());
        const CompiledPipeline single =
            compiler.compile(l, FilmType::ColorNegative, p, src.spec(), 1.0, separate);
        const RenderRequest r = request(l, p);
        const auto a = executor.run(fused, src, r, Execution::Whole);
        const auto b = executor.run(single, src, r, Execution::Whole);
        ASSERT_TRUE(a && b);
        EXPECT_LE(maxRelDiff(*b, *a), 1e-5);
    }
}

// NF-QUAL-02
TEST_F(Executor, U_PIP_10_CachedEqualsUncached)
{
    const EditList l = film("color")
                           .with(entry("flatfield", rank::flatfield, "sigma", 1.0))
                           .with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.1, 0.8, 0.8}))
                           .with(entry("exposure", rank::exposure, "ev", 0.4))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    const auto cached1 = executor.execute(request(l, profile(RenderMode::Preview, true)));
    const auto cached2 = executor.execute(request(l, profile(RenderMode::Preview, true))); // aus dem Cache
    const auto plain = executor.execute(request(l, profile(RenderMode::Preview, false)));
    ASSERT_TRUE(cached1 && cached2 && plain);
    EXPECT_TRUE(bitEqual(*cached1, *plain));
    EXPECT_TRUE(bitEqual(*cached2, *plain));
}

// NF-PERF-01
TEST_F(Executor, U_PIP_11_ChangeRecomputesOnlyFromAffectedStage)
{
    const EditList l = film("color")
                           .with(entry("flatfield", rank::flatfield, "sigma", 1.0))
                           .with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.1, 0.8, 0.8}))
                           .with(entry("exposure", rank::exposure, "ev", 0.3))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    ASSERT_TRUE(executor.execute(request(l, profile(RenderMode::Preview, true))));
    EXPECT_EQ(backend.calls("gaussian"), 1);
    EXPECT_EQ(backend.calls("warp"), 1);

    backend.resetCalls();
    ASSERT_TRUE(executor.execute(
        request(l.with(entry("exposure", rank::exposure, "ev", 0.6)), profile(RenderMode::Preview, true))));
    EXPECT_EQ(backend.calls("gaussian"), 0); // Leuchtflächenkorrektur vor C1
    EXPECT_EQ(backend.calls("warp"), 0);     // Zuschnitt vor C2
    EXPECT_EQ(backend.calls("runPixel"), 1);
    EXPECT_EQ(backend.calls("unsharp"), 1);
}

// NF-PERF-07
TEST(StageCache, U_PIP_12_LeastRecentlyUsedIsEvicted)
{
    const image::ImageBuffer img = synthetic(10, 10); // 1200 Byte
    const model::FrameId a({1});
    const model::FrameId b({2});
    StageCache cache(3 * img.sizeBytes());
    cache.put(a, 1, img);
    cache.put(b, 2, img);
    cache.put(b, 3, img);
    EXPECT_TRUE(cache.find(1).has_value()); // 1 zuletzt genutzt
    cache.put(a, 4, img);                   // verdrängt 2
    cache.put(a, 5, img);                   // verdrängt 3
    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    EXPECT_FALSE(cache.contains(3));
    EXPECT_TRUE(cache.contains(4));
    EXPECT_TRUE(cache.contains(5));
    EXPECT_EQ(cache.usedBytes(), 3 * img.sizeBytes());

    cache.evict(a);
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.usedBytes(), 0U);
    cache.put(a, 6, synthetic(40, 40)); // größer als das Budget: nicht aufgenommen
    EXPECT_FALSE(cache.contains(6));
}

// NF-ARC-08, NF-PERF-07
TEST_F(Executor, U_PIP_13_TiledEqualsWhole)
{
    const EditList l = film("color")
                           .with(entry("flatfield", rank::flatfield, "sigma", 1.5))
                           .with(entry("rotation", rank::rotation, "angle", 3.0))
                           .with(entry("crop", rank::crop, "rect", model::NormRect{0.05, 0.05, 0.9, 0.9}))
                           .with(entry("exposure", rank::exposure, "ev", 0.2))
                           .with(shadowsHighlights(0.4, 2.0))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    for (const auto& [w, h] : {std::pair{1001, 777}, std::pair{2049, 3}}) {
        SCOPED_TRACE(std::to_string(w) + " x " + std::to_string(h));
        const image::ImageBuffer src = synthetic(w, h);
        const RenderProfile p = profile(RenderMode::Export);
        const CompiledPipeline cp = compiler.compile(l, FilmType::ColorNegative, p, src.spec());
        ASSERT_GE(cp.totalHalo(), 5);
        const RenderRequest r = request(l, p);
        const auto whole = executor.run(cp, src, r, Execution::Whole);
        const auto tiled = executor.run(cp, src, r, Execution::Tiled);
        ASSERT_TRUE(whole && tiled);
        EXPECT_LE(maxRelDiff(*whole, *tiled), 1e-6);
    }
}

// LF-GEO-01, LF-KON-05
TEST_F(Executor, U_PIP_14_StopAfterReturnsIntermediate)
{
    const base::Vec3 basis{0.7, 0.5, 0.4};
    const EditList l = film("color")
                           .with(entry("filmbase", rank::filmbase, "base", basis))
                           .with(entry("exposure", rank::exposure, "ev", 0.5))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    RenderRequest r = request(l, profile(RenderMode::Analysis));
    r.stopAfter = id("density");
    const auto out = executor.execute(r);
    ASSERT_TRUE(out);
    EXPECT_EQ(backend.calls("unsharp"), 0);

    const image::ImageBuffer input = synthetic(64, 48); // Puffer muss leben, solange src genutzt wird
    const auto src = input.host();
    const auto got = out->host();
    ASSERT_EQ(got.size(), src.size());
    const double b[3] = {basis.x, basis.y, basis.z};
    for (std::size_t i = 0; i < src.size(); i += 97) {
        const double want = -std::log10(std::max(src[i] / b[i % 3], 1e-6));
        EXPECT_NEAR(got[i], want, 1e-5) << i;
    }
}

// LF-BAT-09
TEST_F(Executor, U_PIP_15_CancelStopsAfterAtMostOneMoreTile)
{
    const image::ImageBuffer src = synthetic(1001, 777); // 4 x 4 Kacheln zu 256
    const EditList l = film("slide").with(entry("exposure", rank::exposure, "ev", 0.5));
    const RenderProfile p = profile(RenderMode::Export);
    RenderRequest r = request(l, p, FilmType::Slide);
    const image::CancelToken token = r.cancel;
    backend.setHook([token](int n) {
        if (n == 3) {
            token.cancel(); // Abbruch während Kachel 3
        }
    });
    const CompiledPipeline cp = compiler.compile(l, FilmType::Slide, p, src.spec());
    const auto out = executor.run(cp, src, r);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().code, base::ErrorCode::Cancelled);
    EXPECT_LE(backend.totalCalls(), 4);
}

// NF-ARC-16, NF-PERF-10
TEST(SourceProvider, U_PIP_16_ConcurrentRequestsDecodeOnce)
{
    FakeDecoder decoder(synthetic(32, 32), std::chrono::milliseconds(100));
    NoReader reader;
    SourceProvider sources(decoder, reader);
    const model::FrameId f({9});
    std::vector<image::ImageBuffer> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = sources.source(f, "scan.raw", 0).value(); });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(decoder.decodes.load(), 1);
    for (const auto& r : results) {
        EXPECT_EQ(r.storage().get(), results[0].storage().get());
    }
    sources.release(f);
    (void)sources.source(f, "scan.raw", 0);
    EXPECT_EQ(decoder.decodes.load(), 2);
}

// LF-KON-07
TEST_F(Executor, U_PIP_18_AllIntermediatesAreF32)
{
    const EditList l = film("color")
                           .with(entry("crop", rank::crop, "rect", model::NormRect{0.1, 0.1, 0.8, 0.8}))
                           .with(entry("filmbase", rank::filmbase, "base", base::Vec3{0.7, 0.5, 0.4}))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.5));
    for (const char* stop : {"crop", "filmbase", "density", "sharpen"}) {
        RenderRequest r = request(l, profile(RenderMode::Preview));
        r.stopAfter = id(stop);
        const auto out = executor.execute(r);
        ASSERT_TRUE(out) << stop;
        EXPECT_EQ(out->spec().format, image::PixelFormat::F32) << stop;
    }

    // Ein Backend, das kein F32 liefert, wird erkannt.
    mocks::MockComputeBackend bad;
    const auto u16 = image::ImageBuffer::allocateHost(
        image::ImageSpec{64, 48, 3, image::PixelFormat::U16, image::ColorSpace::WorkingLinear});
    EXPECT_CALL(bad, runPixel).WillRepeatedly(::testing::Return(base::Expected<image::ImageBuffer>(*u16)));
    PipelineExecutor badExecutor(compiler, bad, cache, sources);
    const auto out =
        badExecutor.execute(request(film("slide").with(entry("exposure", rank::exposure, "ev", 1.0)),
                                    profile(RenderMode::Preview), FilmType::Slide));
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().code, base::ErrorCode::Internal);
}

// NF-ARC-05, NF-PERF-08
TEST_F(Executor, U_BCK_06_UnavailableGpuFallsBackToCpu)
{
    mocks::MockComputeBackend gpu;
    EXPECT_CALL(gpu, available()).WillRepeatedly(::testing::Return(false));
    EXPECT_CALL(gpu, runPixel).Times(0);
    ports::IComputeBackend* preferred[] = {&gpu};
    ports::IComputeBackend& chosen = selectBackend(preferred, backend);
    EXPECT_EQ(&chosen, &backend);

    PipelineExecutor cpuExecutor(compiler, chosen, cache, sources);
    const auto out =
        cpuExecutor.execute(request(film("slide").with(entry("exposure", rank::exposure, "ev", 1.0)),
                                    profile(RenderMode::Preview), FilmType::Slide));
    EXPECT_TRUE(out.has_value());
}

TEST_F(Executor, M2_DetailRegionEqualsCropOfWhole)
{
    const EditList l = film("color")
                           .with(entry("exposure", rank::exposure, "ev", 0.3))
                           .with(entry("sharpen", rank::sharpen, "amount", 0.7));
    RenderProfile detail = profile(RenderMode::Detail);
    detail.roi = image::Roi{10, 7, 30, 20};
    const auto part = executor.execute(request(l, detail));
    const auto whole = executor.execute(request(l, profile(RenderMode::Export)));
    ASSERT_TRUE(part && whole);
    const auto expected = whole->copyRegion(*detail.roi);
    EXPECT_LE(maxRelDiff(*expected, *part), 1e-6);
}

TEST_F(Executor, M2_PreviewIsScaledToTarget)
{
    RenderProfile p = profile(RenderMode::Preview);
    p.target = {32, 32};
    const auto out = executor.execute(
        request(film("slide").with(entry("exposure", rank::exposure, "ev", 0.1)), p, FilmType::Slide));
    ASSERT_TRUE(out);
    EXPECT_EQ(out->spec().width, 32);
    EXPECT_EQ(out->spec().height, 24);
}

} // namespace
} // namespace autoneg::pipeline
