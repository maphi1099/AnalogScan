// Zeilenkernels mit Google Highway (OE-01). Die Datei wird von Highway je Befehlssatz neu
// übersetzt (foreach_target); zur Laufzeit wählt HWY_DYNAMIC_DISPATCH den besten verfügbaren.
// Die skalaren Restschleifen folgen exakt den Formeln der Referenz (tests/reference).

#include "kernels.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <variant>

// Alle erreichbaren Ziele übersetzen, auch EMU128 (ohne SIMD), damit U-BCK-02 jede Stufe
// erzwingen kann.
#ifndef HWY_COMPILE_ALL_ATTAINABLE
#define HWY_COMPILE_ALL_ATTAINABLE
#endif

// Reihenfolge ist zwingend (Highway): foreach_target.h vor highway.h; nicht umsortieren.
// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "kernels_hwy.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep
#include <hwy/highway.h>
#include <hwy/contrib/math/math-inl.h>
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace autoneg::backend::cpu::detail {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

using D = hn::ScalableTag<float>;
using V = hn::Vec<D>;

// --- skalare Gegenstücke (Restschleifen) -----------------------------------------------------

inline float lutScalar(const image::Lut1D& lut, float x)
{
    const int count = static_cast<int>(lut.values.size());
    if (count == 1) {
        return lut.values[0];
    }
    const float t = (std::clamp(x, lut.inMin, lut.inMax) - lut.inMin) / (lut.inMax - lut.inMin) *
                    static_cast<float>(count - 1);
    const int i = std::min(static_cast<int>(t), count - 2);
    const float f = t - static_cast<float>(i);
    const auto ui = static_cast<std::size_t>(i);
    return lut.values[ui] + f * (lut.values[ui + 1] - lut.values[ui]);
}

// --- Vektorbausteine ---------------------------------------------------------------------

HWY_INLINE V LutVec(D d, V x, const image::Lut1D& lut)
{
    const int count = static_cast<int>(lut.values.size());
    if (count == 1) {
        return hn::Set(d, lut.values[0]);
    }
    const hn::RebindToSigned<D> di;
    const V lo = hn::Set(d, lut.inMin);
    const V t = hn::Mul(hn::Div(hn::Sub(hn::Min(hn::Max(x, lo), hn::Set(d, lut.inMax)), lo),
                                hn::Set(d, lut.inMax - lut.inMin)),
                        hn::Set(d, static_cast<float>(count - 1)));
    const auto idx = hn::Min(hn::ConvertTo(di, t), hn::Set(di, count - 2));
    const V f = hn::Sub(t, hn::ConvertTo(d, idx));
    const V a = hn::GatherIndex(d, lut.values.data(), idx);
    const V b = hn::GatherIndex(d, lut.values.data() + 1, idx);
    return hn::MulAdd(f, hn::Sub(b, a), a);
}

/// Wendet fn(v) auf alle Werte an (kanalunabhängige Operationen).
template <class VecFn, class ScalarFn>
HWY_INLINE void Flat(float* p, std::size_t len, VecFn vf, ScalarFn sf)
{
    const D d;
    const std::size_t n = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + n <= len; i += n) {
        hn::StoreU(vf(d, hn::LoadU(d, p + i)), d, p + i);
    }
    for (; i < len; ++i) {
        p[i] = sf(p[i]);
    }
}

/// Wendet fn(r, g, b) auf verschachtelte RGB-Pixel an.
template <class VecFn, class ScalarFn>
HWY_INLINE void Rgb(float* p, std::size_t pixels, VecFn vf, ScalarFn sf)
{
    const D d;
    const std::size_t n = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + n <= pixels; i += n) {
        V r, g, b;
        hn::LoadInterleaved3(d, p + 3 * i, r, g, b);
        vf(d, r, g, b);
        hn::StoreInterleaved3(r, g, b, d, p + 3 * i);
    }
    for (; i < pixels; ++i) {
        sf(p[3 * i], p[3 * i + 1], p[3 * i + 2]);
    }
}

