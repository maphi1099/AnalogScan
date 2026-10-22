#include "autoneg/backend/cpu/cpu_backend.hpp"

#include "kernels.hpp"

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

std::vector<float> gaussianWeights(float sigma)
{
    const int r = std::max(1, static_cast<int>(std::ceil(3.0F * sigma)));
    std::vector<float> w(static_cast<std::size_t>(2 * r + 1));
    float sum = 0.0F;
    for (int k = -r; k <= r; ++k) {
        const float v = std::exp(-0.5F * static_cast<float>(k * k) / (sigma * sigma));
        w[static_cast<std::size_t>(k + r)] = v;
        sum += v;
    }
    for (float& v : w) {
        v /= sum;
    }
    return w;
}

/// Gauß-Unschärfe der Zeilen [y0, y1) eines Bildes (Zeilenzugriff über src) in dst;
/// horizontal für den Block plus Rand in einen lokalen Puffer, dann vertikal.
template <class SrcRow, class DstRow>
void blurBlock(SrcRow src, DstRow dst, int width, int height, int channels, std::span<const float> w, int y0,
               int y1)
{
    const int r = static_cast<int>(w.size() / 2);
    const int t0 = std::max(0, y0 - r);
    const int t1 = std::min(height, y1 + r);
    const std::size_t len = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<float> tmp(static_cast<std::size_t>(t1 - t0) * len);
    for (int y = t0; y < t1; ++y) {
        detail::convolveRowH(src(y), tmp.data() + static_cast<std::size_t>(y - t0) * len, width, channels, w);
    }
    std::vector<const float*> rows(w.size());
    for (int y = y0; y < y1; ++y) {
        for (int k = -r; k <= r; ++k) {
            const int yy = std::clamp(y + k, 0, height - 1);
            rows[static_cast<std::size_t>(k + r)] = tmp.data() + static_cast<std::size_t>(yy - t0) * len;
        }
        detail::convolveRowV(rows.data(), dst(y), len, w);
    }
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
    const image::ImageSpec& s = in.spec();
    const auto w = gaussianWeights(sigma);
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            blurBlock([&](int y) { return rowOf(in, y); }, [&](int y) { return out.row(y); }, s.width,
                      s.height, s.channels, w, y0, y1);
        });
    });
    return done ? out.finish() : cancelled();
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
    const auto w = gaussianWeights(r);
    const std::size_t len = rowLen(s);
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> blur(static_cast<std::size_t>(y1 - y0) * len);
            blurBlock([&](int y) { return rowOf(in, y); },
                      [&](int y) { return blur.data() + static_cast<std::size_t>(y - y0) * len; }, s.width,
                      s.height, s.channels, w, y0, y1);
            for (int y = y0; y < y1; ++y) {
                detail::unsharpRow(rowOf(in, y), blur.data() + static_cast<std::size_t>(y - y0) * len,
                                   out.row(y), len, a, t);
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
    std::vector<float> lum(width * static_cast<std::size_t>(s.height));
    const auto lumRow = [&](int y) { return lum.data() + static_cast<std::size_t>(y) * width; };
    const auto w = gaussianWeights(r);
    Output out(s);
    const bool done = impl_->run([&] {
        // 1. Luminanz des ganzen Bildes (die Unschärfe braucht Nachbarzeilen anderer Blöcke)
        if (!forBlocks(s.height, c.cancel, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    detail::luminanceRow(rowOf(in, y), lumRow(y), width, s.channels);
                }
            })) {
            return false;
        }
        // 2. Unschärfe der Luminanz und Anwendung blockweise
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> base(static_cast<std::size_t>(y1 - y0) * width);
            const auto baseRow = [&](int y) {
                return base.data() + static_cast<std::size_t>(y - y0) * width;
            };
            if (r > 0.0F) {
                blurBlock([&](int y) { return static_cast<const float*>(lumRow(y)); }, baseRow, s.width,
                          s.height, 1, w, y0, y1);
            } else {
                for (int y = y0; y < y1; ++y) {
                    std::copy(lumRow(y), lumRow(y) + width, baseRow(y));
                }
            }
            for (int y = y0; y < y1; ++y) {
                detail::toneRow(rowOf(in, y), baseRow(y), out.row(y), width, s.channels, sh, hl);
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::denoise(Ctx c, In in, float strength)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
    if (strength <= 0.0F) {
        return in;
    }
    const image::ImageSpec& s = in.spec();
    constexpr float sigmaS = 1.5F;
    const float sigmaR = 0.02F + 0.2F * strength;
    const int ch = s.channels;
    const auto at = [&](int x, int y, int k) {
        return rowOf(in, std::clamp(y, 0, s.height - 1))[std::clamp(x, 0, s.width - 1) * ch + k];
    };
    Output out(s);
    const bool done = impl_->run([&] {
        return forBlocks(s.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> acc(static_cast<std::size_t>(ch));
            for (int y = y0; y < y1; ++y) {
                float* o = out.row(y);
                for (int x = 0; x < s.width; ++x) {
                    std::ranges::fill(acc, 0.0F);
                    float wsum = 0.0F;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            float dist2 = 0.0F;
                            for (int k = 0; k < ch; ++k) {
                                const float dd = at(x + dx, y + dy, k) - at(x, y, k);
                                dist2 += dd * dd;
                            }
                            const float wgt =
                                std::exp(-static_cast<float>(dx * dx + dy * dy) / (2.0F * sigmaS * sigmaS)) *
                                std::exp(-dist2 / (2.0F * sigmaR * sigmaR));
                            for (int k = 0; k < ch; ++k) {
                                acc[static_cast<std::size_t>(k)] += wgt * at(x + dx, y + dy, k);
                            }
                            wsum += wgt;
                        }
                    }
                    for (int k = 0; k < ch; ++k) {
                        o[x * ch + k] = acc[static_cast<std::size_t>(k)] / wsum;
                    }
                }
            }
        });
    });
    return done ? out.finish() : cancelled();
}

