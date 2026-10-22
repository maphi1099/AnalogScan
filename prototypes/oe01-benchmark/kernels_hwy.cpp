// Highway-Variante: SIMD mit Laufzeitauswahl des Befehlssatzes (SSE4/AVX2/AVX-512 …),
// parallel über Zeilenblöcke mit oneTBB.

#include "common.hpp"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <vector>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "kernels_hwy.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/contrib/math/math-inl.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace bench {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// Kanalabhängige Parameter bei verschachtelten Daten: Periode 3 passt nicht zur Vektorbreite N.
// Nach 3*N Werten wiederholt sich das Muster, deshalb drei vorberechnete Vektoren je Parameter.
struct Pattern
{
    std::vector<float> v;
    Pattern(const std::array<float, 3>& c, std::size_t n)
        : v(3 * n)
    {
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] = c[i % 3];
        }
    }
};

template <class D, class V>
HWY_INLINE V PointwiseVec(D d, V x, V invBase, V dMin, V scale, V exposure)
{
    const V eps = hn::Set(d, 1e-6F);
    const V v = hn::Max(hn::Mul(x, invBase), eps);
    const V dens = hn::Neg(hn::Log10(d, v));
    return hn::MulAdd(hn::Sub(dens, dMin), scale, exposure);
}

template <class D, class V>
HWY_INLINE V LutVec(D d, V x, const float* lut, int n)
{
    const hn::RebindToSigned<D> di;
    const V hi = hn::Set(d, static_cast<float>(n - 1));
    const V scaled = hn::Mul(hn::Min(hn::Max(x, hn::Zero(d)), hn::Set(d, 1.0F)), hi);
    auto idx = hn::ConvertTo(di, hn::Floor(scaled));
    idx = hn::Min(idx, hn::Set(di, n - 2));
    const V f = hn::Sub(scaled, hn::ConvertTo(d, idx));
    const V a = hn::GatherIndex(d, lut, idx);
    const V b = hn::GatherIndex(d, lut + 1, idx);
    return hn::MulAdd(f, hn::Sub(b, a), a);
}

void PointwiseRows(const Image& in, Image& out, int y0, int y1, const PointwiseParams& p, const float* lut,
                   int lutSize)
{
    const hn::ScalableTag<float> d;
    const std::size_t n = hn::Lanes(d);
    const Pattern ib(p.invBase, n), dm(p.dMin, n), sc(p.scale, n);
    const auto ex = hn::Set(d, p.exposure);
    const std::size_t rowLen = static_cast<std::size_t>(in.w) * 3;
    for (int y = y0; y < y1; ++y) {
        const float* s = in.row(y);
        float* o = out.row(y);
        std::size_t i = 0;
        for (; i + 3 * n <= rowLen; i += 3 * n) {
            for (std::size_t k = 0; k < 3; ++k) {
                auto v = PointwiseVec(d, hn::LoadU(d, s + i + k * n), hn::LoadU(d, ib.v.data() + k * n),
                                      hn::LoadU(d, dm.v.data() + k * n), hn::LoadU(d, sc.v.data() + k * n), ex);
                if (lut != nullptr) {
                    v = LutVec(d, v, lut, lutSize);
                }
                hn::StoreU(v, d, o + i + k * n);
            }
        }
        for (; i < rowLen; ++i) { // Rest skalar
            const std::size_t c = i % 3;
            float v = (-std::log10(std::max(s[i] * p.invBase[c], 1e-6F)) - p.dMin[c]) * p.scale[c] + p.exposure;
            if (lut != nullptr) {
                const float x = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(lutSize - 1);
                const int j = std::min(static_cast<int>(x), lutSize - 2);
                v = lut[j] + (x - static_cast<float>(j)) * (lut[j + 1] - lut[j]);
            }
            o[i] = v;
        }
    }
}

void LutRows(const Image& in, Image& out, int y0, int y1, const float* lut, int lutSize)
{
    const hn::ScalableTag<float> d;
    const std::size_t n = hn::Lanes(d);
    const std::size_t rowLen = static_cast<std::size_t>(in.w) * 3;
    for (int y = y0; y < y1; ++y) {
        const float* s = in.row(y);
        float* o = out.row(y);
        std::size_t i = 0;
        for (; i + n <= rowLen; i += n) {
            hn::StoreU(LutVec(d, hn::LoadU(d, s + i), lut, lutSize), d, o + i);
        }
        for (; i < rowLen; ++i) {
            const float x = std::clamp(s[i], 0.0F, 1.0F) * static_cast<float>(lutSize - 1);
            const int j = std::min(static_cast<int>(x), lutSize - 2);
            o[i] = lut[j] + (x - static_cast<float>(j)) * (lut[j + 1] - lut[j]);
        }
    }
}

