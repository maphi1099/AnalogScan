#include "backends/cpu/cpu_backend.hpp"

#include "kernels.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace autoneg::backend::cpu {

struct CpuBackend::Impl
{
    std::optional<tbb::task_arena> arena;

    template <class F>
    auto run(F&& f)
    {
        if (arena) {
            return arena->execute(std::forward<F>(f));
        }
        return f();
    }
};

namespace {

using Result = base::Expected<image::ImageBuffer>;

Result cancelled()
{
    return base::makeError(base::ErrorCode::Cancelled, "Berechnung abgebrochen");
}

base::Expected<void> requireHostF32(const image::ImageBuffer& in)
{
    if (in.empty() || in.spec().format != image::PixelFormat::F32 || in.host().empty()) {
        return base::makeError(base::ErrorCode::Unsupported,
                               "CpuBackend erwartet einen F32-Puffer im Hauptspeicher");
    }
    return {};
}

std::size_t rowLen(const image::ImageSpec& s)
{
    return s.samplesPerRow();
}

/// Ausgabepuffer, in den die Kernels schreiben; danach unveränderlich eingepackt.
struct Output
{
    image::ImageSpec spec;
    std::shared_ptr<image::HostStorage> storage;

    explicit Output(const image::ImageSpec& s)
        : spec(s)
        , storage(std::make_shared<image::HostStorage>(s.sizeBytes()))
    {}
    [[nodiscard]] float* row(int y) const
    {
        return storage->floats().data() + static_cast<std::size_t>(y) * spec.samplesPerRow();
    }
    [[nodiscard]] Result finish() { return image::ImageBuffer::wrap(spec, std::move(storage)); }
};

const float* rowOf(const image::ImageBuffer& b, int y)
{
    return b.host().data() + static_cast<std::size_t>(y) * b.spec().samplesPerRow();
}

// --- OpenCV (Standardfunktionen, wo sie gleich gut und schneller sind; CLAUDE.md) -------------

/// Eingabe als cv::Mat ohne Kopie (nur gelesen).
cv::Mat matOf(const image::ImageBuffer& b)
{
    const image::ImageSpec& s = b.spec();
    return {s.height, s.width, CV_32FC(s.channels), const_cast<float*>(b.host().data())};
}

/// Ausgabe als cv::Mat ohne Kopie.
cv::Mat matOf(const Output& o)
{
    return {o.spec.height, o.spec.width, CV_32FC(o.spec.channels), o.storage->floats().data()};
}

/// Kerngröße wie der Rand der Schritte (3 σ): OpenCV nähme sonst 4 σ, und Kachelränder wichen ab.
cv::Size gaussianSize(float sigma)
{
    const int r = std::max(1, static_cast<int>(std::ceil(3.0F * sigma)));
    return {2 * r + 1, 2 * r + 1};
}

/// Fehler aus OpenCV als Rückgabewert (keine Ausnahme über die Modulgrenze).
template <class F>
base::Expected<void> guarded(F&& f)
{
    try {
        f();
        return {};
    } catch (const cv::Exception& e) {
        return base::makeError(base::ErrorCode::Internal, std::string("OpenCV: ") + e.what());
    }
}

/// Zeilenblöcke parallel; prüft den Abbruch vor jedem Block. false bei Abbruch.
template <class Fn>
bool forBlocks(int height, const image::CancelToken& cancel, Fn&& fn)
{
    const int blocks = (height + CpuBackend::blockRows - 1) / CpuBackend::blockRows;
    std::atomic<bool> stopped{false};
    tbb::parallel_for(tbb::blocked_range<int>(0, blocks, 1), [&](const tbb::blocked_range<int>& r) {
        for (int b = r.begin(); b < r.end(); ++b) {
            if (cancel.cancelled()) {
                stopped = true;
                return;
            }
            fn(b * CpuBackend::blockRows, std::min(height, (b + 1) * CpuBackend::blockRows));
        }
    });
    return !stopped.load() && !cancel.cancelled();
}

/// Mittelwert über (2q + 1)² Werte mit Randwiederholung für eine Ebene mit einem Kanal.
/// Horizontal gleitende Summe je Zeile, vertikal gleitende Summe je Zeilenblock; der Startwert
/// wird je Block neu summiert (feste Blockeinteilung), daher unabhängig von der Threadzahl.
bool boxPlane(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int q,
              const image::CancelToken& cancel)
{
    const auto width = static_cast<std::size_t>(w);
    const auto at = [width](int x, int y) {
        return static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
    };
    std::vector<float> hsum(src.size());
    if (!forBlocks(h, cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                double acc = 0.0;
                for (int k = -q; k <= q; ++k) {
                    acc += src[at(std::clamp(k, 0, w - 1), y)];
                }
                hsum[at(0, y)] = static_cast<float>(acc);
                for (int x = 1; x < w; ++x) {
                    acc += src[at(std::min(x + q, w - 1), y)];
                    acc -= src[at(std::max(x - 1 - q, 0), y)];
                    hsum[at(x, y)] = static_cast<float>(acc);
                }
            }
        })) {
        return false;
    }
    const double n = static_cast<double>((2 * q + 1) * (2 * q + 1));
    return forBlocks(h, cancel, [&](int y0, int y1) {
        std::vector<double> acc(width, 0.0);
        const auto add = [&](int y, double sign) {
            const float* row = hsum.data() + at(0, std::clamp(y, 0, h - 1));
            for (std::size_t x = 0; x < width; ++x) {
                acc[x] += sign * row[x];
            }
        };
        for (int k = -q; k <= q; ++k) {
            add(y0 + k, 1.0);
        }
        for (int y = y0; y < y1; ++y) {
            float* o = dst.data() + at(0, y);
            for (std::size_t x = 0; x < width; ++x) {
                o[x] = static_cast<float>(acc[x] / n);
            }
            add(y + 1 + q, 1.0);
            add(y - q, -1.0);
        }
    });
}

