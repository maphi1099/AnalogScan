// Komponententest LcmsColorEngine mit echter Little CMS (K-07): ColorChecker-Sollwerte
// (Lab D50) aus dem Arbeitsfarbraum nach sRGB, Adobe RGB, ProPhoto und in ein Monitorprofil.
// Die Sollwerte im Zielraum rechnet der Test unabhängig (Bradford, Primärvalenzen, Kennlinie).

#include "test_files.hpp"

#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/testing/color_metrics.hpp"
#include "autoneg/testing/synthetic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <thread>
#include <vector>

namespace autoneg::adapter::lcms {
namespace {

namespace files = autoneg::testing::files;
namespace t = autoneg::testing;

using V3 = std::array<double, 3>;
using M3 = std::array<double, 9>;

V3 mul(const M3& m, const V3& v)
{
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

M3 mul(const M3& a, const M3& b)
{
    M3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                r[static_cast<std::size_t>(i * 3 + j)] +=
                    a[static_cast<std::size_t>(i * 3 + k)] * b[static_cast<std::size_t>(k * 3 + j)];
            }
        }
    }
    return r;
}

M3 inv(const M3& m)
{
    const double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    return {
        (m[4] * m[8] - m[5] * m[7]) / d, (m[2] * m[7] - m[1] * m[8]) / d, (m[1] * m[5] - m[2] * m[4]) / d,
        (m[5] * m[6] - m[3] * m[8]) / d, (m[0] * m[8] - m[2] * m[6]) / d, (m[2] * m[3] - m[0] * m[5]) / d,
        (m[3] * m[7] - m[4] * m[6]) / d, (m[1] * m[6] - m[0] * m[7]) / d, (m[0] * m[4] - m[1] * m[3]) / d};
}

V3 xyz(double x, double y)
{
    return {x / y, 1.0, (1.0 - x - y) / y};
}

constexpr V3 d50{0.9642, 1.0, 0.8249};

M3 bradford(const V3& from, const V3& to)
{
    const M3 b{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const V3 s = mul(b, from);
    const V3 d = mul(b, to);
    return mul(inv(b), mul(M3{d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]}, b));
}