void PixelRow(const image::PixelProgram& prog, const float* in, float* out, std::size_t pixels, int channels)
{
    const std::size_t len = pixels * static_cast<std::size_t>(channels);
    if (in != out) {
        std::copy(in, in + len, out);
    }
    const bool rgb = channels == 3;
    for (const image::PixelOp& op : prog.ops) {
        if (const auto* m = std::get_if<image::Matrix3>(&op)) {
            const auto f = [&](int r, int c) { return static_cast<float>(m->m(r, c)); };
            if (!rgb) {
                const float s = f(0, 0);
                Flat(
                    out, len, [s](D d, V v) { return hn::Mul(hn::Set(d, s), v); },
                    [s](float v) { return s * v; });
                continue;
            }
            Rgb(
                out, pixels,
                [&](D d, V& r, V& g, V& b) {
                    const V nr =
                        hn::MulAdd(hn::Set(d, f(0, 2)), b,
                                   hn::MulAdd(hn::Set(d, f(0, 1)), g, hn::Mul(hn::Set(d, f(0, 0)), r)));
                    const V ng =
                        hn::MulAdd(hn::Set(d, f(1, 2)), b,
                                   hn::MulAdd(hn::Set(d, f(1, 1)), g, hn::Mul(hn::Set(d, f(1, 0)), r)));
                    const V nb =
                        hn::MulAdd(hn::Set(d, f(2, 2)), b,
                                   hn::MulAdd(hn::Set(d, f(2, 1)), g, hn::Mul(hn::Set(d, f(2, 0)), r)));
                    r = nr;
                    g = ng;
                    b = nb;
                },
                [&](float& r, float& g, float& b) {
                    const float nr = f(0, 0) * r + f(0, 1) * g + f(0, 2) * b;
                    const float ng = f(1, 0) * r + f(1, 1) * g + f(1, 2) * b;
                    const float nb = f(2, 0) * r + f(2, 1) * g + f(2, 2) * b;
                    r = nr;
                    g = ng;
                    b = nb;
                });
        } else if (const auto* o = std::get_if<image::Offset3>(&op)) {
            const float ox = static_cast<float>(o->v.x);
            const float oy = static_cast<float>(o->v.y);
            const float oz = static_cast<float>(o->v.z);
            if (!rgb) {
                Flat(
                    out, len, [ox](D d, V v) { return hn::Add(v, hn::Set(d, ox)); },
                    [ox](float v) { return v + ox; });
                continue;
            }
            Rgb(
                out, pixels,
                [&](D d, V& r, V& g, V& b) {
                    r = hn::Add(r, hn::Set(d, ox));
                    g = hn::Add(g, hn::Set(d, oy));
                    b = hn::Add(b, hn::Set(d, oz));
                },
                [&](float& r, float& g, float& b) {
                    r += ox;
                    g += oy;
                    b += oz;
                });
        } else if (const auto* s = std::get_if<image::Scale3>(&op)) {
            const float sx = static_cast<float>(s->v.x);
            const float sy = static_cast<float>(s->v.y);
            const float sz = static_cast<float>(s->v.z);
            if (!rgb) {
                Flat(
                    out, len, [sx](D d, V v) { return hn::Mul(v, hn::Set(d, sx)); },
                    [sx](float v) { return v * sx; });
                continue;
            }
            Rgb(
                out, pixels,
                [&](D d, V& r, V& g, V& b) {
                    r = hn::Mul(r, hn::Set(d, sx));
                    g = hn::Mul(g, hn::Set(d, sy));
                    b = hn::Mul(b, hn::Set(d, sz));
                },
                [&](float& r, float& g, float& b) {
                    r *= sx;
                    g *= sy;
                    b *= sz;
                });
        } else if (std::holds_alternative<image::Log10>(op)) {
            Flat(
                out, len, [](D d, V v) { return hn::Log10(d, hn::Max(v, hn::Set(d, 1e-6F))); },
                [](float v) { return std::log10(std::max(v, 1e-6F)); });
        } else if (std::holds_alternative<image::Exp10>(op)) {
            Flat(
                out, len,
                [](D d, V v) { return hn::Exp(d, hn::Mul(v, hn::Set(d, std::numbers::ln10_v<float>))); },
                [](float v) { return std::pow(10.0F, v); });
        } else if (const auto* l = std::get_if<image::LutRef>(&op)) {
            const image::Lut1D& lut = **l;
            if (lut.channel < 0) {
                Flat(
                    out, len, [&](D d, V v) { return LutVec(d, v, lut); },
                    [&](float v) { return lutScalar(lut, v); });
            } else if (!rgb) {
                if (lut.channel == 0) {
                    Flat(
                        out, len, [&](D d, V v) { return LutVec(d, v, lut); },
                        [&](float v) { return lutScalar(lut, v); });
                }
            } else {
                const int ch = lut.channel;
                Rgb(
                    out, pixels,
                    [&](D d, V& r, V& g, V& b) {
                        V& t = ch == 0 ? r : ch == 1 ? g : b;
                        t = LutVec(d, t, lut);
                    },
                    [&](float& r, float& g, float& b) {
                        float& t = ch == 0 ? r : ch == 1 ? g : b;
                        t = lutScalar(lut, t);
                    });
            }
        } else if (const auto* c = std::get_if<image::Clamp>(&op)) {
            const float lo = c->lo;
            const float hi = c->hi;
            Flat(
                out, len, [lo, hi](D d, V v) { return hn::Min(hn::Max(v, hn::Set(d, lo)), hn::Set(d, hi)); },
                [lo, hi](float v) { return std::clamp(v, lo, hi); });
        }
    }
}