// --- Filter für warp/resample (Definition: tests/reference) --------------------------------

float lanczos3(float x)
{
    x = std::abs(x);
    if (x < 1e-6F) {
        return 1.0F;
    }
    if (x >= 3.0F) {
        return 0.0F;
    }
    const float px = std::numbers::pi_v<float> * x;
    return 3.0F * std::sin(px) * std::sin(px / 3.0F) / (px * px);
}

float bicubic(float x)
{
    constexpr float a = -0.5F;
    x = std::abs(x);
    if (x <= 1.0F) {
        return ((a + 2.0F) * x - (a + 3.0F)) * x * x + 1.0F;
    }
    if (x < 2.0F) {
        return ((a * x - 5.0F * a) * x + 8.0F * a) * x - 4.0F * a;
    }
    return 0.0F;
}

float filterKernel(image::Filter f, float x)
{
    switch (f) {
    case image::Filter::Bilinear:
        return std::max(0.0F, 1.0F - std::abs(x));
    case image::Filter::Bicubic:
        return bicubic(x);
    case image::Filter::Lanczos3:
        return lanczos3(x);
    case image::Filter::Nearest:
        return 0.0F;
    }
    return 0.0F;
}

int filterSupport(image::Filter f)
{
    switch (f) {
    case image::Filter::Nearest:
    case image::Filter::Bilinear:
        return 1;
    case image::Filter::Bicubic:
        return 2;
    case image::Filter::Lanczos3:
        return 3;
    }
    return 3;
}

bool isExactAxisMap(const base::Mat2x3& a)
{
    const auto unit = [](double v) { return v == 0.0 || v == 1.0 || v == -1.0; };
    if (!unit(a.m[0]) || !unit(a.m[1]) || !unit(a.m[3]) || !unit(a.m[4])) {
        return false;
    }
    const base::Point2 s = a.apply({0.5, 0.5});
    const double sx = s.x - 0.5;
    const double sy = s.y - 0.5;
    return std::abs(sx - std::round(sx)) < 1e-9 && std::abs(sy - std::round(sy)) < 1e-9;
}

image::ImageSpec withSize(image::ImageSpec s, int w, int h)
{
    s.width = w;
    s.height = h;
    return s;
}

} // namespace

CpuBackend::CpuBackend()
    : CpuBackend(Options{})
{}

CpuBackend::CpuBackend(Options o)
    : impl_(std::make_unique<Impl>())
{
    if (o.threads > 0) {
        impl_->arena.emplace(o.threads);
    }
}

CpuBackend::~CpuBackend() = default;

int CpuBackend::concurrency() const
{
    return impl_->run([] { return tbb::this_task_arena::max_concurrency(); });
}

CpuBackend::Result CpuBackend::runPixel(Ctx c, const image::PixelProgram& p, In in)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    if (s.channels != 1 && s.channels != 3) {
        return base::makeError(base::ErrorCode::Unsupported, "Punktprogramme brauchen 1 oder 3 Kanäle");
    }
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                detail::pixelRow(p, rowOf(in, y), out.row(y), static_cast<std::size_t>(s.width), s.channels);
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::multiply(Ctx c, In in, In gain)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = requireHostF32(gain); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    const image::ImageSpec& g = gain.spec();
    if (g.width != s.width || g.height != s.height || (g.channels != 1 && g.channels != s.channels)) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Verstärkungsbild passt nicht zum Bild");
    }
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                detail::multiplyRow(rowOf(in, y), rowOf(gain, y), out.row(y),
                                    static_cast<std::size_t>(s.width), s.channels, g.channels);
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::gaussian(Ctx c, In in, float sigma)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (sigma <= 0.0F) {
        return in;
    }
    if (c.cancel.cancelled()) {
        return cancelled();
    }
    // cv::GaussianBlur: gleiches Ergebnis wie die eigene Faltung (PSNR > 80 dB), doppelt so schnell
    Output out(in.spec());
    cv::Mat dst = matOf(out);
    if (auto ok = guarded([&] {
            impl_->run([&] {
                cv::GaussianBlur(matOf(in), dst, gaussianSize(sigma), sigma, sigma, cv::BORDER_REPLICATE);
            });
        });
        !ok) {
        return std::unexpected(ok.error());
    }
    return out.finish();
}

