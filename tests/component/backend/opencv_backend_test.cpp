// CpuBackend ruft für Unschärfe, Rauschminderung, Drehen und Verkleinern OpenCV auf (D-21,
// Standardfunktionen vor Eigenbau). Diese Tests sichern, dass die Aufrufe genau den
// OpenCV-Funktionen entsprechen (Parameter, Ränder, Pixelmitten) und die Eigenschaften der
// Anwendung erhalten bleiben: gleiche Ergebnisse in Kacheln, unabhängig von der Threadzahl.

#include "backends/cpu/cpu_backend.hpp"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace autoneg::backend::cpu {
namespace {

using image::ImageBuffer;

const ports::KernelContext ctx{};

ImageBuffer randomImage(int w, int h, int ch, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.0F, 1.0F);
    std::vector<float> v(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(ch));
    // weicher Verlauf plus Rauschen: wie ein Bild mit Korn
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < ch; ++c) {
                v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) *
                      static_cast<std::size_t>(ch) +
                  static_cast<std::size_t>(c)] =
                    0.2F + 0.5F * static_cast<float>(x) / static_cast<float>(w) + 0.1F * (u(rng) - 0.5F);
            }
        }
    }
    return *ImageBuffer::fromFloats({w, h, ch, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v);
}

cv::Mat matOf(const ImageBuffer& b)
{
    const auto& s = b.spec();
    return cv::Mat(s.height, s.width, CV_32FC(s.channels), const_cast<float*>(b.host().data())).clone();
}

double maxAbs(const cv::Mat& a, const ImageBuffer& b)
{
    cv::Mat d;
    cv::absdiff(a, matOf(b), d);
    double m = 0.0;
    cv::minMaxLoc(d.reshape(1), nullptr, &m);
    return m;
}

// D-21: Unschärfe = cv::GaussianBlur mit Kern 2·⌈3σ⌉ + 1 und Randwiederholung
TEST(CpuBackendOpenCv, M10_GaussianIsOpenCv)
{
    CpuBackend cpu;
    const ImageBuffer in = randomImage(160, 90, 3, 1);
    for (const float sigma : {0.8F, 3.0F}) {
        const int r = std::max(1, static_cast<int>(std::ceil(3.0F * sigma)));
        cv::Mat ref;
        cv::GaussianBlur(matOf(in), ref, {2 * r + 1, 2 * r + 1}, sigma, sigma, cv::BORDER_REPLICATE);
        EXPECT_EQ(maxAbs(ref, cpu.gaussian(ctx, in, sigma).value()), 0.0) << sigma;
    }
}

// D-21: Rauschminderung „schnell“ = cv::bilateralFilter 5 × 5, σs 1,5, σr 0,02 + 0,2·Stärke
TEST(CpuBackendOpenCv, M10_FastDenoiseIsBilateral)
{
    CpuBackend cpu;
    const ImageBuffer in = randomImage(120, 80, 3, 2);
    cv::Mat ref;
    cv::bilateralFilter(matOf(in), ref, 5, 0.02 + 0.2 * 0.5, 1.5, cv::BORDER_REPLICATE);
    EXPECT_LE(maxAbs(ref, cpu.denoise(ctx, in, 0.5F, ports::DenoiseMethod::Fast).value()), 1e-6);
}

// Rauschminderung „stark“ (Non-Local Means, 8 Bit): mindert Rauschen deutlich stärker als
// „schnell“, lässt den Verlauf stehen und hat keine 8-Bit-Stufen im Ergebnis (nur die Änderung
// wird übertragen); unabhängig von der Threadzahl gleich
TEST(CpuBackendOpenCv, M10_StrongDenoiseRemovesGrainKeepsPrecision)
{
    CpuBackend cpu;
    const ImageBuffer in = randomImage(200, 120, 3, 3);
    const auto noise = [](const ImageBuffer& b) {
        // Rauschen = Abweichung vom waagerechten Mittel jeder Spalte
        const auto& s = b.spec();
        const auto v = b.host();
        const auto at = [&s, &v](int x, int y) {
            return v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) + static_cast<std::size_t>(x)) * 3];
        };
        double sum = 0.0;
        for (int x = 20; x < s.width - 20; ++x) {
            double mean = 0.0;
            for (int y = 20; y < s.height - 20; ++y) {
                mean += at(x, y);
            }
            mean /= s.height - 40;
            for (int y = 20; y < s.height - 20; ++y) {
                const double d = at(x, y) - mean;
                sum += d * d;
            }
        }
        return std::sqrt(sum / ((s.width - 40.0) * (s.height - 40.0)));
    };
    const ImageBuffer fast = cpu.denoise(ctx, in, 0.8F, ports::DenoiseMethod::Fast).value();
    const ImageBuffer strong = cpu.denoise(ctx, in, 0.8F, ports::DenoiseMethod::Strong).value();
    EXPECT_LT(noise(strong), 0.5 * noise(fast));
    // keine 8-Bit-Stufen: mehr verschiedene Werte als 256
    std::vector<float> values(strong.host().begin(), strong.host().end());
    std::ranges::sort(values);
    EXPECT_GT(std::ranges::unique(values).begin() - values.begin(), 1000);
    CpuBackend one(CpuBackend::Options{.threads = 1});
    const ImageBuffer single = one.denoise(ctx, in, 0.8F, ports::DenoiseMethod::Strong).value();
    EXPECT_TRUE(std::ranges::equal(single.hostBytes(), strong.hostBytes()));
}

// D-21: Drehen = cv::warpAffine (Lanczos-4) mit der eigenen Pixelmitten-Konvention
TEST(CpuBackendOpenCv, M10_WarpIsWarpAffine)
{
    CpuBackend cpu;
    const ImageBuffer in = randomImage(200, 140, 3, 4);
    const base::Mat2x3 rot = base::Mat2x3::translation(100, 70) * base::Mat2x3::rotation(2.0) *
                             base::Mat2x3::translation(-100, -70);
    const image::WarpPlan plan{rot, {200, 140}, image::Filter::Lanczos3};
    const auto& a = rot.m;
    const cv::Matx23d m(a[0], a[1], 0.5 * (a[0] + a[1]) + a[2] - 0.5, a[3], a[4], 0.5 * (a[3] + a[4]) + a[5] - 0.5);
    cv::Mat ref;
    cv::warpAffine(matOf(in), ref, m, {200, 140}, cv::INTER_LANCZOS4 | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
    const ImageBuffer got = cpu.warp(ctx, in, plan).value();
    // innen gleich; außerhalb der Quelle schwarz (Ecken)
    const cv::Rect inner(10, 10, 180, 120);
    cv::Mat d;
    cv::absdiff(ref(inner), matOf(got)(inner), d);
    double worst = 0.0;
    cv::minMaxLoc(d.reshape(1), nullptr, &worst);
    EXPECT_EQ(worst, 0.0);
    EXPECT_EQ(got.host()[0], 0.0F); // linke obere Ecke liegt nach 2° außerhalb der Quelle
}

// D-21: Verkleinern = cv::resize INTER_AREA
TEST(CpuBackendOpenCv, M10_ResampleIsInterArea)
{
    CpuBackend cpu;
    const ImageBuffer in = randomImage(300, 200, 3, 5);
    cv::Mat ref;
    cv::resize(matOf(in), ref, {111, 74}, 0, 0, cv::INTER_AREA);
    EXPECT_EQ(maxAbs(ref, cpu.resample(ctx, in, {111, 74}).value()), 0.0);
}

} // namespace
} // namespace autoneg::backend::cpu