void UnsharpRows(const Image& in, Image& out, int y0, int y1, const UnsharpParams& p)
{
    constexpr int r = UnsharpParams::radius;
    const auto w = p.weights();
    const hn::ScalableTag<float> d;
    const std::size_t n = hn::Lanes(d);
    const int rowLen = in.w * 3;
    const int t0 = std::max(0, y0 - r);
    const int t1 = std::min(in.h, y1 + r);
    std::vector<float> tmp(static_cast<std::size_t>(t1 - t0) * rowLen);

    // horizontal: benachbarte Pixel liegen 3 Werte auseinander
    for (int y = t0; y < t1; ++y) {
        const float* s = in.row(y);
        float* t = tmp.data() + static_cast<std::size_t>(y - t0) * rowLen;
        const auto scalarAt = [&](int i) {
            const int x = i / 3;
            const int c = i % 3;
            float acc = 0.0F;
            for (int k = -r; k <= r; ++k) {
                acc += w[static_cast<std::size_t>(k + r)] * s[std::clamp(x + k, 0, in.w - 1) * 3 + c];
            }
            t[i] = acc;
        };
        int i = 0;
        for (; i < std::min(3 * r, rowLen); ++i) {
            scalarAt(i);
        }
        const int end = rowLen - 3 * r;
        for (; i + static_cast<int>(n) <= end; i += static_cast<int>(n)) {
            auto acc = hn::Mul(hn::Set(d, w[0]), hn::LoadU(d, s + i - 3 * r));
            for (int k = -r + 1; k <= r; ++k) {
                acc = hn::MulAdd(hn::Set(d, w[static_cast<std::size_t>(k + r)]), hn::LoadU(d, s + i + 3 * k), acc);
            }
            hn::StoreU(acc, d, t + i);
        }
        for (; i < rowLen; ++i) {
            scalarAt(i);
        }
    }
    // vertikal und Kombination mit Schwellwert
    const auto amount = hn::Set(d, p.amount);
    const auto threshold = hn::Set(d, p.threshold);
    for (int y = y0; y < y1; ++y) {
        const float* rows[2 * r + 1];
        for (int k = -r; k <= r; ++k) {
            const int yy = std::clamp(y + k, 0, in.h - 1);
            rows[k + r] = tmp.data() + static_cast<std::size_t>(yy - t0) * rowLen;
        }
        const float* s = in.row(y);
        float* o = out.row(y);
        int i = 0;
        for (; i + static_cast<int>(n) <= rowLen; i += static_cast<int>(n)) {
            auto blur = hn::Mul(hn::Set(d, w[0]), hn::LoadU(d, rows[0] + i));
            for (int k = 1; k <= 2 * r; ++k) {
                blur = hn::MulAdd(hn::Set(d, w[static_cast<std::size_t>(k)]), hn::LoadU(d, rows[k] + i), blur);
            }
            const auto src = hn::LoadU(d, s + i);
            const auto diff = hn::Sub(src, blur);
            const auto mask = hn::Gt(hn::Abs(diff), threshold);
            hn::StoreU(hn::Add(src, hn::IfThenElseZero(mask, hn::Mul(amount, diff))), d, o + i);
        }
        for (; i < rowLen; ++i) {
            float blur = 0.0F;
            for (int k = 0; k <= 2 * r; ++k) {
                blur += w[static_cast<std::size_t>(k)] * rows[k][i];
            }
            const float diff = s[i] - blur;
            o[i] = std::abs(diff) > p.threshold ? s[i] + p.amount * diff : s[i];
        }
    }
}

const char* TargetName()
{
    return hwy::TargetName(HWY_TARGET);
}

} // namespace HWY_NAMESPACE
} // namespace bench
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace bench {

HWY_EXPORT(PointwiseRows);
HWY_EXPORT(LutRows);
HWY_EXPORT(UnsharpRows);
HWY_EXPORT(TargetName);

namespace {
template <class Fn>
void forBlocks(int height, Fn&& fn)
{
    const int blocks = (height + blockRows - 1) / blockRows;
    tbb::parallel_for(tbb::blocked_range<int>(0, blocks, 1), [&](const tbb::blocked_range<int>& r) {
        for (int b = r.begin(); b < r.end(); ++b) {
            fn(b * blockRows, std::min(height, (b + 1) * blockRows));
        }
    });
}
} // namespace

void hwyPointwise(const Image& in, Image& out, const PointwiseParams& p)
{
    forBlocks(in.h, [&](int y0, int y1) { HWY_DYNAMIC_DISPATCH(PointwiseRows)(in, out, y0, y1, p, nullptr, 0); });
}

void hwyLut(const Image& in, Image& out, const Lut& lut)
{
    forBlocks(in.h, [&](int y0, int y1) {
        HWY_DYNAMIC_DISPATCH(LutRows)(in, out, y0, y1, lut.values.data(), static_cast<int>(lut.values.size()));
    });
}

void hwyFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut)
{
    forBlocks(in.h, [&](int y0, int y1) {
        HWY_DYNAMIC_DISPATCH(PointwiseRows)(in, out, y0, y1, p, lut.values.data(),
                                            static_cast<int>(lut.values.size()));
    });
}

void hwyUnsharp(const Image& in, Image& out, const UnsharpParams& p)
{
    forBlocks(in.h, [&](int y0, int y1) { HWY_DYNAMIC_DISPATCH(UnsharpRows)(in, out, y0, y1, p); });
}

const char* hwyTargetName()
{
    return HWY_DYNAMIC_DISPATCH(TargetName)();
}

} // namespace bench
#endif