CpuBackend::Result CpuBackend::unsharp(Ctx c, In in, float a, float r, float t)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    if (r <= 0.0F) {
        return in; // Unschärfe = Bild, Differenz 0
    }
    const std::size_t len = rowLen(s);
    // Unschärfe mit cv::GaussianBlur, Schwelle und Verstärkung je Zeile (eigene SIMD-Formel)
    Output blur(s);
    cv::Mat blurMat = matOf(blur);
    if (auto ok = guarded([&] {
            impl_->run([&] { cv::GaussianBlur(matOf(in), blurMat, gaussianSize(r), r, r, cv::BORDER_REPLICATE); });
        });
        !ok) {
        return std::unexpected(ok.error());
    }
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                detail::unsharpRow(rowOf(in, y), blur.row(y), out.row(y), len, a, t);
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::localTone(Ctx c, In in, float sh, float hl, float r)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    const auto width = static_cast<std::size_t>(s.width);
    const std::size_t count = width * static_cast<std::size_t>(s.height);
    const int q = std::max(1, static_cast<int>(std::lround(r)));
    constexpr float eps = 0.01F;
    constexpr float logFloor = 1.0F / 65536.0F; // 2^-16
    // Ebenen mit einem Kanal: Luminanz, Quadrat, Mittelwerte, Koeffizienten a/b
    std::vector<float> lum(count), sq(count), meanI(count), corrI(count), a(count), b(count);
    const auto rows = [width](std::vector<float>& plane) {
        return [&plane, width](int y) { return plane.data() + static_cast<std::size_t>(y) * width; };
    };
    Output out(s);
    const bool done = impl_->run([&] {
        // Guided Filter der log2-Luminanz mit sich selbst (Definition: tests/reference)
        if (!forBlocks(s.height, c.cancel, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    float* l = rows(lum)(y);
                    detail::luminanceRow(rowOf(in, y), l, width, s.channels);
                    float* q2 = rows(sq)(y);
                    for (std::size_t x = 0; x < width; ++x) {
                        l[x] = std::log2(std::max(l[x], logFloor));
                        q2[x] = l[x] * l[x];
                    }
                }
            })) {
            return false;
        }
        if (!boxPlane(lum, meanI, s.width, s.height, q, c.cancel) ||
            !boxPlane(sq, corrI, s.width, s.height, q, c.cancel)) {
            return false;
        }
        if (!forBlocks(s.height, c.cancel, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const float* m = rows(meanI)(y);
                    const float* cr = rows(corrI)(y);
                    float* pa = rows(a)(y);
                    float* pb = rows(b)(y);
                    for (std::size_t x = 0; x < width; ++x) {
                        const float var = std::max(0.0F, cr[x] - m[x] * m[x]);
                        pa[x] = var / (var + eps);
                        pb[x] = m[x] - pa[x] * m[x];
                    }
                }
            })) {
            return false;
        }
        // meanI/corrI werden als Ziel für box(a)/box(b) wiederverwendet
        if (!boxPlane(a, meanI, s.width, s.height, q, c.cancel) ||
            !boxPlane(b, corrI, s.width, s.height, q, c.cancel)) {
            return false;
        }
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> base(width);
            for (int y = y0; y < y1; ++y) {
                const float* ma = rows(meanI)(y);
                const float* mb = rows(corrI)(y);
                const float* l = rows(lum)(y);
                for (std::size_t x = 0; x < width; ++x) {
                    base[x] = std::exp2(ma[x] * l[x] + mb[x]);
                }
                detail::toneRow(rowOf(in, y), base.data(), out.row(y), width, s.channels, sh, hl);
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::denoise(Ctx c, In in, float strength, ports::DenoiseMethod m)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (strength <= 0.0F) {
        return in;
    }
    const image::ImageSpec& s = in.spec();
    if (s.channels != 1 && s.channels != 3) {
        return base::makeError(base::ErrorCode::Unsupported, "Rauschminderung nur mit 1 oder 3 Kanälen");
    }
    if (c.cancel.cancelled()) {
        return cancelled();
    }
    Output out(s);
    if (m == ports::DenoiseMethod::Fast) {
        // cv::bilateralFilter 5 × 5, σs = 1,5, σr wie bisher (gut 2× schneller als die eigene Fassung)
        const float sigmaR = 0.02F + 0.2F * strength;
        cv::Mat dst = matOf(out);
        if (auto ok = guarded([&] {
                impl_->run([&] { cv::bilateralFilter(matOf(in), dst, 5, sigmaR, 1.5, cv::BORDER_REPLICATE); });
            });
            !ok) {
            return std::unexpected(ok.error());
        }
        return out.finish();
    }
    // Stark: cv::fastNlMeansDenoising(Colored), nur 8 Bit. Gegen Stufen: vor dem Filtern mit Gamma
    // 1/2,2 kodieren (Stufen gleichmäßig über die Helligkeit verteilt) und nur die Änderung des
    // Filters auf das Gleitkommabild übertragen. Feste Blöcke mit Rand: abbrechbar und unabhängig
    // von der Threadzahl.
    constexpr int halo = 13; // Suchfenster 21 (Radius 10) + Vergleichsfeld 7 (Radius 3)
    const float h = 2.0F + 10.0F * strength;
    const int type = s.channels == 3 ? CV_8UC3 : CV_8UC1;
    const auto encode = [](float v) {
        return static_cast<unsigned char>(std::lround(255.0F * std::pow(std::clamp(v, 0.0F, 1.0F), 1.0F / 2.2F)));
    };
    std::array<float, 256> decode{};
    for (std::size_t i = 0; i < decode.size(); ++i) {
        decode[i] = std::pow(static_cast<float>(i) / 255.0F, 2.2F);
    }
    std::atomic<bool> failed{false};
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            const int a = std::max(0, y0 - halo);
            const int b = std::min(s.height, y1 + halo);
            cv::Mat src8(b - a, s.width, type);
            for (int y = a; y < b; ++y) {
                const float* row = rowOf(in, y);
                auto* o = src8.ptr<unsigned char>(y - a);
                for (std::size_t i = 0; i < rowLen(s); ++i) {
                    o[i] = encode(row[i]);
                }
            }
            cv::Mat dst8;
            try {
                if (s.channels == 3) {
                    cv::fastNlMeansDenoisingColored(src8, dst8, h, h, 7, 21);
                } else {
                    cv::fastNlMeansDenoising(src8, dst8, h, 7, 21);
                }
            } catch (const cv::Exception&) {
                failed = true;
                return;
            }
            for (int y = y0; y < y1; ++y) {
                const float* row = rowOf(in, y);
                const auto* before = src8.ptr<unsigned char>(y - a);
                const auto* after = dst8.ptr<unsigned char>(y - a);
                float* o = out.row(y);
                for (std::size_t i = 0; i < rowLen(s); ++i) {
                    o[i] = row[i] + (decode[after[i]] - decode[before[i]]);
                }
            }
        });
    });
    if (failed) {
        return base::makeError(base::ErrorCode::Internal, "OpenCV: Non-Local Means fehlgeschlagen");
    }
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::inpaint(Ctx c, In in, const model::SpotList& spots)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    constexpr float ringWidth = 2.0F; // Pixel
    const image::ImageSpec& s = in.spec();
    const int ch = s.channels;
    Output out(s);
    const auto src = in.host();
    std::ranges::copy(src, out.storage->floats().begin());
    for (const model::Spot& spot : spots) {
        if (c.cancel.cancelled()) {
            return cancelled();
        }
        const float r = static_cast<float>(spot.radius) * static_cast<float>(s.width);
        if (r <= 0.0F) {
            continue;
        }
        const float cx = static_cast<float>(spot.center.x) * static_cast<float>(s.width);
        const float cy = static_cast<float>(spot.center.y) * static_cast<float>(s.height);
        const int dx = static_cast<int>(std::lround(spot.offset.x * s.width));
        const int dy = static_cast<int>(std::lround(spot.offset.y * s.height));
        // Bereich der Stelle samt Ring für den Helligkeitsausgleich
        const float outer = r + ringWidth;
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - outer)) - 1);
        const int x1 = std::min(s.width, static_cast<int>(std::ceil(cx + outer)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - outer)) - 1);
        const int y1 = std::min(s.height, static_cast<int>(std::ceil(cy + outer)) + 1);
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }
        // Stand vor dieser Stelle sichern: Ziel- und Quellbereich können sich überlappen
        const int bw = x1 - x0;
        const int bh = y1 - y0;
        const auto idx = [&](int x, int y, int k) {
            return (static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(bw) +
                    static_cast<std::size_t>(x - x0)) *
                       static_cast<std::size_t>(ch) +
                   static_cast<std::size_t>(k);
        };
        std::vector<float> before(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) *
                                  static_cast<std::size_t>(ch));
        std::vector<float> source(before.size());
        for (int y = y0; y < y1; ++y) {
            const int sy = std::clamp(y + dy, 0, s.height - 1);
            for (int x = x0; x < x1; ++x) {
                const int sx = std::clamp(x + dx, 0, s.width - 1);
                for (int k = 0; k < ch; ++k) {
                    before[idx(x, y, k)] = out.row(y)[x * ch + k];
                    source[idx(x, y, k)] = out.row(sy)[sx * ch + k];
                }
            }
        }
        const auto dist = [cx, cy](int x, int y) {
            return std::hypot(static_cast<float>(x) + 0.5F - cx, static_cast<float>(y) + 0.5F - cy);
        };
        // Heilen: Helligkeitsunterschied zwischen Ziel und Quelle auf dem Ring je Kanal
        // (Definition und Summationsreihenfolge wie tests/reference)
        std::array<double, 4> diff{};
        int ring = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float d = dist(x, y);
                if (d < r || d >= outer) {
                    continue;
                }
                for (int k = 0; k < ch; ++k) {
                    diff[static_cast<std::size_t>(k)] += before[idx(x, y, k)] - source[idx(x, y, k)];
                }
                ++ring;
            }
        }
        std::array<float, 4> offset{};
        for (std::size_t k = 0; k < offset.size(); ++k) {
            offset[k] = ring > 0 ? static_cast<float>(diff[k] / ring) : 0.0F;
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float d = dist(x, y);
                if (d >= r) {
                    continue;
                }
                const float f = d <= 0.7F * r ? 1.0F : (r - d) / (0.3F * r);
                for (int k = 0; k < ch; ++k) {
                    const float healed = source[idx(x, y, k)] + offset[static_cast<std::size_t>(k)];
                    out.row(y)[x * ch + k] = (1.0F - f) * before[idx(x, y, k)] + f * healed;
                }
            }
        }
    }
    return out.finish();
}