void MultiplyRow(const float* in, const float* gain, float* out, std::size_t pixels, int channels,
                 int gainChannels)
{
    const D d;
    const std::size_t n = hn::Lanes(d);
    const std::size_t len = pixels * static_cast<std::size_t>(channels);
    if (gainChannels == channels) {
        std::size_t i = 0;
        for (; i + n <= len; i += n) {
            hn::StoreU(hn::Mul(hn::LoadU(d, in + i), hn::LoadU(d, gain + i)), d, out + i);
        }
        for (; i < len; ++i) {
            out[i] = in[i] * gain[i];
        }
        return;
    }
    // Verstärkung mit einem Kanal für alle Kanäle
    const auto ch = static_cast<std::size_t>(channels);
    for (std::size_t p = 0; p < pixels; ++p) {
        for (std::size_t c = 0; c < ch; ++c) {
            out[p * ch + c] = in[p * ch + c] * gain[p];
        }
    }
}

void ConvolveRowH(const float* in, float* out, int width, int channels, std::span<const float> w)
{
    const D d;
    const int n = static_cast<int>(hn::Lanes(d));
    const int r = static_cast<int>(w.size() / 2);
    const int ch = channels;
    const int len = width * ch;
    const auto scalarAt = [&](int i) {
        const int x = i / ch;
        const int c = i % ch;
        float acc = 0.0F;
        for (int k = -r; k <= r; ++k) {
            acc += w[static_cast<std::size_t>(k + r)] * in[std::clamp(x + k, 0, width - 1) * ch + c];
        }
        out[i] = acc;
    };
    const int begin = std::min(r * ch, len);
    const int end = std::max(begin, (width - r) * ch);
    int i = 0;
    for (; i < begin; ++i) {
        scalarAt(i);
    }
    for (; i + n <= end; i += n) {
        V acc = hn::Mul(hn::Set(d, w[0]), hn::LoadU(d, in + i - r * ch));
        for (int k = -r + 1; k <= r; ++k) {
            acc = hn::MulAdd(hn::Set(d, w[static_cast<std::size_t>(k + r)]), hn::LoadU(d, in + i + k * ch),
                             acc);
        }
        hn::StoreU(acc, d, out + i);
    }
    for (; i < len; ++i) {
        scalarAt(i);
    }
}

void ConvolveRowV(const float* const* rows, float* out, std::size_t len, std::span<const float> w)
{
    const D d;
    const std::size_t n = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + n <= len; i += n) {
        V acc = hn::Mul(hn::Set(d, w[0]), hn::LoadU(d, rows[0] + i));
        for (std::size_t k = 1; k < w.size(); ++k) {
            acc = hn::MulAdd(hn::Set(d, w[k]), hn::LoadU(d, rows[k] + i), acc);
        }
        hn::StoreU(acc, d, out + i);
    }
    for (; i < len; ++i) {
        float acc = 0.0F;
        for (std::size_t k = 0; k < w.size(); ++k) {
            acc += w[k] * rows[k][i];
        }
        out[i] = acc;
    }
}

void UnsharpRow(const float* in, const float* blur, float* out, std::size_t len, float a, float t)
{
    const D d;
    const std::size_t n = hn::Lanes(d);
    const V va = hn::Set(d, a);
    const V vt = hn::Set(d, t);
    std::size_t i = 0;
    for (; i + n <= len; i += n) {
        const V src = hn::LoadU(d, in + i);
        const V diff = hn::Sub(src, hn::LoadU(d, blur + i));
        hn::StoreU(hn::IfThenElse(hn::Gt(hn::Abs(diff), vt), hn::MulAdd(va, diff, src), src), d, out + i);
    }
    for (; i < len; ++i) {
        const float diff = in[i] - blur[i];
        out[i] = std::abs(diff) > t ? in[i] + a * diff : in[i];
    }
}

void LuminanceRow(const float* in, float* lum, std::size_t pixels, int channels)
{
    if (channels != 3) {
        const auto ch = static_cast<std::size_t>(channels);
        for (std::size_t p = 0; p < pixels; ++p) {
            lum[p] = in[p * ch];
        }
        return;
    }
    const D d;
    const std::size_t n = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + n <= pixels; i += n) {
        V r, g, b;
        hn::LoadInterleaved3(d, in + 3 * i, r, g, b);
        const V l = hn::MulAdd(hn::Set(d, 0.0593F), b,
                               hn::MulAdd(hn::Set(d, 0.6780F), g, hn::Mul(hn::Set(d, 0.2627F), r)));
        hn::StoreU(l, d, lum + i);
    }
    for (; i < pixels; ++i) {
        lum[i] = 0.2627F * in[3 * i] + 0.6780F * in[3 * i + 1] + 0.0593F * in[3 * i + 2];
    }
}

