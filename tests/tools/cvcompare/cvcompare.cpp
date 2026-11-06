// Vergleichswerkzeug: eigene Kernels des CpuBackend gegen die Standardfunktionen von OpenCV
// (Abweichung, PSNR, Zeit) an einem echten Scan. Grundlage für die Entscheidung, welche
// Kernels durch OpenCV ersetzt werden. Kein Teil der Anwendung, kein CTest.
//
//   autoneg_cvcompare <scan.raw> <ausgabeordner>
//
// Schreibt je Funktion Ausschnitte (eigen | OpenCV | 10-fache Differenz) als PNG.

#include "adapters/libraw/libraw_decoder.hpp"
#include "backends/cpu/cpu_backend.hpp"
#include "image/image_buffer.hpp"
#include "image/warp_plan.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace autoneg;

namespace {

cv::Mat toMat(const image::ImageBuffer& b)
{
    const auto& s = b.spec();
    cv::Mat m(s.height, s.width, CV_32FC(s.channels));
    std::copy(b.host().begin(), b.host().end(), m.ptr<float>());
    return m;
}

image::ImageBuffer fromMat(const cv::Mat& m)
{
    cv::Mat c = m.isContinuous() ? m : m.clone();
    image::ImageSpec s;
    s.width = c.cols;
    s.height = c.rows;
    s.channels = c.channels();
    return *image::ImageBuffer::fromFloats(
        s, std::span<const float>(c.ptr<float>(), c.total() * static_cast<std::size_t>(c.channels())));
}

/// Zeit in ms (Median aus drei Läufen).
double timeMs(const std::function<void()>& f)
{
    std::vector<double> t;
    for (int i = 0; i < 3; ++i) {
        const auto a = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
    }
    std::ranges::sort(t);
    return t[1];
}

struct Diff
{
    double maxAbs = 0;
    double meanAbs = 0;
    double psnr = 0;
};

/// Abweichung im Inneren (Rand von 16 px ausgenommen: Randbehandlung unterscheidet sich).
Diff compare(const cv::Mat& a, const cv::Mat& b)
{
    const cv::Rect inner(16, 16, a.cols - 32, a.rows - 32);
    cv::Mat d;
    cv::absdiff(a(inner), b(inner), d);
    double mx = 0;
    cv::minMaxLoc(d.reshape(1), nullptr, &mx);
    const cv::Scalar mean = cv::mean(d);
    double m = 0;
    for (int c = 0; c < a.channels(); ++c) {
        m += mean[c];
    }
    m /= a.channels();
    cv::Mat sq;
    cv::multiply(d, d, sq);
    const cv::Scalar ms = cv::mean(sq);
    double mse = 0;
    for (int c = 0; c < a.channels(); ++c) {
        mse += ms[c];
    }
    mse /= a.channels();
    return {mx, m, mse > 0 ? 10.0 * std::log10(1.0 / mse) : 999.0};
}

/// Ausschnitt eigen | OpenCV | Differenz ×10 als 8-Bit-PNG (Gamma 2,2 für die Ansicht).
void saveCrop(const fs::path& file, const cv::Mat& own, const cv::Mat& cv, cv::Rect r)
{
    r &= cv::Rect(0, 0, std::min(own.cols, cv.cols), std::min(own.rows, cv.rows));
    const auto view = [](const cv::Mat& m) {
        cv::Mat g;
        cv::pow(cv::max(m, 0.0), 1.0 / 2.2, g);
        cv::Mat u8;
        g.convertTo(u8, CV_8U, 255.0);
        cv::Mat bgr;
        cv::cvtColor(u8, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    };
    cv::Mat d;
    cv::absdiff(own(r), cv(r), d);
    cv::Mat d8;
    d.convertTo(d8, CV_8U, 2550.0);
    cv::cvtColor(d8, d8, cv::COLOR_RGB2BGR);
    cv::Mat row;
    cv::hconcat(std::vector<cv::Mat>{view(own(r)), view(cv(r)), d8}, row);
    cv::imwrite(file.string(), row);
}

void report(const char* name, const Diff& d, double tOwn, double tCv)
{
    std::printf("%-22s max %.5f  mittel %.6f  PSNR %6.1f dB   eigen %8.1f ms  OpenCV %8.1f ms\n", name, d.maxAbs,
                d.meanAbs, d.psnr, tOwn, tCv);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Aufruf: autoneg_cvcompare <scan.raw> <ausgabeordner>\n";
        return 2;
    }
    const fs::path outDir(argv[2]);
    fs::create_directories(outDir);
    adapter::libraw::LibRawDecoder decoder;
    const auto raw = decoder.decode(fs::path(argv[1]), {});
    if (!raw) {
        std::cerr << raw.error().message << "\n";
        return 1;
    }
    // Mittlerer Ausschnitt 3000 × 2000, als grobes Positiv (invertiert, normiert) wie nach der Konvertierung
    cv::Mat full = toMat(*raw);
    const cv::Rect mid((full.cols - 3000) / 2, (full.rows - 2000) / 2, 3000, 2000);
    cv::Mat img = full(mid).clone();
    double mx = 0;
    cv::minMaxLoc(img.reshape(1), nullptr, &mx);
    img = 1.0 - img / mx;
    cv::pow(cv::max(img, 0.0), 2.0, img); // etwas Kontrast, Werte 0 … 1 linear
    const image::ImageBuffer in = fromMat(img);
    backend::cpu::CpuBackend cpu;
    const ports::KernelContext ctx{};
    const cv::Rect crop(1300, 900, 300, 200);
    std::printf("Scan %s, Ausschnitt %d x %d, %d Kanäle\n\n", argv[1], img.cols, img.rows, img.channels());

    // Gauß-Unschärfe
    {
        const float sigma = 3.0F;
        image::ImageBuffer own;
        cv::Mat ref;
        const double tOwn = timeMs([&] { own = *cpu.gaussian(ctx, in, sigma); });
        const double tCv = timeMs([&] { cv::GaussianBlur(img, ref, cv::Size(0, 0), sigma, sigma, cv::BORDER_REPLICATE); });
        report("GaussianBlur", compare(toMat(own), ref), tOwn, tCv);
        saveCrop(outDir / "gaussian.png", toMat(own), ref, crop);
    }
    // Schärfen: Unschärfe + Maske |in − blur| > t
    {
        const float a = 1.0F, r = 2.0F, t = 0.01F;
        image::ImageBuffer own;
        cv::Mat ref;
        const double tOwn = timeMs([&] { own = *cpu.unsharp(ctx, in, a, r, t); });
        const double tCv = timeMs([&] {
            cv::Mat blur;
            cv::GaussianBlur(img, blur, cv::Size(0, 0), r, r, cv::BORDER_REPLICATE);
            cv::Mat diff = img - blur;
            cv::Mat mask = cv::abs(diff) > t;
            ref = img.clone();
            cv::Mat sharp;
            cv::addWeighted(img, 1.0 + a, blur, -a, 0.0, sharp);
            sharp.copyTo(ref, mask);
        });
        report("Unscharf maskieren", compare(toMat(own), ref), tOwn, tCv);
        saveCrop(outDir / "unsharp.png", toMat(own), ref, crop);
    }
    // Rauschminderung: bilateral (eigen L2-Farbabstand) gegen cv::bilateralFilter
    {
        const float strength = 0.5F;
        const float sigmaR = 0.02F + 0.2F * strength;
        image::ImageBuffer own;
        cv::Mat ref;
        const double tOwn = timeMs([&] { own = *cpu.denoise(ctx, in, strength); });
        const double tCv = timeMs([&] { cv::bilateralFilter(img, ref, 5, sigmaR, 1.5, cv::BORDER_REPLICATE); });
        report("bilateralFilter", compare(toMat(own), ref), tOwn, tCv);
        saveCrop(outDir / "bilateral.png", toMat(own), ref, crop);
        // „Stark“: Non-Local Means, nur 8 Bit
        cv::Mat nlm;
        const double tNlm = timeMs([&] {
            cv::Mat u8;
            img.convertTo(u8, CV_8UC3, 255.0);
            cv::Mat d;
            cv::fastNlMeansDenoisingColored(u8, d, 6.0F, 6.0F, 7, 21);
            d.convertTo(nlm, CV_32FC3, 1.0 / 255.0);
        });
        report("fastNlMeans (8 Bit)", compare(toMat(own), nlm), tOwn, tNlm);
        saveCrop(outDir / "nlmeans.png", toMat(own), nlm, crop);
        // Verlust allein durch 8 Bit (ohne Filter)
        cv::Mat q;
        img.convertTo(q, CV_8UC3, 255.0);
        q.convertTo(q, CV_32FC3, 1.0 / 255.0);
        report("nur 8-Bit-Rundung", compare(img, q), 0, 0);
    }
    // Lichter/Schatten: Guided Filter mit cv::boxFilter
    {
        const float sh = 0.5F, hl = 0.3F, r = 30.0F;
        image::ImageBuffer own;
        const double tOwn = timeMs([&] { own = *cpu.localTone(ctx, in, sh, hl, r); });
        // nur der Filterteil mit OpenCV: Basis gleich? Vergleich der Mittelwertfilter
        cv::Mat lum;
        cv::transform(img, lum, cv::Matx13f(0.2627F, 0.6780F, 0.0593F));
        cv::log(cv::max(lum, 1.0F / 65536.0F), lum);
        lum /= std::log(2.0F);
        cv::Mat ref;
        const int q = static_cast<int>(std::lround(r));
        const double tCv = timeMs([&] {
            const cv::Size k(2 * q + 1, 2 * q + 1);
            cv::Mat mI, mII, sq;
            cv::multiply(lum, lum, sq);
            cv::boxFilter(lum, mI, -1, k, {-1, -1}, true, cv::BORDER_REPLICATE);
            cv::boxFilter(sq, mII, -1, k, {-1, -1}, true, cv::BORDER_REPLICATE);
            cv::Mat var = cv::max(mII - mI.mul(mI), 0.0);
            cv::Mat a = var / (var + 0.01);
            cv::Mat b = mI - a.mul(mI);
            cv::Mat ma, mb;
            cv::boxFilter(a, ma, -1, k, {-1, -1}, true, cv::BORDER_REPLICATE);
            cv::boxFilter(b, mb, -1, k, {-1, -1}, true, cv::BORDER_REPLICATE);
            cv::Mat base = ma.mul(lum) + mb;
            base *= std::log(2.0F);
            cv::exp(base, base);
            // Ton wie detail::toneRow
            ref = img.clone();
            for (int y = 0; y < img.rows; ++y) {
                for (int x = 0; x < img.cols; ++x) {
                    const float bb = base.at<float>(y, x);
                    const float g = (1.0F + sh * 2.0F * std::max(0.0F, 0.5F - bb)) *
                                    (1.0F - hl * std::max(0.0F, bb - 0.5F));
                    ref.at<cv::Vec3f>(y, x) *= g;
                }
            }
        });
        report("Guided Filter (box)", compare(toMat(own), ref), tOwn, tCv);
        saveCrop(outDir / "guided.png", toMat(own), ref, crop);
    }
    // Feindrehung 1,5°: Lanczos-3 gegen warpAffine Lanczos-4
    {
        const double deg = 1.5;
        const double cx = img.cols / 2.0, cy = img.rows / 2.0;
        const base::Mat2x3 m = base::Mat2x3::translation(cx, cy) * base::Mat2x3::rotation(deg) *
                               base::Mat2x3::translation(-cx, -cy);
        image::WarpPlan plan{m, {img.cols, img.rows}, image::Filter::Lanczos3};
        image::ImageBuffer own;
        cv::Mat ref;
        const double tOwn = timeMs([&] { own = *cpu.warp(ctx, in, plan); });
        // eigene Abbildung: in = A·(out + 0,5) − 0,5 → OpenCV (Pixelmitten auf ganzen Zahlen)
        const auto& a = m.m;
        const cv::Matx23d M(a[0], a[1], a[0] * 0.5 + a[1] * 0.5 + a[2] - 0.5, a[3], a[4],
                            a[3] * 0.5 + a[4] * 0.5 + a[5] - 0.5);
        const double tCv = timeMs([&] {
            cv::warpAffine(img, ref, M, img.size(), cv::INTER_LANCZOS4 | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT,
                           cv::Scalar::all(0));
        });
        const cv::Rect inner(100, 100, img.cols - 200, img.rows - 200);
        report("warpAffine Lanczos4", compare(toMat(own)(inner), ref(inner)), tOwn, tCv);
        saveCrop(outDir / "warp.png", toMat(own), ref, crop);
    }
    // Verkleinern auf 1/5: Lanczos-3 mit Tiefpass gegen INTER_AREA bzw. INTER_LANCZOS4
    {
        const base::Size size{img.cols / 5, img.rows / 5};
        image::ImageBuffer own;
        cv::Mat area, lz;
        const double tOwn = timeMs([&] { own = *cpu.resample(ctx, in, size); });
        const double tArea = timeMs([&] { cv::resize(img, area, {size.width, size.height}, 0, 0, cv::INTER_AREA); });
        const double tLz = timeMs([&] { cv::resize(img, lz, {size.width, size.height}, 0, 0, cv::INTER_LANCZOS4); });
        report("resize INTER_AREA", compare(toMat(own), area), tOwn, tArea);
        report("resize LANCZOS4", compare(toMat(own), lz), tOwn, tLz);
        const cv::Rect small(200, 150, 150, 100);
        saveCrop(outDir / "resize_area.png", toMat(own), area, small);
        saveCrop(outDir / "resize_lanczos4.png", toMat(own), lz, small);
    }
    std::printf("\nBilder: %s\n", outDir.string().c_str());
    return 0;
}