CpuBackend::Result CpuBackend::retouch(Ctx c, In in, const model::RetouchShapes& shapes,
                                       const ports::RetouchOptions& o)
{
    // Staubretusche mit Formen (K-M10-23), nur OpenCV-Standardfunktionen:
    //  1. Fläche aus den eingeschalteten Formen (cv::circle, cv::line; Radierer je Form),
    //  2. Auffälligkeiten in der Luminanz (log): Black-Hat und Top-Hat (cv::morphologyEx) mit einer
    //     Scheibe der größten Fehlerbreite finden alles, was dunkler bzw. heller und schmaler ist
    //     (Staub, Fussel, Kratzer); Schwelle aus der Empfindlichkeit,
    //  3. Fehler (leicht vergrößert, cv::dilate) innerhalb der Fläche je Kanal füllen
    //     (cv::inpaint, Telea; Gleitkomma geht nur einkanalig).
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    if (s.channels != 1 && s.channels != 3) {
        return base::makeError(base::ErrorCode::Unsupported, "Retusche nur mit 1 oder 3 Kanälen");
    }
    if (c.cancel.cancelled()) {
        return cancelled();
    }
    const double w = s.width;
    const double h = s.height;
    const auto px = [&](const model::NormPoint& p) {
        return cv::Point(static_cast<int>(std::lround(p.x * w)), static_cast<int>(std::lround(p.y * h)));
    };
    cv::Mat area = cv::Mat::zeros(s.height, s.width, CV_8UC1);
    for (const model::RetouchShape& shape : shapes) {
        if (!shape.enabled) {
            continue;
        }
        if (shape.kind == model::RetouchShape::Kind::Circle) {
            cv::circle(area, px(shape.center), std::max(1, static_cast<int>(std::lround(shape.radius * w))),
                       cv::Scalar(255), cv::FILLED);
            continue;
        }
        // Fläche: eigene Maske, damit der Radierer nur in dieser Form wirkt
        cv::Mat own = cv::Mat::zeros(s.height, s.width, CV_8UC1);
        for (const model::RetouchStroke& st : shape.strokes) {
            const int thick = std::max(1, static_cast<int>(std::lround(st.width * w)));
            const cv::Scalar value(st.erase ? 0 : 255);
            if (st.points.size() == 1) {
                cv::circle(own, px(st.points.front()), std::max(1, thick / 2), value, cv::FILLED);
            }
            for (std::size_t i = 1; i < st.points.size(); ++i) {
                cv::line(own, px(st.points[i - 1]), px(st.points[i]), value, thick); // runde Enden
            }
        }
        // nur das zusammenhängende Stück um den inneren Punkt (vom Radierer geteilte Fläche)
        const cv::Point seed = px(shape.center);
        if (cv::Rect(0, 0, s.width, s.height).contains(seed) && own.at<unsigned char>(seed) == 255) {
            cv::floodFill(own, seed, cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
            own = own == 128;
        }
        cv::bitwise_or(area, own, area);
    }
    const cv::Rect used = cv::boundingRect(area);
    if (used.empty()) {
        return in;
    }
    const int defect = std::max(3, static_cast<int>(std::lround(o.defectPx)) | 1);
    const int margin = defect + 8;
    const cv::Rect box = (used + cv::Size(2 * margin, 2 * margin) - cv::Point(margin, margin)) &
                         cv::Rect(0, 0, s.width, s.height);
    Output out(s);
    std::ranges::copy(in.host(), out.storage->floats().begin());
    cv::Mat whole = matOf(out);
    if (auto ok = guarded([&] {
            impl_->run([&] {
                cv::Mat roi = matOf(in)(box);
                cv::Mat lum;
                if (s.channels == 3) {
                    cv::transform(roi, lum, cv::Matx13f(1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F));
                } else {
                    lum = roi.clone();
                }
                cv::log(cv::max(lum, 1e-4), lum);
                const cv::Mat disk = cv::getStructuringElement(cv::MORPH_ELLIPSE, {defect, defect});
                cv::Mat dark;
                cv::Mat bright;
                cv::morphologyEx(lum, dark, cv::MORPH_BLACKHAT, disk, {-1, -1}, 1, cv::BORDER_REPLICATE);
                cv::morphologyEx(lum, bright, cv::MORPH_TOPHAT, disk, {-1, -1}, 1, cv::BORDER_REPLICATE);
                // Schwelle in ln-Einheiten: Empfindlichkeit 0,5 ≈ 30 % Kontrast, 1 ≈ 2 %
                const double t = 0.02 + (1.0 - std::clamp(static_cast<double>(o.sensitivity), 0.0, 1.0)) * 0.5;
                cv::Mat defects = (dark > t) | (bright > t);
                cv::dilate(defects, defects, cv::getStructuringElement(cv::MORPH_ELLIPSE, {5, 5}));
                cv::bitwise_and(defects, area(box), defects);
                if (cv::countNonZero(defects) == 0) {
                    return;
                }
                // cv::inpaint liefert mit 32-Bit-Gleitkomma falsche Werte (gemessen, OpenCV 4.14);
                // deshalb je Kanal in 16 Bit füllen. Ersetzt werden nur die Fehlerpixel.
                double hi = 0.0;
                cv::minMaxLoc(roi.reshape(1), nullptr, &hi);
                const double scale = 65535.0 / std::max(hi, 1e-6);
                std::vector<cv::Mat> planes;
                cv::split(roi, planes);
                for (cv::Mat& p : planes) {
                    cv::Mat p16;
                    p.convertTo(p16, CV_16U, scale);
                    cv::Mat filled;
                    cv::inpaint(p16, defects, filled, 3.0, cv::INPAINT_TELEA);
                    filled.convertTo(p, CV_32F, 1.0 / scale);
                }
                cv::Mat merged;
                cv::merge(planes, merged);
                merged.copyTo(whole(box), defects);
            });
        });
        !ok) {
        return std::unexpected(ok.error());
    }
    return c.cancel.cancelled() ? cancelled() : out.finish();
}

CpuBackend::Result CpuBackend::warpOpenCv(Ctx c, In in, const image::WarpPlan& plan)
{
    // cv::warpAffine (Lanczos-4 statt -3, etwa 7× schneller; mittlere Abweichung 0,04 %).
    // Eigene Abbildung: Quelle = A·(Ziel + 0,5) − 0,5; OpenCV rechnet mit Pixelmitten auf ganzen
    // Zahlen und WARP_INVERSE_MAP (Ziel → Quelle).
    const image::ImageSpec& s = in.spec();
    const auto& a = plan.affine.m;
    const cv::Matx23d m(a[0], a[1], 0.5 * (a[0] + a[1]) + a[2] - 0.5, a[3], a[4], 0.5 * (a[3] + a[4]) + a[5] - 0.5);
    const int interp = plan.filter == image::Filter::Bilinear  ? cv::INTER_LINEAR
                       : plan.filter == image::Filter::Bicubic ? cv::INTER_CUBIC
                                                               : cv::INTER_LANCZOS4;
    if (c.cancel.cancelled()) {
        return cancelled();
    }
    Output out(image::ImageSpec{plan.out.width, plan.out.height, s.channels, s.format, s.space});
    cv::Mat dst = matOf(out);
    if (auto ok = guarded([&] {
            impl_->run([&] {
                // Rand wiederholen wie bisher; Ziele außerhalb der Quelle danach schwarz
                cv::warpAffine(matOf(in), dst, m, dst.size(), interp | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
            });
        });
        !ok) {
        return std::unexpected(ok.error());
    }
    const bool done = impl_->run([&] {
        return forBlocks(plan.out.height, c.cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                float* o = out.row(y);
                for (int x = 0; x < plan.out.width; ++x) {
                    const double sx = m(0, 0) * x + m(0, 1) * y + m(0, 2);
                    const double sy = m(1, 0) * x + m(1, 1) * y + m(1, 2);
                    if (sx < -0.5 || sy < -0.5 || sx > s.width - 0.5 || sy > s.height - 0.5) {
                        std::fill_n(o + static_cast<std::ptrdiff_t>(x) * s.channels, s.channels, 0.0F);
                    }
                }
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::warp(Ctx c, In in, const image::WarpPlan& plan)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (plan.out.width <= 0 || plan.out.height <= 0) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Leere Ausgabegröße der Verzerrung");
    }
    const image::ImageSpec& s = in.spec();
    const int ch = s.channels;
    const bool exact = isExactAxisMap(plan.affine);
    if (!exact && plan.filter != image::Filter::Nearest && ch <= 4) {
        return warpOpenCv(c, in, plan);
    }
    const int sup = filterSupport(plan.filter);
    const auto at = [&](int x, int y, int k) {
        return rowOf(in, std::clamp(y, 0, s.height - 1))[std::clamp(x, 0, s.width - 1) * ch + k];
    };
    Output out(withSize(s, plan.out.width, plan.out.height));
    const bool done = impl_->run([&] {
        return forBlocks(plan.out.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> wx(static_cast<std::size_t>(2 * sup));
            std::vector<float> wy(static_cast<std::size_t>(2 * sup));
            std::vector<const float*> rows(static_cast<std::size_t>(2 * sup));
            std::vector<int> cols(static_cast<std::size_t>(2 * sup));
            std::vector<float> acc(static_cast<std::size_t>(ch));
            for (int y = y0; y < y1; ++y) {
                float* o = out.row(y);
                for (int x = 0; x < plan.out.width; ++x) {
                    const base::Point2 p = plan.affine.apply({x + 0.5, y + 0.5});
                    const double sx = p.x - 0.5;
                    const double sy = p.y - 0.5;
                    if (sx < -0.5 || sy < -0.5 || sx > s.width - 0.5 || sy > s.height - 0.5) {
                        for (int k = 0; k < ch; ++k) {
                            o[x * ch + k] = 0.0F;
                        }
                        continue;
                    }
                    if (exact || plan.filter == image::Filter::Nearest) {
                        const int ix = static_cast<int>(std::lround(sx));
                        const int iy = static_cast<int>(std::lround(sy));
                        for (int k = 0; k < ch; ++k) {
                            o[x * ch + k] = at(ix, iy, k);
                        }
                        continue;
                    }
                    const int ix = static_cast<int>(std::floor(sx));
                    const int iy = static_cast<int>(std::floor(sy));
                    for (int j = 0; j < 2 * sup; ++j) {
                        wy[static_cast<std::size_t>(j)] =
                            filterKernel(plan.filter, static_cast<float>(sy - (iy - sup + 1 + j)));
                        wx[static_cast<std::size_t>(j)] =
                            filterKernel(plan.filter, static_cast<float>(sx - (ix - sup + 1 + j)));
                    }
                    // Zeilen und Spalten einmal klemmen; je Kanal dieselbe Summationsreihenfolge
                    for (int j = 0; j < 2 * sup; ++j) {
                        rows[static_cast<std::size_t>(j)] =
                            rowOf(in, std::clamp(iy - sup + 1 + j, 0, s.height - 1));
                        cols[static_cast<std::size_t>(j)] = std::clamp(ix - sup + 1 + j, 0, s.width - 1) * ch;
                    }
                    std::ranges::fill(acc, 0.0F);
                    float wsum = 0.0F;
                    for (int j = 0; j < 2 * sup; ++j) {
                        const float* row = rows[static_cast<std::size_t>(j)];
                        for (int i = 0; i < 2 * sup; ++i) {
                            const float wgt =
                                wy[static_cast<std::size_t>(j)] * wx[static_cast<std::size_t>(i)];
                            const float* px = row + cols[static_cast<std::size_t>(i)];
                            for (int k = 0; k < ch; ++k) {
                                acc[static_cast<std::size_t>(k)] += wgt * px[k];
                            }
                            wsum += wgt;
                        }
                    }
                    for (int k = 0; k < ch; ++k) {
                        o[x * ch + k] = wsum != 0.0F ? acc[static_cast<std::size_t>(k)] / wsum : 0.0F;
                    }
                }
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::resample(Ctx c, In in, base::Size size)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (size.width <= 0 || size.height <= 0) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Leere Zielgröße");
    }
    const image::ImageSpec& s = in.spec();
    const int ch = s.channels;
    if (size.width <= s.width && size.height <= s.height && ch <= 4) {
        // Verkleinern mit cv::resize INTER_AREA (Flächenmittel): 3–5× schneller als Lanczos-3,
        // etwas weicher (Entscheidung des Nutzers: schneller vor schärfer)
        if (c.cancel.cancelled()) {
            return cancelled();
        }
        Output small(withSize(s, size.width, size.height));
        cv::Mat dst = matOf(small);
        if (auto ok = guarded([&] {
                impl_->run([&] { cv::resize(matOf(in), dst, dst.size(), 0, 0, cv::INTER_AREA); });
            });
            !ok) {
            return std::unexpected(ok.error());
        }
        return small.finish();
    }

    struct Taps
    {
        int first = 0;
        std::vector<float> w; ///< normiert
    };
    const auto plan = [](int inLen, int outLen) {
        std::vector<Taps> taps(static_cast<std::size_t>(outLen));
        const float f = static_cast<float>(inLen) / static_cast<float>(outLen);
        const float stretch = std::max(1.0F, f);
        const float sup = 3.0F * stretch;
        for (int o = 0; o < outLen; ++o) {
            const float center = (static_cast<float>(o) + 0.5F) * f - 0.5F;
            Taps& t = taps[static_cast<std::size_t>(o)];
            t.first = static_cast<int>(std::floor(center - sup)) + 1;
            const int last = static_cast<int>(std::floor(center + sup));
            float wsum = 0.0F;
            for (int i = t.first; i <= last; ++i) {
                t.w.push_back(lanczos3((static_cast<float>(i) - center) / stretch));
                wsum += t.w.back();
            }
            for (float& w : t.w) {
                w /= wsum;
            }
        }
        return taps;
    };
    const auto hTaps = plan(s.width, size.width);
    const auto vTaps = plan(s.height, size.height);

    // horizontal: Zwischenbild size.width x s.height
    std::vector<float> mid(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(s.height) *
                           static_cast<std::size_t>(ch));
    const auto midRow = [&](int y) {
        return mid.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) *
                                static_cast<std::size_t>(ch);
    };
    Output out(withSize(s, size.width, size.height));
    const bool done = impl_->run([&] {
        if (!forBlocks(s.height, c.cancel, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const float* src = rowOf(in, y);
                    float* dst = midRow(y);
                    for (int o = 0; o < size.width; ++o) {
                        const Taps& t = hTaps[static_cast<std::size_t>(o)];
                        for (int k = 0; k < ch; ++k) {
                            float acc = 0.0F;
                            for (std::size_t i = 0; i < t.w.size(); ++i) {
                                const int xx = std::clamp(t.first + static_cast<int>(i), 0, s.width - 1);
                                acc += t.w[i] * src[xx * ch + k];
                            }
                            dst[o * ch + k] = acc;
                        }
                    }
                }
            })) {
            return false;
        }
        return forBlocks(size.height, c.cancel, [&](int y0, int y1) {
            const std::size_t len = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(ch);
            for (int y = y0; y < y1; ++y) {
                const Taps& t = vTaps[static_cast<std::size_t>(y)];
                float* dst = out.row(y);
                std::fill(dst, dst + len, 0.0F);
                for (std::size_t i = 0; i < t.w.size(); ++i) {
                    const float* src = midRow(std::clamp(t.first + static_cast<int>(i), 0, s.height - 1));
                    for (std::size_t j = 0; j < len; ++j) {
                        dst[j] += t.w[i] * src[j];
                    }
                }
            }
        });
    });
    return done ? out.finish() : cancelled();
}

base::Expected<image::ImageBuffer> CpuBackend::colorTransform(Ctx c, In in, const ports::IColorTransform& xf)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    const image::ImageSpec& s = in.spec();
    // Matrix-Kurven-Profile in geschlossener Form mit SIMD statt über die Bibliothek (D-18)
    if (const auto ms = xf.matrixShaper(); ms && (s.channels == 3 || s.channels == 1)) {
        image::ImageSpec os = s;
        os.channels = ms->outChannels;
        os.space = image::ColorSpace::Output;
        Output out(os);
        const bool done = impl_->run([&] {
            return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    detail::matrixShaperRow(*ms, rowOf(in, y), out.row(y), static_cast<std::size_t>(s.width),
                                            s.channels);
                }
            });
        });
        return done ? out.finish() : cancelled();
    }
    if (s.height <= blockRows) {
        return xf.apply(in);
    }
    // Erster Block allein: liefert Kanalzahl und Farbraum der Ausgabe
    const auto first = in.copyRegion({0, 0, s.width, blockRows});
    if (!first) {
        return std::unexpected(first.error());
    }
    auto head = xf.apply(*first);
    if (!head) {
        return head;
    }
    image::ImageSpec os = s;
    os.channels = head->spec().channels;
    os.space = head->spec().space;
    Output out(os);
    const std::size_t rowSamples = os.samplesPerRow();
    std::copy_n(head->host().data(), rowSamples * static_cast<std::size_t>(blockRows), out.row(0));
    std::mutex errorMutex;
    std::optional<base::Error> error;
    const bool done = impl_->run([&] {
        return forBlocks(s.height - blockRows, c.cancel, [&](int y0, int y1) {
            const int top = y0 + blockRows;
            const auto part = in.copyRegion({0, top, s.width, y1 - y0});
            auto res =
                part ? xf.apply(*part) : base::Expected<image::ImageBuffer>(std::unexpected(part.error()));
            if (!res || res->spec().channels != os.channels) {
                const std::scoped_lock lock(errorMutex);
                if (!error) {
                    error = res ? base::Error{base::ErrorCode::Internal,
                                              "Farbtransformation: Kanalzahl uneinheitlich"}
                                : res.error();
                }
                return;
            }
            std::copy_n(res->host().data(), rowSamples * static_cast<std::size_t>(y1 - y0), out.row(top));
        });
    });
    if (error) {
        return std::unexpected(*error);
    }
    if (!done) {
        return cancelled();
    }
    return out.finish();
}

