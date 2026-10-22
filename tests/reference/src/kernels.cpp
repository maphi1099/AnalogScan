#include "autoneg/reference/kernels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <variant>

namespace autoneg::reference {

namespace {

/// Einfacher veränderlicher Bildspeicher für die Referenz.
struct Img
{
    int w = 0;
    int h = 0;
    int c = 0;
    std::vector<float> v;

    Img(int width, int height, int channels)
        : w(width)
        , h(height)
        , c(channels)
        , v(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                static_cast<std::size_t>(channels),
            0.0F)
    {}
    explicit Img(const image::ImageBuffer& b)
        : Img(b.spec().width, b.spec().height, b.spec().channels)
    {
        const auto src = b.host();
        std::copy(src.begin(), src.end(), v.begin());
    }

    [[nodiscard]] std::size_t index(int x, int y, int ch) const
    {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) *
                   static_cast<std::size_t>(c) +
               static_cast<std::size_t>(ch);
    }
    float& at(int x, int y, int ch) { return v[index(x, y, ch)]; }
    [[nodiscard]] float at(int x, int y, int ch) const { return v[index(x, y, ch)]; }
    [[nodiscard]] float clamped(int x, int y, int ch) const
    {
        return at(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1), ch);
    }

    [[nodiscard]] image::ImageBuffer buffer(image::ColorSpace space) const
    {
        return image::ImageBuffer::fromFloats(image::ImageSpec{w, h, c, image::PixelFormat::F32, space}, v)
            .value();
    }
};

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

float kernel(image::Filter f, float x)
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

int support(image::Filter f)
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

Img gaussianImg(const Img& in, float sigma)
{
    if (sigma <= 0.0F) {
        return in;
    }
    const auto w = gaussianWeights(sigma);
    const int r = static_cast<int>(w.size() / 2);
    Img tmp(in.w, in.h, in.c);
    for (int y = 0; y < in.h; ++y) {
        for (int x = 0; x < in.w; ++x) {
            for (int ch = 0; ch < in.c; ++ch) {
                float acc = 0.0F;
                for (int k = -r; k <= r; ++k) {
                    acc += w[static_cast<std::size_t>(k + r)] * in.clamped(x + k, y, ch);
                }
                tmp.at(x, y, ch) = acc;
            }
        }
    }
    Img out(in.w, in.h, in.c);
    for (int y = 0; y < in.h; ++y) {
        for (int x = 0; x < in.w; ++x) {
            for (int ch = 0; ch < in.c; ++ch) {
                float acc = 0.0F;
                for (int k = -r; k <= r; ++k) {
                    acc += w[static_cast<std::size_t>(k + r)] * tmp.clamped(x, y + k, ch);
                }
                out.at(x, y, ch) = acc;
            }
        }
    }
    return out;
}

} // namespace

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

std::array<float, 3> applyProgram(const image::PixelProgram& p, std::array<float, 3> v, int channels)
{
    const int n = channels == 1 ? 1 : 3;
    for (const image::PixelOp& op : p.ops) {
        if (const auto* m = std::get_if<image::Matrix3>(&op)) {
            const auto f = [&](int r, int c) { return static_cast<float>(m->m(r, c)); };
            if (n == 1) {
                v[0] = f(0, 0) * v[0];
            } else {
                const std::array<float, 3> in = v;
                for (int r = 0; r < 3; ++r) {
                    v[static_cast<std::size_t>(r)] = f(r, 0) * in[0] + f(r, 1) * in[1] + f(r, 2) * in[2];
                }
            }
        } else if (const auto* o = std::get_if<image::Offset3>(&op)) {
            const std::array<float, 3> add{static_cast<float>(o->v.x), static_cast<float>(o->v.y),
                                           static_cast<float>(o->v.z)};
            for (int c = 0; c < n; ++c) {
                v[static_cast<std::size_t>(c)] += add[static_cast<std::size_t>(c)];
            }
        } else if (const auto* s = std::get_if<image::Scale3>(&op)) {
            const std::array<float, 3> mul{static_cast<float>(s->v.x), static_cast<float>(s->v.y),
                                           static_cast<float>(s->v.z)};
            for (int c = 0; c < n; ++c) {
                v[static_cast<std::size_t>(c)] *= mul[static_cast<std::size_t>(c)];
            }
        } else if (std::holds_alternative<image::Log10>(op)) {
            for (int c = 0; c < n; ++c) {
                v[static_cast<std::size_t>(c)] = std::log10(std::max(v[static_cast<std::size_t>(c)], 1e-6F));
            }
        } else if (std::holds_alternative<image::Exp10>(op)) {
            for (int c = 0; c < n; ++c) {
                v[static_cast<std::size_t>(c)] = std::pow(10.0F, v[static_cast<std::size_t>(c)]);
            }
        } else if (const auto* l = std::get_if<image::LutRef>(&op)) {
            const auto& lut = **l;
            const int count = static_cast<int>(lut.values.size());
            for (int c = 0; c < n; ++c) {
                if (lut.channel >= 0 && lut.channel != c) {
                    continue;
                }
                float& x = v[static_cast<std::size_t>(c)];
                if (count == 1) {
                    x = lut.values[0];
                    continue;
                }
                const float t = (std::clamp(x, lut.inMin, lut.inMax) - lut.inMin) / (lut.inMax - lut.inMin) *
                                static_cast<float>(count - 1);
                const int i = std::min(static_cast<int>(t), count - 2);
                const float f = t - static_cast<float>(i);
                x = lut.values[static_cast<std::size_t>(i)] +
                    f * (lut.values[static_cast<std::size_t>(i + 1)] -
                         lut.values[static_cast<std::size_t>(i)]);
            }
        } else if (const auto* cl = std::get_if<image::Clamp>(&op)) {
            for (int c = 0; c < n; ++c) {
                v[static_cast<std::size_t>(c)] = std::clamp(v[static_cast<std::size_t>(c)], cl->lo, cl->hi);
            }
        } else if (const auto* sat = std::get_if<image::Saturate>(&op)) {
            if (n == 3) {
                const float lum = 0.2627F * v[0] + 0.6780F * v[1] + 0.0593F * v[2];
                const std::array<float, 3> chroma{v[0] - lum, v[1] - lum, v[2] - lum};
                const float pixelSat = std::min(
                    1.0F, std::max(std::abs(chroma[0]), std::max(std::abs(chroma[1]), std::abs(chroma[2]))) /
                              std::max(lum, 1e-4F));
                const float gain = (1.0F + sat->saturation) * (1.0F + sat->vibrance * (1.0F - pixelSat));
                for (std::size_t k = 0; k < 3; ++k) {
                    v[k] = lum + gain * chroma[k];
                }
            }
        }
    }
    return v;
}

