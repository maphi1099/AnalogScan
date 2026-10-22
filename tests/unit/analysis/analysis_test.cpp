// Unit-Tests der Analyse (Architektur 9.7): Bildfeld, Bereiche, Schräglage, Basis,
// Tonwertgrenzen, Neutralisierung, Histogramm. IVision ist durch FakeVision ersetzt,
// Testbilder sind synthetisch (tests/support/synthetic.hpp).

#include "autoneg/analysis/base_meter.hpp"
#include "autoneg/analysis/frame_detector.hpp"
#include "autoneg/analysis/tone.hpp"
#include "autoneg/mocks/fake_vision.hpp"
#include "autoneg/testing/color_metrics.hpp"
#include "autoneg/testing/synthetic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace autoneg::analysis {
namespace {

using model::FilmType;
namespace t = autoneg::testing;

/// Abweichung der vier Kanten in Pixeln.
double worstEdge(const model::NormRect& a, const model::NormRect& b, int w, int h)
{
    return std::max({std::abs(a.x - b.x) * w, std::abs(a.y - b.y) * h, std::abs(a.x + a.w - b.x - b.w) * w,
                     std::abs(a.y + a.h - b.y - b.h) * h});
}

t::NegativeSpec spec(FilmType film = FilmType::ColorNegative)
{
    t::NegativeSpec s;
    s.width = 600;
    s.height = 400;
    s.film = film;
    return s;
}

/// Konvertierung wie autoneg.filmbase + autoneg.density + autoneg.densityrange (Negativ, R = 2).
image::ImageBuffer convert(const image::ImageBuffer& scan, const base::Vec3& base, const DensityRange& r)
{
    const auto& s = scan.spec();
    const double b[3] = {base.x, base.y, base.z};
    const double lo[3] = {r.dMin.x, r.dMin.y, r.dMin.z};
    const double hi[3] = {r.dMax.x, r.dMax.y, r.dMax.z};
    return t::imageFrom(s.width, s.height, 3, [&](int x, int y, int c) {
        const auto k = static_cast<std::size_t>(c);
        const double v = scan.host()[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                                      static_cast<std::size_t>(x)) *
                                         3 +
                                     k];
        const double d = -std::log10(std::max(v / b[k], 1e-9));
        const double n = (d - lo[k]) / std::max(hi[k] - lo[k], 1e-4);
        return static_cast<float>((std::pow(10.0, 2.0 * (n - 1.0)) - 0.01) / 0.99);
    });
}

image::ImageBuffer densityOf(const image::ImageBuffer& scan, const base::Vec3& base)
{
    const auto& s = scan.spec();
    const double b[3] = {base.x, base.y, base.z};
    return t::imageFrom(s.width, s.height, 3, [&](int x, int y, int c) {
        const auto k = static_cast<std::size_t>(c);
        const double v = scan.host()[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                                      static_cast<std::size_t>(x)) *
                                         3 +
                                     k];
        return static_cast<float>(-std::log10(std::max(v / b[k], 1e-9)));
    });
}

class Analysis : public ::testing::Test
{
protected:
    mocks::FakeVision vision;
    FrameDetector detector{vision};
    SkewEstimator skew{vision};
};

// LF-GEO-01
TEST_F(Analysis, U_ANA_01_FrameOnSyntheticNegative)
{
    for (const FilmType f : {FilmType::ColorNegative, FilmType::BwNegative, FilmType::Slide}) {
        SCOPED_TRACE(static_cast<int>(f));
        const auto s = spec(f);
        const t::SyntheticScan scan = t::makeNegative(s);
        const auto d = detector.detect(scan.scan, f);
        ASSERT_TRUE(d.has_value()) << d.error().message;
        EXPECT_LE(worstEdge(d->frame, scan.truth.frame, s.width, s.height), 1.0)
            << d->frame.x << " " << d->frame.y << " " << d->frame.w << " " << d->frame.h;
        EXPECT_NEAR(d->angleDeg, 0.0, 0.1);
        EXPECT_GT(d->confidence, 0.5);
    }
}

