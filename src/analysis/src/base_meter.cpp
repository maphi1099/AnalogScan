#include "autoneg/analysis/base_meter.hpp"

#include "planes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace autoneg::analysis {

namespace {

/// Robuster Mittelwert: Ausreißer der Luminanz (> 3 σ vom Median) verwerfen, dann Median je
/// Kanal und MAD als Streuung.
base::Expected<BaseMeasurement> robustBase(const image::ImageBuffer& img,
                                           const std::vector<std::size_t>& pixels, BaseMethod method)
{
    if (pixels.size() < 20) {
        return base::makeError(base::ErrorCode::NotFound, "Zu wenige Messpunkte im Filmrand");
    }
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(img.spec().channels);
    const auto value = [&](std::size_t i, std::size_t c) { return v[i * ch + (ch == 1 ? 0 : c)]; };
    std::vector<float> lum;
    lum.reserve(pixels.size());
    for (const std::size_t i : pixels) {
        lum.push_back(0.2627F * value(i, 0) + 0.6780F * value(i, 1) + 0.0593F * value(i, 2));
    }
    const detail::Robust r = detail::robust(lum);
    const double limit = 3.0 * std::max(r.mad, 1e-4 * std::abs(r.median) + 1e-7);
    std::array<std::vector<float>, 3> kept;
    for (std::size_t k = 0; k < pixels.size(); ++k) {
        if (std::abs(lum[k] - r.median) <= limit) {
            for (std::size_t c = 0; c < 3; ++c) {
                kept[c].push_back(value(pixels[k], c));
            }
        }
    }
    if (kept[0].size() < 20) {
        return base::makeError(base::ErrorCode::NotFound, "Filmrand zu stark gestört");
    }
    BaseMeasurement m;
    m.samples = static_cast<int>(kept[0].size());
    m.method = method;
    const detail::Robust rr = detail::robust(kept[0]);
    const detail::Robust rg = detail::robust(kept[1]);
    const detail::Robust rb = detail::robust(kept[2]);
    m.transmission = {rr.median, rg.median, rb.median};
    m.spread = {rr.mad, rg.mad, rb.mad};
    return m;
}

} // namespace

base::Expected<BaseMeasurement> BaseMeter::measure(const image::ImageBuffer& img, const RegionMap& m) const
{
    if (img.empty() || img.host().empty() || m.empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Basismessung ohne Bild oder Bereiche");
    }
    const auto& s = img.spec();
    // Abstand zur Klassengrenze im Klassenbild (Unschärfe der Kanten, Antialiasing)
    constexpr int margin = 2;
    const double sx = static_cast<double>(m.width()) / s.width;
    const double sy = static_cast<double>(m.height()) / s.height;
    std::vector<std::size_t> pixels;
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const model::NormPoint p{(x + 0.5) / s.width, (y + 0.5) / s.height};
            if (m.classOf(p) != RegionClass::Rebate) {
                continue;
            }
            bool interior = true;
            for (const auto [dx, dy] :
                 {std::pair{margin, 0}, std::pair{-margin, 0}, std::pair{0, margin}, std::pair{0, -margin}}) {
                const model::NormPoint q{p.x + dx / (sx * s.width), p.y + dy / (sy * s.height)};
                interior = interior && m.classOf(q) == RegionClass::Rebate;
            }
            if (interior) {
                pixels.push_back(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                                 static_cast<std::size_t>(x));
            }
        }
    }
    return robustBase(img, pixels, BaseMethod::Rebate);
}

base::Expected<BaseMeasurement> BaseMeter::measureRect(const image::ImageBuffer& img,
                                                       const model::NormRect& r) const
{
    if (img.empty() || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Basismessung ohne Bild");
    }
    const auto& s = img.spec();
    const int x0 = std::clamp(static_cast<int>(std::floor(r.x * s.width)), 0, s.width);
    const int x1 = std::clamp(static_cast<int>(std::ceil((r.x + r.w) * s.width)), 0, s.width);
    const int y0 = std::clamp(static_cast<int>(std::floor(r.y * s.height)), 0, s.height);
    const int y1 = std::clamp(static_cast<int>(std::ceil((r.y + r.h) * s.height)), 0, s.height);
    std::vector<std::size_t> pixels;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            pixels.push_back(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                             static_cast<std::size_t>(x));
        }
    }
    return robustBase(img, pixels, BaseMethod::Manual);
}

BaseMeasurement BaseEstimator::estimate(const image::ImageBuffer& img) const
{
    BaseMeasurement m;
    m.method = BaseMethod::Statistical;
    std::array<double, 3> t{};
    std::array<double, 3> spread{};
    for (int c = 0; c < 3; ++c) {
        const detail::Plane blocks = detail::blockMean(detail::channel(img, c), 8);
        t[static_cast<std::size_t>(c)] = detail::percentile(blocks.v, 0.995);
        // Streuung der hellsten 2 % als Maß der Unsicherheit
        const double p98 = detail::percentile(blocks.v, 0.98);
        spread[static_cast<std::size_t>(c)] = t[static_cast<std::size_t>(c)] - p98;
        m.samples = static_cast<int>(blocks.v.size());
    }
    m.transmission = {t[0], t[1], t[2]};
    m.spread = {spread[0], spread[1], spread[2]};
    return m;
}

} // namespace autoneg::analysis