CpuBackend::Result CpuBackend::inpaint(Ctx c, In in, const model::SpotList& spots)
{
    if (auto ok = requireHostF32(in); !ok) {
        return std::unexpected(ok.error());
    }
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
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - r)) - 1);
        const int x1 = std::min(s.width, static_cast<int>(std::ceil(cx + r)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - r)) - 1);
        const int y1 = std::min(s.height, static_cast<int>(std::ceil(cy + r)) + 1);
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
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float d =
                    std::hypot(static_cast<float>(x) + 0.5F - cx, static_cast<float>(y) + 0.5F - cy);
                if (d >= r) {
                    continue;
                }
                const float f = d <= 0.7F * r ? 1.0F : (r - d) / (0.3F * r);
                for (int k = 0; k < ch; ++k) {
                    out.row(y)[x * ch + k] = (1.0F - f) * before[idx(x, y, k)] + f * source[idx(x, y, k)];
                }
            }
        }
    }
    return out.finish();
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
    const int sup = filterSupport(plan.filter);
    const auto at = [&](int x, int y, int k) {
        return rowOf(in, std::clamp(y, 0, s.height - 1))[std::clamp(x, 0, s.width - 1) * ch + k];
    };
    Output out(withSize(s, plan.out.width, plan.out.height));
    const bool done = impl_->run([&] {
        return forBlocks(plan.out.height, c.cancel, [&](int y0, int y1) {
            std::vector<float> wx(static_cast<std::size_t>(2 * sup));
            std::vector<float> wy(static_cast<std::size_t>(2 * sup));
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
                    for (int k = 0; k < ch; ++k) {
                        float acc = 0.0F;
                        float wsum = 0.0F;
                        for (int j = 0; j < 2 * sup; ++j) {
                            for (int i = 0; i < 2 * sup; ++i) {
                                const float wgt =
                                    wy[static_cast<std::size_t>(j)] * wx[static_cast<std::size_t>(i)];
                                acc += wgt * at(ix - sup + 1 + i, iy - sup + 1 + j, k);
                                wsum += wgt;
                            }
                        }
                        o[x * ch + k] = wsum != 0.0F ? acc / wsum : 0.0F;
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

} // namespace autoneg::backend::cpu