base::Expected<image::ImageBuffer> CpuBackend::quantize(Ctx c, In in, int bits)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (bits != 8 && bits != 16) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Quantisierung nur auf 8 oder 16 Bit");
    }
    image::ImageSpec os = in.spec();
    os.format = bits == 8 ? image::PixelFormat::U8 : image::PixelFormat::U16;
    auto storage = std::make_shared<image::HostStorage>(os.sizeBytes());
    const std::size_t len = in.spec().samplesPerRow();
    const std::size_t rowBytes = len * (bits == 8 ? 1U : 2U);
    std::byte* base = storage->bytes().data();
    const bool done = impl_->run([&] {
        return forBlocks(os.height, c.cancel, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                detail::quantizeRow(rowOf(in, y), base + static_cast<std::size_t>(y) * rowBytes, len, bits);
            }
        });
    });
    return done ? image::ImageBuffer::wrap(os, std::move(storage)) : cancelled();
}

base::Expected<image::Histogram> CpuBackend::histogram(Ctx c, In in, int bins)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (bins <= 0) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Anzahl der Klassen muss positiv sein");
    }
    const image::ImageSpec& s = in.spec();
    const int blocks = (s.height + blockRows - 1) / blockRows;
    const auto nb = static_cast<std::size_t>(bins);
    // feste Blockeinteilung: Teilergebnisse je Block, danach in Blockreihenfolge summiert
    struct Partial
    {
        std::vector<std::uint32_t> counts; ///< 4 * bins
        std::array<std::uint64_t, 3> low{};
        std::array<std::uint64_t, 3> high{};
    };
    std::vector<Partial> partials(static_cast<std::size_t>(blocks));
    const auto binOf = [bins](float v) {
        return static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::floor(v * static_cast<float>(bins))), 0, bins - 1));
    };
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            Partial& part = partials[static_cast<std::size_t>(y0 / blockRows)];
            part.counts.assign(4 * nb, 0U);
            for (int y = y0; y < y1; ++y) {
                const float* row = rowOf(in, y);
                for (int x = 0; x < s.width; ++x) {
                    std::array<float, 3> v{};
                    for (std::size_t k = 0; k < 3; ++k) {
                        v[k] = row[x * s.channels + (s.channels == 1 ? 0 : static_cast<int>(k))];
                    }
                    const float l = s.channels == 1 ? v[0] : 0.2627F * v[0] + 0.6780F * v[1] + 0.0593F * v[2];
                    for (std::size_t k = 0; k < 3; ++k) {
                        ++part.counts[k * nb + binOf(v[k])];
                        part.low[k] += v[k] <= 0.0F ? 1U : 0U;
                        part.high[k] += v[k] >= 1.0F ? 1U : 0U;
                    }
                    ++part.counts[3 * nb + binOf(l)];
                }
            }
        });
    });
    if (!done) {
        return base::makeError(base::ErrorCode::Cancelled, "Berechnung abgebrochen");
    }
    image::Histogram h;
    for (auto& b : h.bins) {
        b.assign(nb, 0U);
    }
    std::array<std::uint64_t, 3> low{};
    std::array<std::uint64_t, 3> high{};
    for (const Partial& part : partials) {
        for (std::size_t k = 0; k < 4; ++k) {
            for (std::size_t i = 0; i < nb; ++i) {
                h.bins[k][i] += part.counts[k * nb + i];
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            low[k] += part.low[k];
            high[k] += part.high[k];
        }
    }
    const double n = static_cast<double>(s.pixelCount());
    h.clipLow = {static_cast<double>(low[0]) / n, static_cast<double>(low[1]) / n,
                 static_cast<double>(low[2]) / n};
    h.clipHigh = {static_cast<double>(high[0]) / n, static_cast<double>(high[1]) / n,
                  static_cast<double>(high[2]) / n};
    return h;
}