void ToneRow(const float* in, const float* base, float* out, std::size_t pixels, int channels, float s,
             float h)
{
    const auto ch = static_cast<std::size_t>(channels);
    const auto factor = [s, h](float b) {
        return (1.0F + s * 2.0F * std::max(0.0F, 0.5F - b)) * (1.0F - h * std::max(0.0F, b - 0.5F));
    };
    if (channels != 3) {
        for (std::size_t p = 0; p < pixels; ++p) {
            const float f = factor(base[p]);
            for (std::size_t c = 0; c < ch; ++c) {
                out[p * ch + c] = in[p * ch + c] * f;
            }
        }
        return;
    }
    const D d;
    const std::size_t n = hn::Lanes(d);
    const V zero = hn::Zero(d);
    const V half = hn::Set(d, 0.5F);
    const V one = hn::Set(d, 1.0F);
    std::size_t i = 0;
    for (; i + n <= pixels; i += n) {
        const V b = hn::LoadU(d, base + i);
        const V fs = hn::Add(one, hn::Mul(hn::Set(d, s * 2.0F), hn::Max(zero, hn::Sub(half, b))));
        const V fh = hn::Sub(one, hn::Mul(hn::Set(d, h), hn::Max(zero, hn::Sub(b, half))));
        const V f = hn::Mul(fs, fh);
        V r, g, bl;
        hn::LoadInterleaved3(d, in + 3 * i, r, g, bl);
        hn::StoreInterleaved3(hn::Mul(r, f), hn::Mul(g, f), hn::Mul(bl, f), d, out + 3 * i);
    }
    for (; i < pixels; ++i) {
        const float f = factor(base[i]);
        out[3 * i] = in[3 * i] * f;
        out[3 * i + 1] = in[3 * i + 1] * f;
        out[3 * i + 2] = in[3 * i + 2] * f;
    }
}

} // namespace HWY_NAMESPACE
} // namespace autoneg::backend::cpu::detail
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace autoneg::backend::cpu::detail {

HWY_EXPORT(PixelRow);
HWY_EXPORT(MultiplyRow);
HWY_EXPORT(ConvolveRowH);
HWY_EXPORT(ConvolveRowV);
HWY_EXPORT(UnsharpRow);
HWY_EXPORT(LuminanceRow);
HWY_EXPORT(ToneRow);

void pixelRow(const image::PixelProgram& p, const float* in, float* out, std::size_t pixels, int channels)
{
    HWY_DYNAMIC_DISPATCH(PixelRow)(p, in, out, pixels, channels);
}

void multiplyRow(const float* in, const float* gain, float* out, std::size_t pixels, int channels,
                 int gainChannels)
{
    HWY_DYNAMIC_DISPATCH(MultiplyRow)(in, gain, out, pixels, channels, gainChannels);
}

void convolveRowH(const float* in, float* out, int width, int channels, std::span<const float> weights)
{
    HWY_DYNAMIC_DISPATCH(ConvolveRowH)(in, out, width, channels, weights);
}

void convolveRowV(const float* const* rows, float* out, std::size_t len, std::span<const float> weights)
{
    HWY_DYNAMIC_DISPATCH(ConvolveRowV)(rows, out, len, weights);
}

void unsharpRow(const float* in, const float* blur, float* out, std::size_t len, float a, float t)
{
    HWY_DYNAMIC_DISPATCH(UnsharpRow)(in, blur, out, len, a, t);
}

void luminanceRow(const float* in, float* lum, std::size_t pixels, int channels)
{
    HWY_DYNAMIC_DISPATCH(LuminanceRow)(in, lum, pixels, channels);
}

void toneRow(const float* in, const float* base, float* out, std::size_t pixels, int channels, float s,
             float h)
{
    HWY_DYNAMIC_DISPATCH(ToneRow)(in, base, out, pixels, channels, s, h);
}

std::int64_t supportedTargets()
{
    return hwy::SupportedTargets();
}

std::int64_t compiledTargets()
{
    return HWY_TARGETS;
}

const char* targetName(std::int64_t bits)
{
    return hwy::TargetName(bits);
}

void forceTargets(std::int64_t bits)
{
    hwy::SetSupportedTargetsForTest(bits);
}

void resetTargets()
{
    hwy::SetSupportedTargetsForTest(0);
}

} // namespace autoneg::backend::cpu::detail
#endif
