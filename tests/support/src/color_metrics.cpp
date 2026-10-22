#include "autoneg/testing/color_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace autoneg::testing {

namespace {

double f(double t)
{
    constexpr double e = 216.0 / 24389.0;
    constexpr double k = 24389.0 / 27.0;
    return t > e ? std::cbrt(t) : (k * t + 16.0) / 116.0;
}

double deg(double rad)
{
    return rad * 180.0 / std::numbers::pi;
}

double rad(double d)
{
    return d * std::numbers::pi / 180.0;
}

} // namespace

Lab labFromRec2020(const base::Vec3& rgb)
{
    // linear Rec. 2020 → XYZ (D65)
    const double x = 0.6369580 * rgb.x + 0.1446169 * rgb.y + 0.1688810 * rgb.z;
    const double y = 0.2627002 * rgb.x + 0.6779981 * rgb.y + 0.0593017 * rgb.z;
    const double z = 0.0000000 * rgb.x + 0.0280727 * rgb.y + 1.0609851 * rgb.z;
    const double fx = f(x / 0.95047);
    const double fy = f(y / 1.0);
    const double fz = f(z / 1.08883);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

double deltaE2000(const Lab& p, const Lab& q)
{
    // CIEDE2000 (Sharma, Wu, Dalal 2005)
    const double c1 = std::hypot(p.a, p.b);
    const double c2 = std::hypot(q.a, q.b);
    const double cm = (c1 + c2) / 2.0;
    const double g = 0.5 * (1.0 - std::sqrt(std::pow(cm, 7) / (std::pow(cm, 7) + std::pow(25.0, 7))));
    const double a1 = (1.0 + g) * p.a;
    const double a2 = (1.0 + g) * q.a;
    const double cp1 = std::hypot(a1, p.b);
    const double cp2 = std::hypot(a2, q.b);
    const auto hue = [](double b, double a) {
        if (a == 0.0 && b == 0.0) {
            return 0.0;
        }
        double h = deg(std::atan2(b, a));
        return h < 0.0 ? h + 360.0 : h;
    };
    const double h1 = hue(p.b, a1);
    const double h2 = hue(q.b, a2);
    const double dl = q.l - p.l;
    const double dc = cp2 - cp1;
    double dh = 0.0;
    if (cp1 * cp2 != 0.0) {
        dh = h2 - h1;
        if (dh > 180.0) {
            dh -= 360.0;
        } else if (dh < -180.0) {
            dh += 360.0;
        }
    }
    const double dH = 2.0 * std::sqrt(cp1 * cp2) * std::sin(rad(dh / 2.0));
    const double lm = (p.l + q.l) / 2.0;
    const double cpm = (cp1 + cp2) / 2.0;
    double hm = h1 + h2;
    if (cp1 * cp2 != 0.0) {
        if (std::abs(h1 - h2) <= 180.0) {
            hm = (h1 + h2) / 2.0;
        } else {
            hm = (h1 + h2 < 360.0) ? (h1 + h2 + 360.0) / 2.0 : (h1 + h2 - 360.0) / 2.0;
        }
    }
    const double t = 1.0 - 0.17 * std::cos(rad(hm - 30.0)) + 0.24 * std::cos(rad(2.0 * hm)) +
                     0.32 * std::cos(rad(3.0 * hm + 6.0)) - 0.20 * std::cos(rad(4.0 * hm - 63.0));
    const double dTheta = 30.0 * std::exp(-std::pow((hm - 275.0) / 25.0, 2));
    const double rc = 2.0 * std::sqrt(std::pow(cpm, 7) / (std::pow(cpm, 7) + std::pow(25.0, 7)));
    const double sl = 1.0 + 0.015 * std::pow(lm - 50.0, 2) / std::sqrt(20.0 + std::pow(lm - 50.0, 2));
    const double sc = 1.0 + 0.045 * cpm;
    const double sh = 1.0 + 0.015 * cpm * t;
    const double rt = -std::sin(rad(2.0 * dTheta)) * rc;
    return std::sqrt(std::pow(dl / sl, 2) + std::pow(dc / sc, 2) + std::pow(dH / sh, 2) +
                     rt * (dc / sc) * (dH / sh));
}

double deltaE2000(const base::Vec3& rgbA, const base::Vec3& rgbB)
{
    return deltaE2000(labFromRec2020(rgbA), labFromRec2020(rgbB));
}

DeltaStats compareImages(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    DeltaStats st;
    const auto va = a.host();
    const auto vb = b.host();
    const std::size_t ch = static_cast<std::size_t>(a.spec().channels);
    const std::size_t n = std::min(va.size(), vb.size()) / ch;
    for (std::size_t i = 0; i < n; ++i) {
        const auto px = [ch, i](std::span<const float> v) {
            return ch == 1 ? base::Vec3{v[i], v[i], v[i]}
                           : base::Vec3{v[i * ch], v[i * ch + 1], v[i * ch + 2]};
        };
        const double d = deltaE2000(px(va), px(vb));
        st.mean += d;
        st.max = std::max(st.max, d);
    }
    st.mean /= static_cast<double>(std::max<std::size_t>(n, 1));
    return st;
}

double deltaToNeutral(const base::Vec3& rgb)
{
    const Lab lab = labFromRec2020(rgb);
    return deltaE2000(lab, Lab{lab.l, 0.0, 0.0});
}

} // namespace autoneg::testing
