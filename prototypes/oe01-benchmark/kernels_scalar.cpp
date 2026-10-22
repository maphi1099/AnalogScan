// Skalare Referenz: einfache Schleifen, parallel über Zeilenblöcke mit oneTBB.

#include "common.hpp"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bench {

std::array<float, 2 * UnsharpParams::radius + 1> UnsharpParams::weights() const
{
    std::array<float, 2 * radius + 1> w{};
    float sum = 0.0F;
    for (int k = -radius; k <= radius; ++k) {
        w[static_cast<std::size_t>(k + radius)] = std::exp(-0.5F * static_cast<float>(k * k) / (sigma * sigma));
        sum += w[static_cast<std::size_t>(k + radius)];
    }
    for (float& v : w) {
        v /= sum;
    }
    return w;
}

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

inline float pointwise(float v, int c, const PointwiseParams& p)
{
    const float x = std::max(v * p.invBase[c], 1e-6F);
    return (-std::log10(x) - p.dMin[c]) * p.scale[c] + p.exposure;
}

inline float lookup(float v, const Lut& lut)
{
    const float n = static_cast<float>(lut.values.size() - 1);
    const float x = std::clamp(v, 0.0F, 1.0F) * n;
    const int i = std::min(static_cast<int>(x), static_cast<int>(lut.values.size()) - 2);
    const float f = x - static_cast<float>(i);
    return lut.values[i] + f * (lut.values[i + 1] - lut.values[i]);
}

} // namespace

void scalarPointwise(const Image& in, Image& out, const PointwiseParams& p)
{
    forBlocks(in.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = in.row(y);
            float* d = out.row(y);
            for (int x = 0; x < in.w * 3; ++x) {
                d[x] = pointwise(s[x], x % 3, p);
            }
        }
    });
}

void scalarLut(const Image& in, Image& out, const Lut& lut)
{
    forBlocks(in.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = in.row(y);
            float* d = out.row(y);
            for (int x = 0; x < in.w * 3; ++x) {
                d[x] = lookup(s[x], lut);
            }
        }
    });
}

void scalarFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut)
{
    forBlocks(in.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = in.row(y);
            float* d = out.row(y);
            for (int x = 0; x < in.w * 3; ++x) {
                d[x] = lookup(pointwise(s[x], x % 3, p), lut);
            }
        }
    });
}

void scalarUnsharp(const Image& in, Image& out, const UnsharpParams& p)
{
    constexpr int r = UnsharpParams::radius;
    const auto w = p.weights();
    const int rowLen = in.w * 3;
    forBlocks(in.h, [&](int y0, int y1) {
        // horizontale Unschärfe für den Block plus Rand in einen lokalen Puffer
        const int t0 = std::max(0, y0 - r);
        const int t1 = std::min(in.h, y1 + r);
        std::vector<float> tmp(static_cast<std::size_t>(t1 - t0) * rowLen);
        for (int y = t0; y < t1; ++y) {
            const float* s = in.row(y);
            float* t = tmp.data() + static_cast<std::size_t>(y - t0) * rowLen;
            for (int x = 0; x < in.w; ++x) {
                for (int c = 0; c < 3; ++c) {
                    float acc = 0.0F;
                    for (int k = -r; k <= r; ++k) {
                        const int xx = std::clamp(x + k, 0, in.w - 1);
                        acc += w[static_cast<std::size_t>(k + r)] * s[xx * 3 + c];
                    }
                    t[x * 3 + c] = acc;
                }
            }
        }
        for (int y = y0; y < y1; ++y) {
            const float* s = in.row(y);
            float* d = out.row(y);
            for (int i = 0; i < rowLen; ++i) {
                float blur = 0.0F;
                for (int k = -r; k <= r; ++k) {
                    const int yy = std::clamp(y + k, 0, in.h - 1);
                    blur += w[static_cast<std::size_t>(k + r)] * tmp[static_cast<std::size_t>(yy - t0) * rowLen + i];
                }
                const float diff = s[i] - blur;
                d[i] = std::abs(diff) > p.threshold ? s[i] + p.amount * diff : s[i];
            }
        }
    });
}

} // namespace bench
