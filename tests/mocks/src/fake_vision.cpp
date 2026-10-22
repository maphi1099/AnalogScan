#include "autoneg/mocks/fake_vision.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace autoneg::mocks {

namespace {

struct Grid
{
    int w = 0;
    int h = 0;
    std::vector<float> v;

    Grid(int width, int height)
        : w(width)
        , h(height)
        , v(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F)
    {}
    float& at(int x, int y)
    {
        return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
    }
    [[nodiscard]] float at(int x, int y) const
    {
        return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
    }
    [[nodiscard]] float clamped(int x, int y) const
    {
        return at(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1));
    }
};

base::Expected<Grid> gridOf(const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    if (img.empty() || s.format != image::PixelFormat::F32 || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "IVision erwartet F32 im Hauptspeicher");
    }
    Grid g(s.width, s.height);
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(s.channels);
    for (std::size_t i = 0; i < g.v.size(); ++i) {
        g.v[i] = ch == 1 ? v[i] : 0.2627F * v[i * ch] + 0.6780F * v[i * ch + 1] + 0.0593F * v[i * ch + 2];
    }
    return g;
}

image::ImageBuffer imageOf(const Grid& g)
{
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{g.w, g.h, 1, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, g.v)
        .value();
}

Grid gaussian(const Grid& in, float sigma)
{
    if (sigma <= 0.0F) {
        return in;
    }
    const int r = std::max(1, static_cast<int>(std::ceil(3.0F * sigma)));
    std::vector<float> k(static_cast<std::size_t>(2 * r + 1));
    float sum = 0.0F;
    for (int i = -r; i <= r; ++i) {
        k[static_cast<std::size_t>(i + r)] = std::exp(-0.5F * static_cast<float>(i * i) / (sigma * sigma));
        sum += k[static_cast<std::size_t>(i + r)];
    }
    for (float& x : k) {
        x /= sum;
    }
    Grid tmp(in.w, in.h);
    Grid out(in.w, in.h);
    for (int y = 0; y < in.h; ++y) {
        for (int x = 0; x < in.w; ++x) {
            float a = 0.0F;
            for (int i = -r; i <= r; ++i) {
                a += k[static_cast<std::size_t>(i + r)] * in.clamped(x + i, y);
            }
            tmp.at(x, y) = a;
        }
    }
    for (int y = 0; y < in.h; ++y) {
        for (int x = 0; x < in.w; ++x) {
            float a = 0.0F;
            for (int i = -r; i <= r; ++i) {
                a += k[static_cast<std::size_t>(i + r)] * tmp.clamped(x, y + i);
            }
            out.at(x, y) = a;
        }
    }
    return out;
}

} // namespace

base::Expected<image::ImageBuffer> FakeVision::edges(const image::ImageBuffer& img, float sigma)
{
    auto g = gridOf(img);
    if (!g) {
        return std::unexpected(g.error());
    }
    const Grid s = gaussian(*g, sigma);
    Grid gx(s.w, s.h);
    Grid gy(s.w, s.h);
    Grid mag(s.w, s.h);
    float maxMag = 0.0F;
    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            const float dx = (s.clamped(x + 1, y - 1) + 2 * s.clamped(x + 1, y) + s.clamped(x + 1, y + 1)) -
                             (s.clamped(x - 1, y - 1) + 2 * s.clamped(x - 1, y) + s.clamped(x - 1, y + 1));
            const float dy = (s.clamped(x - 1, y + 1) + 2 * s.clamped(x, y + 1) + s.clamped(x + 1, y + 1)) -
                             (s.clamped(x - 1, y - 1) + 2 * s.clamped(x, y - 1) + s.clamped(x + 1, y - 1));
            gx.at(x, y) = dx;
            gy.at(x, y) = dy;
            mag.at(x, y) = std::hypot(dx, dy);
            maxMag = std::max(maxMag, mag.at(x, y));
        }
    }
    const float low = 0.1F * maxMag;
    const float high = 0.2F * maxMag;
    // Nicht-Maxima unterdrücken (vier Richtungen)
    Grid nms(s.w, s.h);
    for (int y = 1; y + 1 < s.h; ++y) {
        for (int x = 1; x + 1 < s.w; ++x) {
            const float m = mag.at(x, y);
            if (m < low || m <= 0.0F) {
                continue;
            }
            const double a = std::atan2(gy.at(x, y), gx.at(x, y)) * 180.0 / std::numbers::pi;
            const double d = a < 0 ? a + 180.0 : a;
            float n1 = 0.0F;
            float n2 = 0.0F;
            if (d < 22.5 || d >= 157.5) {
                n1 = mag.at(x - 1, y);
                n2 = mag.at(x + 1, y);
            } else if (d < 67.5) {
                n1 = mag.at(x - 1, y - 1);
                n2 = mag.at(x + 1, y + 1);
            } else if (d < 112.5) {
                n1 = mag.at(x, y - 1);
                n2 = mag.at(x, y + 1);
            } else {
                n1 = mag.at(x + 1, y - 1);
                n2 = mag.at(x - 1, y + 1);
            }
            if (m >= n1 && m > n2) {
                nms.at(x, y) = m;
            }
        }
    }
    // Hysterese: starke Punkte und mit ihnen verbundene schwache
    Grid out(s.w, s.h);
    std::vector<std::pair<int, int>> stack;
    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            if (nms.at(x, y) >= high) {
                out.at(x, y) = 1.0F;
                stack.emplace_back(x, y);
            }
        }
    }
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < s.w && ny < s.h && out.at(nx, ny) == 0.0F &&
                    nms.at(nx, ny) >= low) {
                    out.at(nx, ny) = 1.0F;
                    stack.emplace_back(nx, ny);
                }
            }
        }
    }
    return imageOf(out);
}