// LF-GEO-05
TEST_F(Analysis, U_ANA_02_RegionClassification)
{
    const auto s = spec();
    const t::SyntheticScan scan = t::makeNegative(s);
    const auto d = detector.detect(scan.scan, FilmType::ColorNegative);
    ASSERT_TRUE(d.has_value()) << d.error().message;
    const RegionMap& m = d->regions;
    EXPECT_EQ(m.classOf({0.5, 0.015}), RegionClass::Holder);    // Halter oben (4 % Höhe)
    EXPECT_EQ(m.classOf({0.5, 0.08}), RegionClass::Background); // Leuchtfläche zwischen Halter und Film
    EXPECT_EQ(m.classOf({0.05, 0.16}), RegionClass::Rebate);    // Filmrand oben, kein Loch
    EXPECT_EQ(m.classOf({0.5, 0.5}), RegionClass::Image);
    EXPECT_EQ(m.classOf({0.02, 0.5}), RegionClass::Rebate);  // Steg links neben dem Bildfeld
    EXPECT_EQ(m.classOf({0.5, 0.985}), RegionClass::Holder); // Halter unten
    ASSERT_FALSE(m.rebate().empty());
    const model::NormRect& top = m.rebate().front();
    EXPECT_NEAR(top.y, scan.truth.rebateTop.y, 1.5 / s.height);
    EXPECT_NEAR(top.y + top.h, scan.truth.rebateTop.y + scan.truth.rebateTop.h, 1.5 / s.height);
}

// LF-GEO-03
TEST_F(Analysis, U_ANA_03_SkewWithinTenthDegree)
{
    for (const double angle : {-5.0, -1.2, 0.0, 3.7, 5.0}) {
        SCOPED_TRACE(angle);
        auto s = spec();
        s.angleDeg = angle;
        const t::SyntheticScan scan = t::makeNegative(s);
        if (angle == 3.7) { // SkewEstimator direkt (sonst über detect, das ihn aufruft)
            const auto est = skew.estimate(scan.scan);
            ASSERT_TRUE(est.has_value()) << est.error().message;
            EXPECT_NEAR(*est, -angle, 0.1);
        }
        const auto d = detector.detect(scan.scan, FilmType::ColorNegative);
        ASSERT_TRUE(d.has_value()) << d.error().message;
        EXPECT_NEAR(d->angleDeg, -angle, 0.1); // Korrekturwinkel
        EXPECT_LE(worstEdge(d->frame, scan.truth.frame, s.width, s.height), 2.0);
    }
}

// LF-GEO-05
TEST_F(Analysis, U_ANA_04_NoFrameRecognizable)
{
    const image::ImageBuffer gray = t::imageFrom(400, 300, 3, [](int, int, int) { return 0.5F; });
    std::mt19937 rng(4);
    const image::ImageBuffer noise = t::imageFrom(
        400, 300, 3, [&](int, int, int) { return static_cast<float>(rng() % 10000) / 10000.0F; });
    for (const auto* img : {&gray, &noise}) {
        const auto d = detector.detect(*img, FilmType::ColorNegative);
        ASSERT_FALSE(d.has_value());
        EXPECT_EQ(d.error().code, base::ErrorCode::NotFound);
        EXPECT_FALSE(d.error().message.empty());
    }
}

// LF-GEO-06
TEST_F(Analysis, U_ANA_05_ThreeFramesInStrip)
{
    auto s = spec();
    s.frames = 3;
    s.frame = {0.04, 0.2, 0.28, 0.6};
    s.frameGap = 0.03;
    const t::SyntheticScan scan = t::makeNegative(s);
    ASSERT_EQ(scan.truth.frames.size(), 3U);
    const auto all = detector.detectAll(scan.scan, FilmType::ColorNegative);
    ASSERT_TRUE(all.has_value()) << all.error().message;
    ASSERT_EQ(all->size(), 3U);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_LE(worstEdge((*all)[i].frame, scan.truth.frames[i], s.width, s.height), 1.0) << "Feld " << i;
    }
}