/// RGB-linear -> XYZ D50 (Bradford-adaptiert, wie ICC-Matrixprofile).
M3 rgbToPcs(const std::array<double, 6>& p, const V3& white)
{
    const V3 r = xyz(p[0], p[1]);
    const V3 g = xyz(p[2], p[3]);
    const V3 b = xyz(p[4], p[5]);
    const M3 m{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const V3 s = mul(inv(m), white);
    const M3 ms{m[0] * s[0], m[1] * s[1], m[2] * s[2], m[3] * s[0], m[4] * s[1],
                m[5] * s[2], m[6] * s[0], m[7] * s[1], m[8] * s[2]};
    return mul(bradford(white, d50), ms);
}

V3 labToXyzD50(const V3& lab)
{
    const double fy = (lab[0] + 16.0) / 116.0;
    const double fx = fy + lab[1] / 500.0;
    const double fz = fy - lab[2] / 200.0;
    const auto finv = [](double f) {
        return f > 6.0 / 29.0 ? f * f * f : 3.0 * (6.0 / 29.0) * (6.0 / 29.0) * (f - 4.0 / 29.0);
    };
    return {d50[0] * finv(fx), d50[1] * finv(fy), d50[2] * finv(fz)};
}

V3 xyzD50ToLab(const V3& v)
{
    const auto f = [](double t) {
        return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
    };
    const double fx = f(v[0] / d50[0]);
    const double fy = f(v[1] / d50[1]);
    const double fz = f(v[2] / d50[2]);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

/// CIEDE2000 über die Testhilfe (Lab-Werte direkt).
double de00(const V3& a, const V3& b)
{
    return t::deltaE2000(t::Lab{a[0], a[1], a[2]}, t::Lab{b[0], b[1], b[2]});
}

/// Zielraum: Matrix (linear -> XYZ D50) und Kennlinie.
struct Target
{
    const char* name;
    M3 toPcs;
    std::function<double(double)> encode;
    std::function<double(double)> decode;
};

double srgbEncode(double v)
{
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

double srgbDecode(double v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

std::function<double(double)> gammaEncode(double g)
{
    return [g](double v) { return std::pow(std::max(v, 0.0), 1.0 / g); };
}

std::function<double(double)> gammaDecode(double g)
{
    return [g](double v) { return std::pow(std::max(v, 0.0), g); };
}

/// ColorChecker 24 (X-Rite, Lab D50), nur als Eingabe.
constexpr std::array<V3, 24> colorChecker{{
    {37.54, 14.37, 14.92},  {64.66, 19.27, 17.50},   {49.32, -3.82, -22.54}, {43.46, -12.74, 22.72},
    {54.94, 9.61, -24.79},  {70.48, -32.26, -0.37},  {62.73, 35.83, 56.50},  {39.43, 10.75, -45.17},
    {50.57, 48.64, 16.67},  {30.10, 22.54, -20.87},  {71.77, -24.13, 58.19}, {71.51, 18.24, 67.37},
    {28.37, 15.42, -49.80}, {54.38, -39.72, 32.27},  {42.43, 51.05, 28.62},  {81.80, 2.67, 80.41},
    {50.63, 51.28, -14.12}, {49.57, -29.71, -28.32}, {95.19, -1.03, 2.93},   {81.29, -0.57, 0.44},
    {66.89, -0.75, -0.06},  {50.76, -0.13, 0.14},    {35.63, -0.46, -0.48},  {20.64, 0.07, -0.46},
}};

ports::IccProfile monitorProfile()
{
    std::ifstream in(files::dataDir() / "monitor_p3_g22.icc", std::ios::binary);
    ports::IccProfile p;
    p.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    p.name = "Test-Monitor Display P3 Gamma 2.2";
    return p;
}

// LF-EXP-05, LF-UI-06
TEST(LcmsComponent, K_07_ColorCheckerThroughAllTargets)
{
    LcmsColorEngine engine;
    const V3 d65 = xyz(0.3127, 0.3290);
    const M3 workingToPcs = rgbToPcs({0.708, 0.292, 0.170, 0.797, 0.131, 0.046}, d65);
    const M3 pcsToWorking = inv(workingToPcs);

    // Eingabe: ColorChecker im Arbeitsfarbraum (linear Rec. 2020)
    std::vector<V3> lab(colorChecker.begin(), colorChecker.end());
    const image::ImageBuffer src = t::imageFrom(24, 1, 3, [&](int x, int, int c) {
        return static_cast<float>(
            mul(pcsToWorking, labToXyzD50(lab[static_cast<std::size_t>(x)]))[static_cast<std::size_t>(c)]);
    });

    const double u8gamma22 = 563.0 / 256.0; // Gamma 2,2 im ICC-Format u8Fixed8
    const std::vector<std::pair<ports::IccProfile, Target>> targets{
        {engine.profile(ports::ProfileId::Srgb).value(),
         {"sRGB", rgbToPcs({0.64, 0.33, 0.30, 0.60, 0.15, 0.06}, d65), srgbEncode, srgbDecode}},
        {engine.profile(ports::ProfileId::AdobeRgb).value(),
         {"Adobe RGB", rgbToPcs({0.64, 0.33, 0.21, 0.71, 0.15, 0.06}, d65), gammaEncode(563.0 / 256.0),
          gammaDecode(563.0 / 256.0)}},
        {engine.profile(ports::ProfileId::ProPhoto).value(),
         {"ProPhoto", rgbToPcs({0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001}, xyz(0.3457, 0.3585)),
          gammaEncode(1.8), gammaDecode(1.8)}},
        {monitorProfile(),
         {"Monitor P3", rgbToPcs({0.680, 0.320, 0.265, 0.690, 0.150, 0.060}, d65), gammaEncode(u8gamma22),
          gammaDecode(u8gamma22)}},
    };
    const ports::IccProfile working = engine.profile(ports::ProfileId::WorkingRec2020Linear).value();
    for (const auto& [icc, target] : targets) {
        SCOPED_TRACE(target.name);
        const auto x = engine.transform(working, icc, ports::Intent::RelativeColorimetric);
        ASSERT_TRUE(x.has_value()) << x.error().message;
        const auto out = (*x)->apply(src);
        ASSERT_TRUE(out.has_value());
        const M3 pcsToTarget = inv(target.toPcs);
        int checked = 0;
        double worst = 0.0;
        for (std::size_t i = 0; i < lab.size(); ++i) {
            const V3 lin = mul(pcsToTarget, labToXyzD50(lab[i]));
            if (std::ranges::any_of(lin, [](double v) { return v < 0.0 || v > 1.0; })) {
                continue; // außerhalb des Zielfarbraums: Klemmung ist Sache der Ausgabe
            }
            // Ergebnis der Engine zurück nach Lab (unabhängige Rechnung)
            V3 got{};
            for (std::size_t c = 0; c < 3; ++c) {
                got[c] = target.decode(out->host()[i * 3 + c]);
            }
            const double d = de00(xyzD50ToLab(mul(target.toPcs, got)), lab[i]);
            worst = std::max(worst, d);
            EXPECT_LE(d, 1.0) << "Feld " << i + 1;
            ++checked;
        }
        std::cout << "  " << target.name << ": " << checked << " Felder, größtes dE00 " << worst << "\n";
        EXPECT_GE(checked, 18);
    }
}

// LF-EXP-05: gleiche Transformation in mehreren Threads gleichzeitig
TEST(LcmsComponent, K_07_ConcurrentTransformsAreIdentical)
{
    LcmsColorEngine engine;
    const ports::IccProfile working = engine.profile(ports::ProfileId::WorkingRec2020Linear).value();
    const auto x = engine.transform(working, monitorProfile(), ports::Intent::Perceptual).value();
    const image::ImageBuffer src = t::imageFrom(512, 256, 3, [](int px, int py, int c) {
        return static_cast<float>(0.5 + 0.45 * std::sin(px * 0.031 + py * 0.017 + c));
    });
    const image::ImageBuffer reference = x->apply(src).value();
    std::vector<image::ImageBuffer> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = x->apply(src).value(); });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const auto& r : results) {
        EXPECT_TRUE(std::ranges::equal(r.hostBytes(), reference.hostBytes()));
    }
}

// NF-ARC-16 (B-35): Transformationen in mehreren Threads gleichzeitig anlegen. Little CMS
// beschreibt dabei für die Schwarzpunktkompensation einen statischen D50-Puffer
// (cmsD50_xyY); ohne Serialisierung meldet ThreadSanitizer ein Datenrennen.
TEST(LcmsComponent, M9_ConcurrentTransformCreationIsRaceFree)
{
    LcmsColorEngine first;
    LcmsColorEngine second; // eigener Kontext: der Puffer ist prozessweit
    const ports::IccProfile working = first.profile(ports::ProfileId::WorkingRec2020Linear).value();
    const ports::IccProfile srgb = first.profile(ports::ProfileId::Srgb).value();
    const ports::IccProfile gray = first.profile(ports::ProfileId::GrayGamma22).value();
    std::vector<std::thread> threads;
    std::vector<int> ok(8, 0);
    for (std::size_t i = 0; i < ok.size(); ++i) {
        threads.emplace_back([&, i] {
            LcmsColorEngine& engine = i % 2 == 0 ? first : second;
            for (std::size_t n = 0; n < 20; ++n) {
                const ports::IccProfile& dst = (i + n) % 2 == 0 ? srgb : gray;
                if (engine.transform(working, dst, ports::Intent::Perceptual).has_value()) {
                    ++ok[i];
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const int n : ok) {
        EXPECT_EQ(n, 20);
    }
}

// D-18, LF-EXP-05: alle mitgelieferten Ausgabeprofile haben eine geschlossene Matrix-Kurven-Form,
// die Little CMS auf zufälligen Farben (auch außerhalb 0…1) innerhalb von 1e-5 trifft
TEST(LcmsComponent, M10_MatrixShaperMatchesLcms)
{
    LcmsColorEngine engine;
    const ports::IccProfile working = engine.profile(ports::ProfileId::WorkingRec2020Linear).value();
    std::mt19937 rng(1810);
    std::uniform_real_distribution<float> u(-0.02F, 1.05F);
    const int w = 97;
    const int h = 31;
    std::vector<float> px(static_cast<std::size_t>(w * h * 3));
    for (float& v : px) {
        v = u(rng);
    }
    const image::ImageBuffer src =
        image::ImageBuffer::fromFloats({w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
                                       px)
            .value();
    for (const ports::ProfileId id : {ports::ProfileId::Srgb, ports::ProfileId::AdobeRgb,
                                      ports::ProfileId::ProPhoto, ports::ProfileId::GrayGamma22}) {
        for (const ports::Intent intent : {ports::Intent::RelativeColorimetric, ports::Intent::Perceptual}) {
            const auto x = engine.transform(working, engine.profile(id).value(), intent).value();
            const auto ms = x->matrixShaper();
            ASSERT_TRUE(ms.has_value()) << static_cast<int>(id);
            const image::ImageBuffer want = x->apply(src).value();
            ASSERT_EQ(want.spec().channels, ms->outChannels);
            const auto out = want.host();
            float worst = 0.0F;
            for (std::size_t i = 0; i < static_cast<std::size_t>(w * h); ++i) {
                for (int c = 0; c < ms->outChannels; ++c) {
                    const auto row = static_cast<std::size_t>(c) * 3;
                    const float lin = ms->matrix[row] * px[i * 3] + ms->matrix[row + 1] * px[i * 3 + 1] +
                                      ms->matrix[row + 2] * px[i * 3 + 2];
                    const float got = ms->encode(lin);
                    worst = std::max(worst, std::abs(got - out[i * static_cast<std::size_t>(ms->outChannels) +
                                                               static_cast<std::size_t>(c)]));
                }
            }
            EXPECT_LE(worst, 1e-5F) << static_cast<int>(id);
        }
    }
    // Absolut farbmetrisch: keine geschlossene Form (Weißpunktanpassung)
    const auto abs = engine.transform(working, engine.profile(ports::ProfileId::Srgb).value(),
                                      ports::Intent::AbsoluteColorimetric);
    EXPECT_FALSE(abs.value()->matrixShaper().has_value());
}

} // namespace
} // namespace autoneg::adapter::lcms