base::Expected<std::vector<ports::Polygon>> FakeVision::contours(const image::ImageBuffer& mask)
{
    auto g = gridOf(mask);
    if (!g) {
        return std::unexpected(g.error());
    }
    // Zusammenhangskomponenten (8er-Nachbarschaft); Kontur als umschließendes Rechteck der
    // Randpixel (gleiche Ausdehnung wie die äußere Kontur)
    std::vector<int> label(g->v.size(), 0);
    std::vector<ports::Polygon> out;
    int next = 0;
    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            const auto i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(g->w) + static_cast<std::size_t>(x);
            if (g->v[i] < 0.5F || label[i] != 0) {
                continue;
            }
            ++next;
            int minX = x;
            int maxX = x;
            int minY = y;
            int maxY = y;
            std::vector<std::pair<int, int>> stack{{x, y}};
            label[i] = next;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= g->w || ny >= g->h) {
                            continue;
                        }
                        const auto j = static_cast<std::size_t>(ny) * static_cast<std::size_t>(g->w) +
                                       static_cast<std::size_t>(nx);
                        if (g->v[j] >= 0.5F && label[j] == 0) {
                            label[j] = next;
                            stack.emplace_back(nx, ny);
                        }
                    }
                }
            }
            out.push_back({{{static_cast<double>(minX), static_cast<double>(minY)},
                            {static_cast<double>(maxX), static_cast<double>(minY)},
                            {static_cast<double>(maxX), static_cast<double>(maxY)},
                            {static_cast<double>(minX), static_cast<double>(maxY)}}});
        }
    }
    return out;
}