std::vector<SimdTarget> CpuBackend::targets()
{
    std::vector<SimdTarget> out;
    std::int64_t bits = detail::supportedTargets() & detail::compiledTargets();
    while (bits != 0) {
        const std::int64_t bit = bits & -bits;
        out.push_back(SimdTarget{bit, detail::targetName(bit)});
        bits &= ~bit;
    }
    return out;
}

SimdTarget CpuBackend::activeTarget()
{
    const std::int64_t bits = detail::supportedTargets() & detail::compiledTargets();
    const std::int64_t best = bits & -bits; // Highway: kleinere Bits sind leistungsfähigere Ziele
    return SimdTarget{best, detail::targetName(best)};
}

void CpuBackend::forceTargetForTest(const SimdTarget& t)
{
    detail::forceTargets(t.bits);
}

void CpuBackend::resetTargetForTest()
{
    detail::resetTargets();
}

void CpuBackend::parallelFor(int n, const std::function<void(int)>& fn)
{
    impl_->run([&] {
        tbb::parallel_for(tbb::blocked_range<int>(0, n, 1), [&](const tbb::blocked_range<int>& r) {
            for (int i = r.begin(); i < r.end(); ++i) {
                fn(i);
            }
        });
    });
}

} // namespace autoneg::backend::cpu