image::ImageBuffer runPixel(const image::PixelProgram& p, const image::ImageBuffer& in)
{
    Img img(in);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            std::array<float, 3> v{};
            for (int c = 0; c < img.c && c < 3; ++c) {
                v[static_cast<std::size_t>(c)] = img.at(x, y, c);
            }
            v = applyProgram(p, v, img.c);
            for (int c = 0; c < img.c && c < 3; ++c) {
                img.at(x, y, c) = v[static_cast<std::size_t>(c)];
            }
        }
    }
    return img.buffer(in.spec().space);
}

image::ImageBuffer warp(const image::ImageBuffer& in, const image::WarpPlan& plan)
{
    const Img src(in);
    Img out(plan.out.width, plan.out.height, src.c);
    const bool exact = isExactAxisMap(plan.affine);
    const int s = support(plan.filter);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            const base::Point2 p = plan.affine.apply({x + 0.5, y + 0.5});
            const double sx = p.x - 0.5;
            const double sy = p.y - 0.5;
            if (sx < -0.5 || sy < -0.5 || sx > src.w - 0.5 || sy > src.h - 0.5) {
                continue; // außerhalb: 0
            }
            for (int c = 0; c < src.c; ++c) {
                if (exact || plan.filter == image::Filter::Nearest) {
                    out.at(x, y, c) =
                        src.clamped(static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)), c);
                    continue;
                }
                const int ix = static_cast<int>(std::floor(sx));
                const int iy = static_cast<int>(std::floor(sy));
                float acc = 0.0F;
                float wsum = 0.0F;
                for (int j = iy - s + 1; j <= iy + s; ++j) {
                    const float wy = kernel(plan.filter, static_cast<float>(sy - j));
                    for (int i = ix - s + 1; i <= ix + s; ++i) {
                        const float wgt = wy * kernel(plan.filter, static_cast<float>(sx - i));
                        acc += wgt * src.clamped(i, j, c);
                        wsum += wgt;
                    }
                }
                out.at(x, y, c) = wsum != 0.0F ? acc / wsum : 0.0F;
            }
        }
    }
    return out.buffer(in.spec().space);
}