double worstChannel(const base::Vec3& a, const base::Vec3& b)
{
    return std::max({std::abs(a.x - b.x) / b.x, std::abs(a.y - b.y) / b.y, std::abs(a.z - b.z) / b.z});
}

// LF-BAS-01
TEST_F(Analysis, U_ANA_06_BaseFromRebate)
{
    for (const FilmType f : {FilmType::ColorNegative, FilmType::BwNegative}) {
        const auto s = spec(f);
        const t::SyntheticScan scan = t::makeNegative(s);
        const auto d = detector.detect(scan.scan, f);
        ASSERT_TRUE(d.has_value()) << d.error().message;
        const auto m = BaseMeter{}.measure(scan.scan, d->regions);
        ASSERT_TRUE(m.has_value()) << m.error().message;
        EXPECT_LE(worstChannel(m->transmission, scan.truth.base), 0.02);
        EXPECT_EQ(m->method, BaseMethod::Rebate);
        EXPECT_GT(m->samples, 1000);
    }
}

// LF-BAS-02
TEST_F(Analysis, U_ANA_07_BaseRobustAgainstDisturbances)
{
    auto s = spec();
    s.edgeText = true;
    s.dust = 60;
    s.lightLeak = true;
    s.perforation = true;
    const t::SyntheticScan scan = t::makeNegative(s);
    const auto d = detector.detect(scan.scan, FilmType::ColorNegative);
    ASSERT_TRUE(d.has_value()) << d.error().message;
    const auto m = BaseMeter{}.measure(scan.scan, d->regions);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_LE(worstChannel(m->transmission, scan.truth.base), 0.02)
        << m->transmission.x << " " << m->transmission.y << " " << m->transmission.z;
}

// LF-BAS-04
TEST_F(Analysis, U_ANA_08_ManualRectangle)
{
    auto s = spec();
    s.dust = 40;
    const t::SyntheticScan scan = t::makeNegative(s);
    // Rechteck im oberen Filmrand zwischen zwei Perforationslöchern
    const model::NormRect r{0.032, scan.truth.rebateTop.y + 0.01, 0.025, scan.truth.rebateTop.h - 0.02};
    const auto m = BaseMeter{}.measureRect(scan.scan, r);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_NEAR(m->transmission.x, scan.truth.base.x, 1e-6);
    EXPECT_NEAR(m->transmission.y, scan.truth.base.y, 1e-6);
    EXPECT_NEAR(m->transmission.z, scan.truth.base.z, 1e-6);
    EXPECT_EQ(m->method, BaseMethod::Manual);
}

// LF-BAS-05
TEST_F(Analysis, U_ANA_09_StatisticalBaseGivesNeutralGrays)
{
    auto s = spec();
    s.rebate = false;
    s.holder = false;
    s.frame = {0.0, 0.0, 1.0, 1.0};
    const t::SyntheticScan scan = t::makeNegative(s);
    const BaseMeasurement b = BaseEstimator{}.estimate(scan.scan);
    EXPECT_EQ(b.method, BaseMethod::Statistical);
    const DensityRange r = AutoLevels{}.compute(densityOf(scan.scan, b.transmission), 0.0);
    const image::ImageBuffer out = convert(scan.scan, b.transmission, r);
    const int gw = s.width / 6;
    for (int i = 0; i < 6; ++i) {
        const base::Vec3 g = t::meanRgb(out, i * gw + gw / 4, s.height / 20, gw / 2, s.height / 10);
        EXPECT_LE(t::deltaToNeutral(g), 3.0) << "Graufeld " << i;
    }
}

