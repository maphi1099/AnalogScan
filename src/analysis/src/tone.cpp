#include "autoneg/analysis/tone.hpp"

#include "planes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace autoneg::analysis {

DensityRange AutoLevels::compute(const image::ImageBuffer& density, double clip) const
{
    const double half = std::clamp(clip, 0.0, 0.5) / 2.0;
    std::array<double, 3> lo{};
    std::array<double, 3> hi{};
    for (int c = 0; c < 3; ++c) {
        const detail::Plane p = detail::channel(density, c);
        const auto i = static_cast<std::size_t>(c);
        if (half == 0.0) {
            lo[i] = *std::ranges::min_element(p.v);
            hi[i] = *std::ranges::max_element(p.v);
        } else {
            lo[i] = detail::percentile(p.v, half);
            hi[i] = detail::percentile(p.v, 1.0 - half);
        }
    }
    return {{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}};
}

base::Vec3 AutoNeutral::compute(const image::ImageBuffer& img) const
{
    // Blockmittel 4 × 4 gegen Rauschen; nur Mitteltöne mit allen Kanälen > 0
    std::array<detail::Plane, 3> ch;
    for (int c = 0; c < 3; ++c) {
        ch[static_cast<std::size_t>(c)] = detail::blockMean(detail::channel(img, c), 4);
    }
    struct Sample
    {
        double r, g, b, lr, lb;
    };
    std::vector<Sample> samples;
    std::vector<float> lum;
    for (std::size_t i = 0; i < ch[0].v.size(); ++i) {
        const double r = ch[0].v[i];
        const double g = ch[1].v[i];
        const double b = ch[2].v[i];
        if (!(r > 1e-4 && g > 1e-4 && b > 1e-4) || !std::isfinite(r + g + b)) {
            continue;
        }
        samples.push_back({r, g, b, std::log(r / g), std::log(b / g)});
        lum.push_back(static_cast<float>(0.2627 * r + 0.678 * g + 0.0593 * b));
    }
    if (samples.empty()) {
        return {1.0, 1.0, 1.0};
    }
    // Mitteltöne: 10.–95. Perzentil der Luminanz
    const double l10 = detail::percentile(lum, 0.10);
    const double l95 = detail::percentile(lum, 0.95);
    std::vector<Sample> mid;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (lum[i] >= l10 && lum[i] <= l95) {
            mid.push_back(samples[i]);
        }
    }
    if (mid.empty()) {
        mid = samples;
    }
    // Startwert: Median der Log-Verhältnisse; dann zweimal auf die Nachbarschaft verfeinern
    std::vector<float> lr;
    std::vector<float> lb;
    for (const Sample& s : mid) {
        lr.push_back(static_cast<float>(s.lr));
        lb.push_back(static_cast<float>(s.lb));
    }
    double cr = detail::percentile(lr, 0.5);
    double cb = detail::percentile(lb, 0.5);
    for (const double radius : {0.15, 0.06}) {
        double sr = 0.0;
        double sb = 0.0;
        int n = 0;
        for (const Sample& s : mid) {
            if (std::hypot(s.lr - cr, s.lb - cb) <= radius) {
                sr += s.lr;
                sb += s.lb;
                ++n;
            }
        }
        if (n > 0) {
            cr = sr / n;
            cb = sb / n;
        }
    }
    return {std::exp(cr), 1.0, std::exp(cb)};
}

image::Histogram HistogramCalculator::compute(const image::ImageBuffer& img, int bins) const
{
    image::Histogram h;
    const int n = std::max(bins, 1);
    for (auto& b : h.bins) {
        b.assign(static_cast<std::size_t>(n), 0U);
    }
    const auto& s = img.spec();
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(s.channels);
    const std::size_t pixels = static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height);
    std::array<std::size_t, 3> low{};
    std::array<std::size_t, 3> high{};
    const auto binOf = [n](float x) {
        return static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::floor(x * static_cast<float>(n))), 0, n - 1));
    };
    for (std::size_t i = 0; i < pixels; ++i) {
        std::array<float, 3> px{};
        for (std::size_t c = 0; c < 3; ++c) {
            px[c] = v[i * ch + (ch == 1 ? 0 : c)];
            ++h.bins[c][binOf(px[c])];
            low[c] += px[c] <= 0.0F ? 1U : 0U;
            high[c] += px[c] >= 1.0F ? 1U : 0U;
        }
        ++h.bins[image::Histogram::luminance][binOf(0.2627F * px[0] + 0.6780F * px[1] + 0.0593F * px[2])];
    }
    const double total = static_cast<double>(std::max<std::size_t>(pixels, 1));
    const auto share = [total](std::size_t count) { return static_cast<double>(count) / total; };
    h.clipLow = {share(low[0]), share(low[1]), share(low[2])};
    h.clipHigh = {share(high[0]), share(high[1]), share(high[2])};
    return h;
}

} // namespace autoneg::analysis