image::ImageBuffer resample(const image::ImageBuffer& in, base::Size size)
{
    const Img src(in);
    const auto pass = [](const Img& from, int outLen, bool horizontal) {
        const int inLen = horizontal ? from.w : from.h;
        Img to(horizontal ? outLen : from.w, horizontal ? from.h : outLen, from.c);
        const float f = static_cast<float>(inLen) / static_cast<float>(outLen);
        const float stretch = std::max(1.0F, f);
        const float sup = 3.0F * stretch;
        for (int o = 0; o < outLen; ++o) {
            const float center = (static_cast<float>(o) + 0.5F) * f - 0.5F;
            const int i0 = static_cast<int>(std::floor(center - sup)) + 1;
            const int i1 = static_cast<int>(std::floor(center + sup));
            std::vector<float> wts;
            float wsum = 0.0F;
            for (int i = i0; i <= i1; ++i) {
                const float wgt = lanczos3((static_cast<float>(i) - center) / stretch);
                wts.push_back(wgt);
                wsum += wgt;
            }
            const int other = horizontal ? from.h : from.w;
            for (int k = 0; k < other; ++k) {
                for (int c = 0; c < from.c; ++c) {
                    float acc = 0.0F;
                    for (int i = i0; i <= i1; ++i) {
                        const float v = horizontal ? from.clamped(i, k, c) : from.clamped(k, i, c);
                        acc += wts[static_cast<std::size_t>(i - i0)] * v;
                    }
                    (horizontal ? to.at(o, k, c) : to.at(k, o, c)) = acc / wsum;
                }
            }
        }
        return to;
    };
    const Img h = pass(src, size.width, true);
    return pass(h, size.height, false).buffer(in.spec().space);
}

image::ImageBuffer multiply(const image::ImageBuffer& in, const image::ImageBuffer& gain)
{
    Img img(in);
    const Img g(gain);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            for (int c = 0; c < img.c; ++c) {
                img.at(x, y, c) *= g.at(x, y, g.c == 1 ? 0 : c);
            }
        }
    }
    return img.buffer(in.spec().space);
}

image::ImageBuffer gaussian(const image::ImageBuffer& in, float sigma)
{
    return gaussianImg(Img(in), sigma).buffer(in.spec().space);
}

image::ImageBuffer unsharp(const image::ImageBuffer& in, float amount, float radius, float threshold)
{
    Img img(in);
    const Img blur = gaussianImg(img, radius);
    for (std::size_t i = 0; i < img.v.size(); ++i) {
        const float d = img.v[i] - blur.v[i];
        if (std::abs(d) > threshold) {
            img.v[i] = img.v[i] + amount * d;
        }
    }
    return img.buffer(in.spec().space);
}

image::ImageBuffer localTone(const image::ImageBuffer& in, float shadows, float highlights, float radius)
{
    constexpr float logFloor = 1.0F / 65536.0F; // 2^-16
    Img img(in);
    Img lum(img.w, img.h, 1);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            lum.at(x, y, 0) = img.c == 1 ? img.at(x, y, 0)
                                         : 0.2627F * img.at(x, y, 0) + 0.6780F * img.at(x, y, 1) +
                                               0.0593F * img.at(x, y, 2);
        }
    }
    // Guided Filter (He et al.) der log2-Luminanz mit sich selbst: kantenerhaltende Basis.
    // Im log-Raum ist eps (Varianz in Blendenstufen²) über den ganzen Tonwertumfang gleich wirksam.
    for (float& v : lum.v) {
        v = std::log2(std::max(v, logFloor));
    }
    const int r = std::max(1, static_cast<int>(std::lround(radius)));
    constexpr double eps = 0.01;
    const auto box = [r](const Img& src) {
        Img out(src.w, src.h, 1);
        const double n = static_cast<double>((2 * r + 1) * (2 * r + 1));
        for (int y = 0; y < src.h; ++y) {
            for (int x = 0; x < src.w; ++x) {
                double sum = 0.0;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        sum += src.clamped(x + dx, y + dy, 0);
                    }
                }
                out.at(x, y, 0) = static_cast<float>(sum / n);
            }
        }
        return out;
    };
    Img sq(img.w, img.h, 1);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        sq.v[i] = lum.v[i] * lum.v[i];
    }
    const Img meanI = box(lum);
    const Img corrI = box(sq);
    Img a(img.w, img.h, 1);
    Img bb(img.w, img.h, 1);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        const float var = std::max(0.0F, corrI.v[i] - meanI.v[i] * meanI.v[i]);
        a.v[i] = var / (var + static_cast<float>(eps));
        bb.v[i] = meanI.v[i] - a.v[i] * meanI.v[i];
    }
    const Img meanA = box(a);
    const Img meanB = box(bb);
    Img base(img.w, img.h, 1);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        base.v[i] = std::exp2(meanA.v[i] * lum.v[i] + meanB.v[i]);
    }
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            const float b = base.at(x, y, 0);
            const float factor = (1.0F + shadows * 2.0F * std::max(0.0F, 0.5F - b)) *
                                 (1.0F - highlights * std::max(0.0F, b - 0.5F));
            for (int c = 0; c < img.c; ++c) {
                img.at(x, y, c) *= factor;
            }
        }
    }
    return img.buffer(in.spec().space);
}

