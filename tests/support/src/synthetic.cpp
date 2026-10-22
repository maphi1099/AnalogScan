#include "autoneg/testing/synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

namespace autoneg::testing {

namespace {

constexpr double outputFloor = 0.01; // 10^−R mit R = 2

// Farbfelder (linear Rec. 2020), angelehnt an typische Motivfarben
constexpr std::array<base::Vec3, 12> patches{{
    {0.40, 0.20, 0.15},
    {0.75, 0.50, 0.40},
    {0.20, 0.30, 0.50},
    {0.15, 0.25, 0.10},
    {0.35, 0.35, 0.60},
    {0.30, 0.60, 0.55},
    {0.75, 0.35, 0.08},
    {0.12, 0.15, 0.45},
    {0.65, 0.15, 0.20},
    {0.20, 0.10, 0.25},
    {0.45, 0.60, 0.15},
    {0.85, 0.55, 0.10},
}};
constexpr std::array<double, 6> grays{0.03, 0.09, 0.18, 0.36, 0.59, 0.90};

double luminance(const base::Vec3& v)
{
    return 0.2627 * v.x + 0.6780 * v.y + 0.0593 * v.z;
}

base::Vec3 transmissionOf(const base::Vec3& positive, const base::Vec3& base, const base::Vec3& gamma)
{
    const auto t = [](double v, double b, double g) {
        const double p = std::clamp(v, 0.0, 1.0) * (1.0 - outputFloor) + outputFloor;
        return b * std::pow(10.0, -g * (std::log10(p) + 2.0));
    };
    return {t(positive.x, base.x, gamma.x), t(positive.y, base.y, gamma.y), t(positive.z, base.z, gamma.z)};
}

struct Layout
{
    int w = 0;
    int h = 0;
    double fx0 = 0, fx1 = 0, fy0 = 0, fy1 = 0; // erstes Bildfeld (Pixel)
    std::vector<std::pair<double, double>> fx; // waagerechte Grenzen aller Bildfelder
    double sy0 = 0, sy1 = 0;                   // Filmstreifen
    double hy0 = 0, hy1 = 0;                   // Halteröffnung
    double band = 0;                           // Höhe des Filmrands
};

} // namespace

base::Vec3 positiveAt(Pattern p, double u, double w)
{
    switch (p) {
    case Pattern::Flat:
        return {0.5, 0.5, 0.5};
    case Pattern::Gradient: {
        const double v = 0.02 + 0.95 * u;
        return {v, v, v};
    }
    case Pattern::Gray: {
        const double v = grays[static_cast<std::size_t>(std::clamp(static_cast<int>(u * 6.0), 0, 5))];
        return {v, v, v};
    }
    case Pattern::Chart:
        break;
    }
    if (w < 0.25) { // Graustufen
        const double v = grays[static_cast<std::size_t>(std::clamp(static_cast<int>(u * 6.0), 0, 5))];
        return {v, v, v};
    }
    if (w < 0.70) { // 2 x 6 Farbfelder
        const int col = std::clamp(static_cast<int>(u * 6.0), 0, 5);
        const int row = w < 0.475 ? 0 : 1;
        return patches[static_cast<std::size_t>(row * 6 + col)];
    }
    const double v = 0.02 + 0.9 * u; // Verlauf mit leichter Färbung
    return {v, v * (0.9 + 0.1 * w), v * (1.05 - 0.1 * w)};
}

image::ImageBuffer positiveImage(Pattern p, int w, int h, int channels)
{
    return imageFrom(w, h, channels, [&](int x, int y, int c) {
        const base::Vec3 v = positiveAt(p, (x + 0.5) / w, (y + 0.5) / h);
        if (channels == 1) {
            return static_cast<float>(luminance(v));
        }
        return static_cast<float>(c == 0 ? v.x : c == 1 ? v.y : v.z);
    });
}

SyntheticScan makeNegative(const NegativeSpec& s)
{
    Layout l;
    l.w = s.width;
    l.h = s.height;
    l.fx0 = std::round(s.frame.x * l.w);
    l.fx1 = std::round((s.frame.x + s.frame.w) * l.w);
    l.fy0 = std::round(s.frame.y * l.h);
    l.fy1 = std::round((s.frame.y + s.frame.h) * l.h);
    for (int i = 0; i < std::max(1, s.frames); ++i) {
        const double left = s.frame.x + i * (s.frame.w + s.frameGap);
        l.fx.emplace_back(std::round(left * l.w), std::round((left + s.frame.w) * l.w));
    }
    l.band = std::round(0.08 * l.h);
    l.sy0 = s.rebate ? l.fy0 - l.band : l.fy0;
    l.sy1 = s.rebate ? l.fy1 + l.band : l.fy1;
    l.hy0 = s.holder ? std::round(0.04 * l.h) : 0.0;
    l.hy1 = s.holder ? l.h - std::round(0.04 * l.h) : l.h;

    const bool slide = s.film == model::FilmType::Slide;
    const bool bw = s.film == model::FilmType::BwNegative;
    // S/W-Filmbasis ist neutral (keine Farbmaske): Helligkeit der angegebenen Basis in allen Kanälen
    const double bwBase = luminance(s.base);
    const base::Vec3 base = slide ? base::Vec3{1, 1, 1} : bw ? base::Vec3{bwBase, bwBase, bwBase} : s.base;
    const base::Vec3 gamma = bw ? base::Vec3{s.gamma.x, s.gamma.x, s.gamma.x} : s.gamma;
    const base::Vec3 rebateT = slide ? base::Vec3{0.004, 0.004, 0.004} : base;

    // Staubkörner (fester Seed)
    std::mt19937_64 rng(s.seed);
    struct Speck
    {
        double x, y, r;
    };
    std::vector<Speck> dust;
    for (int i = 0; i < s.dust; ++i) {
        dust.push_back({static_cast<double>(rng() % static_cast<std::uint64_t>(l.w)),
                        static_cast<double>(rng() % static_cast<std::uint64_t>(l.h)),
                        1.0 + static_cast<double>(rng() % 3)});
    }

    const auto scene = [&](double x, double y) -> base::Vec3 {
        base::Vec3 t{1, 1, 1}; // Leuchtfläche
        if (y < l.hy0 || y >= l.hy1) {
            t = {0.02, 0.02, 0.02}; // Filmhalter
        } else if (y >= l.sy0 && y < l.sy1) {
            const auto frameAt = std::ranges::find_if(
                l.fx, [&](const auto& f) { return x >= f.first && x < f.second && y >= l.fy0 && y < l.fy1; });
            if (frameAt != l.fx.end()) {
                const base::Vec3 v =
                    positiveAt(s.pattern, (x - frameAt->first) / (frameAt->second - frameAt->first),
                               (y - l.fy0) / (l.fy1 - l.fy0));
                if (slide) {
                    t = v;
                } else {
                    const base::Vec3 pv = bw ? base::Vec3{luminance(v), luminance(v), luminance(v)} : v;
                    t = transmissionOf(pv, base, gamma);
                }
            } else {
                t = rebateT;
                const bool topBand = y < l.fy0;
                const bool bottomBand = y >= l.fy1;
                if ((topBand || bottomBand) && s.perforation) {
                    // Perforation: Löcher im äußeren Teil des Rands
                    const double inner = topBand ? (y - l.sy0) / l.band : (l.sy1 - y) / l.band;
                    const double px = std::fmod(x, 0.06 * l.w);
                    if (inner < 0.55 && inner > 0.15 && px < 0.03 * l.w) {
                        t = {1, 1, 1};
                    }
                }
                if (bottomBand && s.edgeText && !slide) {
                    // Randbeschriftung: belichtete Zeichen (Dichte 0,6) nahe am Bildfeld
                    const double inner = (y - l.fy1) / l.band;
                    const double px = std::fmod(x, 0.05 * l.w);
                    if (inner < 0.35 && px > 0.035 * l.w) {
                        t = {base.x * 0.25, base.y * 0.25, base.z * 0.25};
                    }
                }
                if (topBand && s.lightLeak && x < 0.3 * l.w && !slide) {
                    const double f = 1.0 + 2.0 * (1.0 - x / (0.3 * l.w));
                    t = {std::min(1.0, t.x * f), std::min(1.0, t.y * f), std::min(1.0, t.z * f)};
                }
            }
        }
        for (const Speck& d : dust) {
            if (std::hypot(x - d.x, y - d.y) <= d.r) {
                t = {t.x * 0.05, t.y * 0.05, t.z * 0.05};
            }
        }
        return t;
    };

    // Drehung um die Bildmitte: Pixelmitte zurückdrehen und die Szene dort auswerten
    const double rad = s.angleDeg * std::numbers::pi / 180.0;
    const double c = std::cos(rad);
    const double sn = std::sin(rad);
    const double cx = l.w / 2.0;
    const double cy = l.h / 2.0;
    SyntheticScan out;
    out.scan = imageFrom(l.w, l.h, 3, [&](int x, int y, int ch) {
        const double px = x + 0.5 - cx;
        const double py = y + 0.5 - cy;
        const double ux = cx + c * px + sn * py;
        const double uy = cy - sn * px + c * py;
        const base::Vec3 t = scene(ux, uy);
        return static_cast<float>(ch == 0 ? t.x : ch == 1 ? t.y : t.z);
    });
    out.truth.frame = {l.fx0 / l.w, l.fy0 / l.h, (l.fx1 - l.fx0) / l.w, (l.fy1 - l.fy0) / l.h};
    out.truth.base = base;
    out.truth.dMax = {2.0 * gamma.x, 2.0 * gamma.y, 2.0 * gamma.z};
    out.truth.angleDeg = s.angleDeg;
    out.truth.rebateTop = {0.0, l.sy0 / l.h, 1.0, (l.fy0 - l.sy0) / l.h};
    out.truth.rebateBottom = {0.0, l.fy1 / l.h, 1.0, (l.sy1 - l.fy1) / l.h};
    for (const auto& [x0, x1] : l.fx) {
        out.truth.frames.push_back({x0 / l.w, l.fy0 / l.h, (x1 - x0) / l.w, (l.fy1 - l.fy0) / l.h});
    }
    return out;
}

image::ImageBuffer makeBlank(int w, int h, double falloff, base::Vec3 color)
{
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double maxR2 = cx * cx + cy * cy;
    return imageFrom(w, h, 3, [&](int x, int y, int c) {
        const double dx = x + 0.5 - cx;
        const double dy = y + 0.5 - cy;
        const double f = 1.0 - falloff * (dx * dx + dy * dy) / maxR2;
        return static_cast<float>(f * (c == 0 ? color.x : c == 1 ? color.y : color.z));
    });
}

image::ImageBuffer imageFrom(int w, int h, int channels, const std::function<float(int, int, int)>& f)
{
    std::vector<float> v(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                         static_cast<std::size_t>(channels));
    std::size_t i = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                v[i++] = f(x, y, c);
            }
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{w, h, channels, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v)
        .value();
}

base::Vec3 meanRgb(const image::ImageBuffer& img, int x, int y, int w, int h)
{
    const auto& s = img.spec();
    const auto v = img.host();
    double sum[3] = {0, 0, 0};
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            const std::size_t i = (static_cast<std::size_t>(yy) * static_cast<std::size_t>(s.width) +
                                   static_cast<std::size_t>(xx)) *
                                  static_cast<std::size_t>(s.channels);
            for (int c = 0; c < 3; ++c) {
                sum[c] += v[i + static_cast<std::size_t>(s.channels == 1 ? 0 : c)];
            }
        }
    }
    const double n = static_cast<double>(w) * h;
    return {sum[0] / n, sum[1] / n, sum[2] / n};
}

} // namespace autoneg::testing