base::Expected<std::vector<ports::Line>> FakeVision::lines(const image::ImageBuffer& edges)
{
    auto g = gridOf(edges);
    if (!g) {
        return std::unexpected(g.error());
    }
    std::vector<std::pair<int, int>> pts;
    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            if (g->at(x, y) >= 0.5F) {
                pts.emplace_back(x, y);
            }
        }
    }
    // Hough: Winkel 0,25°, Abstand 1 px
    constexpr int thetas = 720;
    const int maxRho = static_cast<int>(std::ceil(std::hypot(g->w, g->h)));
    const int rhos = 2 * maxRho + 1;
    std::vector<int> acc(static_cast<std::size_t>(thetas) * static_cast<std::size_t>(rhos), 0);
    std::vector<double> cs(thetas);
    std::vector<double> sn(thetas);
    for (int t = 0; t < thetas; ++t) {
        const double a = t * std::numbers::pi / thetas;
        cs[static_cast<std::size_t>(t)] = std::cos(a);
        sn[static_cast<std::size_t>(t)] = std::sin(a);
    }
    for (const auto& [x, y] : pts) {
        for (int t = 0; t < thetas; ++t) {
            const int r = static_cast<int>(std::lround(x * cs[static_cast<std::size_t>(t)] +
                                                       y * sn[static_cast<std::size_t>(t)])) +
                          maxRho;
            ++acc[static_cast<std::size_t>(t) * static_cast<std::size_t>(rhos) + static_cast<std::size_t>(r)];
        }
    }
    // Spitzen mit Unterdrückung der Nachbarschaft (±8 Winkelstufen, ±3 px)
    struct Peak
    {
        int votes, t, r;
    };
    std::vector<Peak> peaks;
    for (int t = 0; t < thetas; ++t) {
        for (int r = 0; r < rhos; ++r) {
            const int v = acc[static_cast<std::size_t>(t) * static_cast<std::size_t>(rhos) +
                              static_cast<std::size_t>(r)];
            if (v >= 20) {
                peaks.push_back({v, t, r});
            }
        }
    }
    std::ranges::sort(peaks, [](const Peak& a, const Peak& b) { return a.votes > b.votes; });
    std::vector<Peak> kept;
    for (const Peak& p : peaks) {
        const bool near = std::ranges::any_of(kept, [&](const Peak& k) {
            const int dt = std::min(std::abs(p.t - k.t), thetas - std::abs(p.t - k.t));
            return dt <= 8 && std::abs(p.r - k.r) <= 3;
        });
        if (!near) {
            kept.push_back(p);
        }
        if (kept.size() >= 64) {
            break;
        }
    }
    // Strecken: Punkte nahe der Geraden entlang der Richtung ordnen, an Lücken > 3 px trennen
    std::vector<ports::Line> out;
    for (const Peak& p : kept) {
        const double c = cs[static_cast<std::size_t>(p.t)];
        const double s = sn[static_cast<std::size_t>(p.t)];
        const double rho = p.r - maxRho;
        std::vector<double> along;
        for (const auto& [x, y] : pts) {
            if (std::abs(x * c + y * s - rho) <= 1.0) {
                along.push_back(-x * s + y * c);
            }
        }
        std::ranges::sort(along);
        std::size_t start = 0;
        for (std::size_t i = 1; i <= along.size(); ++i) {
            if (i == along.size() || along[i] - along[i - 1] > 3.0) {
                const double a0 = along[start];
                const double a1 = along[i - 1];
                if (i - start >= 20 && a1 - a0 >= 20.0) {
                    out.push_back(
                        {{rho * c - a0 * s, rho * s + a0 * c}, {rho * c - a1 * s, rho * s + a1 * c}});
                }
                start = i;
            }
        }
    }
    return out;
}

base::Expected<image::ImageBuffer> FakeVision::morphology(const image::ImageBuffer& mask, ports::MorphOp op,
                                                          int radius)
{
    auto g = gridOf(mask);
    if (!g) {
        return std::unexpected(g.error());
    }
    // elliptisches Strukturelement wie OpenCV (getStructuringElement MORPH_ELLIPSE)
    const int r = std::max(radius, 0);
    std::vector<std::pair<int, int>> element;
    for (int dy = -r; dy <= r; ++dy) {
        const int dx = r == 0 ? 0
                              : static_cast<int>(std::lround(
                                    r * std::sqrt(std::max(0.0, (r * r - dy * dy) / double(r * r)))));
        for (int x = -dx; x <= dx; ++x) {
            element.emplace_back(x, dy);
        }
    }
    const auto apply = [&](const Grid& in, bool dilate) {
        Grid out(in.w, in.h);
        for (int y = 0; y < in.h; ++y) {
            for (int x = 0; x < in.w; ++x) {
                bool v = !dilate;
                for (const auto& [dx, dy] : element) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= in.w || ny >= in.h) {
                        continue; // Rand beeinflusst das Ergebnis nicht
                    }
                    const bool on = in.at(nx, ny) >= 0.5F;
                    v = dilate ? (v || on) : (v && on);
                }
                out.at(x, y) = v ? 1.0F : 0.0F;
            }
        }
        return out;
    };
    Grid out = *g;
    switch (op) {
    case ports::MorphOp::Erode:
        out = apply(out, false);
        break;
    case ports::MorphOp::Dilate:
        out = apply(out, true);
        break;
    case ports::MorphOp::Open:
        out = apply(apply(out, false), true);
        break;
    case ports::MorphOp::Close:
        out = apply(apply(out, true), false);
        break;
    }
    return imageOf(out);
}

} // namespace autoneg::mocks