image::ImageBuffer denoise(const image::ImageBuffer& in, float strength)
{
    const Img src(in);
    if (strength <= 0.0F) {
        return src.buffer(in.spec().space);
    }
    constexpr float sigmaS = 1.5F;
    const float sigmaR = 0.02F + 0.2F * strength;
    Img out(src.w, src.h, src.c);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            std::array<float, 3> acc{};
            float wsum = 0.0F;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    float dist2 = 0.0F;
                    for (int c = 0; c < src.c; ++c) {
                        const float d = src.clamped(x + dx, y + dy, c) - src.at(x, y, c);
                        dist2 += d * d;
                    }
                    const float wgt =
                        std::exp(-static_cast<float>(dx * dx + dy * dy) / (2.0F * sigmaS * sigmaS)) *
                        std::exp(-dist2 / (2.0F * sigmaR * sigmaR));
                    for (int c = 0; c < src.c; ++c) {
                        acc[static_cast<std::size_t>(c)] += wgt * src.clamped(x + dx, y + dy, c);
                    }
                    wsum += wgt;
                }
            }
            for (int c = 0; c < src.c; ++c) {
                out.at(x, y, c) = acc[static_cast<std::size_t>(c)] / wsum;
            }
        }
    }
    return out.buffer(in.spec().space);
}

image::ImageBuffer inpaint(const image::ImageBuffer& in, const model::SpotList& spots)
{
    constexpr float ringWidth = 2.0F; // Pixel
    Img img(in);
    for (const model::Spot& spot : spots) {
        const Img before = img;
        const float r = static_cast<float>(spot.radius) * static_cast<float>(img.w);
        const float cx = static_cast<float>(spot.center.x) * static_cast<float>(img.w);
        const float cy = static_cast<float>(spot.center.y) * static_cast<float>(img.h);
        const int dx = static_cast<int>(std::lround(spot.offset.x * img.w));
        const int dy = static_cast<int>(std::lround(spot.offset.y * img.h));
        if (r <= 0.0F) {
            continue;
        }
        const auto dist = [cx, cy](int x, int y) {
            return std::hypot(static_cast<float>(x) + 0.5F - cx, static_cast<float>(y) + 0.5F - cy);
        };
        // Heilen: Helligkeitsunterschied zwischen Ziel und Quelle auf dem Ring knapp außerhalb
        // der Stelle (r ≤ d < r + ringWidth) je Kanal ausgleichen
        std::array<double, 4> diff{};
        int ring = 0;
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                const float d = dist(x, y);
                if (d < r || d >= r + ringWidth) {
                    continue;
                }
                for (int c = 0; c < img.c; ++c) {
                    diff[static_cast<std::size_t>(c)] +=
                        before.at(x, y, c) - before.clamped(x + dx, y + dy, c);
                }
                ++ring;
            }
        }
        for (double& v : diff) {
            v = ring > 0 ? v / ring : 0.0;
        }
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                const float d = dist(x, y);
                if (d >= r) {
                    continue;
                }
                const float f = d <= 0.7F * r ? 1.0F : (r - d) / (0.3F * r);
                for (int c = 0; c < img.c; ++c) {
                    const float healed = before.clamped(x + dx, y + dy, c) +
                                         static_cast<float>(diff[static_cast<std::size_t>(c)]);
                    img.at(x, y, c) = (1.0F - f) * before.at(x, y, c) + f * healed;
                }
            }
        }
    }
    return img.buffer(in.spec().space);
}

image::Histogram histogram(const image::ImageBuffer& in, int bins)
{
    const Img img(in);
    image::Histogram h;
    for (auto& b : h.bins) {
        b.assign(static_cast<std::size_t>(bins), 0U);
    }
    std::array<std::size_t, 3> low{};
    std::array<std::size_t, 3> high{};
    const auto binOf = [bins](float v) {
        return static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::floor(v * static_cast<float>(bins))), 0, bins - 1));
    };
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            std::array<float, 3> v{};
            for (int c = 0; c < 3; ++c) {
                v[static_cast<std::size_t>(c)] = img.at(x, y, img.c == 1 ? 0 : c);
            }
            const float l = img.c == 1 ? v[0] : 0.2627F * v[0] + 0.6780F * v[1] + 0.0593F * v[2];
            for (std::size_t c = 0; c < 3; ++c) {
                ++h.bins[c][binOf(v[c])];
                low[c] += v[c] <= 0.0F ? 1U : 0U;
                high[c] += v[c] >= 1.0F ? 1U : 0U;
            }
            ++h.bins[image::Histogram::luminance][binOf(l)];
        }
    }
    const double n = static_cast<double>(img.w) * img.h;
    h.clipLow = {static_cast<double>(low[0]) / n, static_cast<double>(low[1]) / n,
                 static_cast<double>(low[2]) / n};
    h.clipHigh = {static_cast<double>(high[0]) / n, static_cast<double>(high[1]) / n,
                  static_cast<double>(high[2]) / n};
    return h;
}

} // namespace autoneg::reference