// LF-KON-05
TEST(AutoLevelsTest, U_ANA_10_ClipShare)
{
    std::mt19937 rng(10);
    const image::ImageBuffer d = t::imageFrom(400, 250, 3, [&](int, int, int c) {
        return static_cast<float>(0.1 + 0.3 * c + 1.5 * static_cast<double>(rng() % 100000) / 100000.0);
    });
    for (const double clip : {0.0, 0.005, 0.02}) {
        const DensityRange r = AutoLevels{}.compute(d, clip);
        const double lo[3] = {r.dMin.x, r.dMin.y, r.dMin.z};
        const double hi[3] = {r.dMax.x, r.dMax.y, r.dMax.z};
        for (std::size_t c = 0; c < 3; ++c) {
            std::size_t out = 0;
            const auto v = d.host();
            for (std::size_t i = c; i < v.size(); i += 3) {
                out += (v[i] < lo[c] || v[i] > hi[c]) ? 1U : 0U;
            }
            EXPECT_NEAR(static_cast<double>(out) / static_cast<double>(v.size() / 3), clip, 0.001)
                << "clip " << clip << " Kanal " << c;
        }
    }
}

// LF-KON-06
TEST(AutoNeutralTest, U_ANA_11_RemovesColorCast)
{
    const base::Vec3 cast{1.25, 1.0, 0.75};
    const image::ImageBuffer positive = t::positiveImage(t::Pattern::Chart, 600, 400);
    const image::ImageBuffer tinted = t::imageFrom(600, 400, 3, [&](int x, int y, int c) {
        const double k = c == 0 ? cast.x : c == 1 ? cast.y : cast.z;
        return static_cast<float>(
            positive.host()[(static_cast<std::size_t>(y) * 600 + static_cast<std::size_t>(x)) * 3 +
                            static_cast<std::size_t>(c)] *
            k);
    });
    const base::Vec3 n = AutoNeutral{}.compute(tinted);
    const image::ImageBuffer corrected = t::imageFrom(600, 400, 3, [&](int x, int y, int c) {
        const double k = c == 0 ? n.y / n.x : c == 1 ? 1.0 : n.y / n.z;
        return static_cast<float>(
            tinted.host()[(static_cast<std::size_t>(y) * 600 + static_cast<std::size_t>(x)) * 3 +
                          static_cast<std::size_t>(c)] *
            k);
    });
    const int gw = 600 / 6;
    for (int i = 0; i < 6; ++i) {
        const base::Vec3 g = t::meanRgb(corrected, i * gw + gw / 4, 20, gw / 2, 40);
        EXPECT_LE(t::deltaToNeutral(g), 2.0) << "Graufeld " << i;
    }
}

// LF-EDT-13, LF-EDT-14
TEST(HistogramTest, U_ANA_12_HistogramAndClipShares)
{
    const image::ImageBuffer img = t::imageFrom(100, 100, 3, [](int x, int y, int) {
        const int i = y * 100 + x;
        return i < 100 ? 0.0F : i < 300 ? 1.0F : 0.5F; // 1 % schwarz, 2 % weiß
    });
    const image::Histogram h = HistogramCalculator{}.compute(img, 256);
    EXPECT_DOUBLE_EQ(h.clipLow.x, 0.01);
    EXPECT_DOUBLE_EQ(h.clipLow.z, 0.01);
    EXPECT_DOUBLE_EQ(h.clipHigh.y, 0.02);
    for (const auto& bins : h.bins) {
        ASSERT_EQ(bins.size(), 256U);
        std::uint64_t sum = 0;
        for (const auto b : bins) {
            sum += b;
        }
        EXPECT_EQ(sum, 10000U);
    }
    EXPECT_EQ(h.bins[image::Histogram::red][0], 100U);
    EXPECT_EQ(h.bins[image::Histogram::red][255], 200U);
}

} // namespace
} // namespace autoneg::analysis
