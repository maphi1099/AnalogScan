#include "planes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace autoneg::analysis::detail {

namespace {

constexpr float nan = std::numeric_limits<float>::quiet_NaN();

} // namespace

float Plane::sample(double x, double y) const
{
    // Pixelmitten liegen bei +0,5
    const double fx = x - 0.5;
    const double fy = y - 0.5;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= w || y0 + 1 >= h) {
        if (x0 >= -1 && y0 >= -1 && x0 + 1 <= w && y0 + 1 <= h) { // Rand: nächster Nachbar
            const int nx = std::clamp(static_cast<int>(std::lround(fx)), 0, w - 1);
            const int ny = std::clamp(static_cast<int>(std::lround(fy)), 0, h - 1);
            return at(nx, ny);
        }
        return nan;
    }
    const auto ax = static_cast<float>(fx - x0);
    const auto ay = static_cast<float>(fy - y0);
    const float top = at(x0, y0) * (1.0F - ax) + at(x0 + 1, y0) * ax;
    const float bottom = at(x0, y0 + 1) * (1.0F - ax) + at(x0 + 1, y0 + 1) * ax;
    return top * (1.0F - ay) + bottom * ay;
}

Plane luminance(const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    Plane p(s.width, s.height);
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(s.channels);
    for (std::size_t i = 0; i < p.v.size(); ++i) {
        p.v[i] = ch == 1 ? v[i] : 0.2627F * v[i * ch] + 0.6780F * v[i * ch + 1] + 0.0593F * v[i * ch + 2];
    }
    return p;
}

Plane channel(const image::ImageBuffer& img, int c)
{
    const auto& s = img.spec();
    Plane p(s.width, s.height);
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(s.channels);
    const auto k = static_cast<std::size_t>(s.channels == 1 ? 0 : c);
    for (std::size_t i = 0; i < p.v.size(); ++i) {
        p.v[i] = v[i * ch + k];
    }
    return p;
}

double percentile(std::vector<float> values, double q)
{
    std::erase_if(values, [](float x) { return !std::isfinite(x); });
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto k = static_cast<std::size_t>(std::clamp(std::floor(q * static_cast<double>(values.size())),
                                                       0.0, static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

Robust robust(std::vector<float> values)
{
    std::erase_if(values, [](float x) { return !std::isfinite(x); });
    if (values.empty()) {
        return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    }
    const double med = percentile(values, 0.5);
    for (float& x : values) {
        x = static_cast<float>(std::abs(x - med));
    }
    return {med, 1.4826 * percentile(values, 0.5)};
}

base::Point2 deskewToSource(base::Point2 q, double skewDeg, double w, double h)
{
    const double rad = skewDeg * std::numbers::pi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double dx = q.x - w / 2.0;
    const double dy = q.y - h / 2.0;
    return {w / 2.0 + c * dx - s * dy, h / 2.0 + s * dx + c * dy};
}

base::Point2 sourceToDeskew(base::Point2 p, double skewDeg, double w, double h)
{
    return deskewToSource(p, -skewDeg, w, h);
}

Plane deskew(const Plane& p, double skewDeg)
{
    if (skewDeg == 0.0) {
        return p;
    }
    Plane out(p.w, p.h);
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            const base::Point2 src = deskewToSource({x + 0.5, y + 0.5}, skewDeg, p.w, p.h);
            out.at(x, y) = p.sample(src.x, src.y);
        }
    }
    return out;
}

Plane blockMean(const Plane& p, int block)
{
    const int bw = p.w / block;
    const int bh = p.h / block;
    Plane out(bw, bh);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            double sum = 0.0;
            int n = 0;
            for (int y = by * block; y < (by + 1) * block; ++y) {
                for (int x = bx * block; x < (bx + 1) * block; ++x) {
                    const float v = p.at(x, y);
                    if (std::isfinite(v)) {
                        sum += v;
                        ++n;
                    }
                }
            }
            out.at(bx, by) = n > 0 ? static_cast<float>(sum / n) : nan;
        }
    }
    return out;
}

image::ImageBuffer toImage(const Plane& p)
{
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{p.w, p.h, 1, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, p.v)
        .value();
}

Plane fromImage(const image::ImageBuffer& img)
{
    Plane p(img.spec().width, img.spec().height);
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(img.spec().channels);
    for (std::size_t i = 0; i < p.v.size(); ++i) {
        p.v[i] = v[i * ch];
    }
    return p;
}

Plane transpose(const Plane& p)
{
    Plane out(p.h, p.w);
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            out.at(y, x) = p.at(x, y);
        }
    }
    return out;
}

} // namespace autoneg::analysis::detail
