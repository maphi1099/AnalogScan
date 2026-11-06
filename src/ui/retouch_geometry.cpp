#include "ui/retouch_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace autoneg::ui {

RetouchGeometry::RetouchGeometry(double aspect)
    : aspect_(aspect > 0.0 ? aspect : 1.0)
    , gh_(std::max(1, static_cast<int>(std::lround(gridWidth * aspect_))))
{}

void RetouchGeometry::stamp(std::vector<std::uint8_t>& m, double cx, double cy, double r, std::uint8_t v) const
{
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - r)));
    const int x1 = std::min(gridWidth - 1, static_cast<int>(std::ceil(cx + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - r)));
    const int y1 = std::min(gh_ - 1, static_cast<int>(std::ceil(cy + r)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            if (dx * dx + dy * dy <= r * r) {
                m[static_cast<std::size_t>(y) * gridWidth + static_cast<std::size_t>(x)] = v;
            }
        }
    }
}

void RetouchGeometry::fill(std::vector<std::uint8_t>& m, int x, int y, std::uint8_t mark) const
{
    std::vector<std::pair<int, int>> todo{{x, y}};
    while (!todo.empty()) {
        const auto [cx, cy] = todo.back();
        todo.pop_back();
        if (cx < 0 || cy < 0 || cx >= gridWidth || cy >= gh_) {
            continue;
        }
        std::uint8_t& v = m[static_cast<std::size_t>(cy) * gridWidth + static_cast<std::size_t>(cx)];
        if (v != 1) {
            continue;
        }
        v = mark;
        todo.insert(todo.end(), {{cx + 1, cy}, {cx - 1, cy}, {cx, cy + 1}, {cx, cy - 1}});
    }
}

std::vector<std::uint8_t> RetouchGeometry::raster(const model::RetouchShape& s) const
{
    std::vector<std::uint8_t> m(static_cast<std::size_t>(gridWidth) * static_cast<std::size_t>(gh_), 0);
    const auto gx = [](double x) { return x * gridWidth; };
    const auto gy = [this](double y) { return y * gh_; };
    if (s.kind == model::RetouchShape::Kind::Circle) {
        stamp(m, gx(s.center.x), gy(s.center.y), std::max(0.5, s.radius * gridWidth), 1);
        return m;
    }
    for (const model::RetouchStroke& st : s.strokes) {
        const double r = std::max(0.5, st.width * gridWidth / 2.0);
        const std::uint8_t v = st.erase ? 0 : 1;
        for (std::size_t i = 0; i < st.points.size(); ++i) {
            const model::NormPoint& b = st.points[i];
            const model::NormPoint& a = i > 0 ? st.points[i - 1] : b;
            const double len = std::hypot(gx(b.x) - gx(a.x), gy(b.y) - gy(a.y));
            const int steps = std::max(1, static_cast<int>(std::ceil(len / 0.5)));
            for (int k = 0; k <= steps; ++k) {
                const double t = static_cast<double>(k) / steps;
                stamp(m, gx(a.x + t * (b.x - a.x)), gy(a.y + t * (b.y - a.y)), r, v);
            }
        }
    }
    // nur das Stück um den inneren Punkt (nach dem Teilen durch den Radierer)
    const int sx = static_cast<int>(gx(s.center.x));
    const int sy = static_cast<int>(gy(s.center.y));
    if (sx >= 0 && sy >= 0 && sx < gridWidth && sy < gh_ &&
        m[static_cast<std::size_t>(sy) * gridWidth + static_cast<std::size_t>(sx)] == 1) {
        fill(m, sx, sy, 2);
        for (std::uint8_t& v : m) {
            v = v == 2 ? 1 : 0;
        }
    }
    return m;
}

model::RetouchShapes RetouchGeometry::addStroke(model::RetouchShapes shapes, const model::RetouchStroke& stroke,
                                                const std::function<std::string()>& newName) const
{
    if (stroke.points.empty()) {
        return shapes;
    }
    if (!stroke.erase) {
        model::RetouchShape s;
        s.kind = model::RetouchShape::Kind::Brush;
        s.center = stroke.points.front();
        s.strokes.push_back(stroke);
        shapes.push_back(std::move(s)); // neu: zuletzt; regroup verbindet mit Berührten
        return regroup(shapes, newName);
    }
    // Radierer: auf jede Fläche, die er berührt (Kreise bleiben)
    model::RetouchShape probe;
    probe.kind = model::RetouchShape::Kind::Brush;
    model::RetouchStroke solid = stroke;
    solid.erase = false;
    probe.strokes.push_back(solid);
    probe.center = {-1.0, -1.0};
    const auto eraser = raster(probe);
    for (model::RetouchShape& s : shapes) {
        if (s.kind != model::RetouchShape::Kind::Brush) {
            continue;
        }
        const auto m = raster(s);
        bool touches = false;
        for (std::size_t i = 0; i < m.size() && !touches; ++i) {
            touches = m[i] != 0 && eraser[i] != 0;
        }
        if (touches) {
            s.strokes.push_back(stroke);
            s.center = {-1.0, -1.0}; // ganze Fläche neu aufteilen (der Radierer kann sie trennen)
        }
    }
    return regroup(shapes, newName);
}

model::RetouchShapes RetouchGeometry::regroup(const model::RetouchShapes& shapes,
                                              const std::function<std::string()>& newName) const
{
    // Masken der Flächen und ihre Vereinigung; Komponenten der Vereinigung = neue Flächen
    std::vector<std::vector<std::uint8_t>> masks(shapes.size());
    std::vector<std::uint8_t> all(static_cast<std::size_t>(gridWidth) * static_cast<std::size_t>(gh_), 0);
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (shapes[i].kind != model::RetouchShape::Kind::Brush) {
            continue;
        }
        masks[i] = raster(shapes[i]);
        for (std::size_t k = 0; k < all.size(); ++k) {
            all[k] = static_cast<std::uint8_t>(all[k] | masks[i][k]);
        }
    }
    struct Group
    {
        std::size_t first = 0; ///< früheste beteiligte Form (Reihenfolge, Name)
        std::vector<std::size_t> members;
        model::NormPoint seed;
    };
    std::vector<Group> groups;
    constexpr std::uint8_t mark = 2; // aktuelle Komponente; erledigte bekommen 3
    for (int y = 0; y < gh_; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            if (all[static_cast<std::size_t>(y) * gridWidth + static_cast<std::size_t>(x)] != 1) {
                continue;
            }
            fill(all, x, y, mark);
            Group g;
            g.seed = {(x + 0.5) / gridWidth, (y + 0.5) / gh_};
            for (std::size_t i = 0; i < shapes.size(); ++i) {
                if (masks[i].empty()) {
                    continue;
                }
                for (std::size_t k = 0; k < all.size(); ++k) {
                    if (all[k] == mark && masks[i][k] != 0) {
                        g.members.push_back(i);
                        break;
                    }
                }
            }
            if (!g.members.empty()) {
                g.first = g.members.front();
                groups.push_back(std::move(g));
            }
            for (std::uint8_t& v : all) {
                if (v == mark) {
                    v = 3;
                }
            }
        }
    }
    // Ergebnis: Kreise und Flächen in der Reihenfolge ihres ersten Auftretens
    model::RetouchShapes out;
    std::vector<std::string> usedNames;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (shapes[i].kind == model::RetouchShape::Kind::Circle) {
            out.push_back(shapes[i]);
            continue;
        }
        for (const Group& g : groups) {
            if (g.first != i) {
                continue;
            }
            model::RetouchShape s;
            s.kind = model::RetouchShape::Kind::Brush;
            s.center = g.seed;
            s.enabled = shapes[i].enabled;
            for (const std::size_t m : g.members) {
                s.strokes.insert(s.strokes.end(), shapes[m].strokes.begin(), shapes[m].strokes.end());
            }
            // Name der frühesten Form; schon vergeben (geteilte Fläche) oder leer: neuer Name
            s.name = shapes[i].name;
            const auto taken = [&](const std::string& n) {
                if (std::ranges::find(usedNames, n) != usedNames.end()) {
                    return true;
                }
                // Name einer anderen Form, die noch folgt
                return std::ranges::any_of(shapes, [&](const model::RetouchShape& o) { return &o != &shapes[i] && o.name == n; });
            };
            if (s.name.empty() || std::ranges::find(usedNames, s.name) != usedNames.end()) {
                do {
                    s.name = newName();
                } while (taken(s.name));
            }
            usedNames.push_back(s.name);
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::optional<std::size_t> RetouchGeometry::hit(const model::RetouchShapes& shapes, model::NormPoint p) const
{
    for (std::size_t i = shapes.size(); i-- > 0;) {
        const model::RetouchShape& s = shapes[i];
        if (s.kind == model::RetouchShape::Kind::Circle) {
            const double dx = p.x - s.center.x;
            const double dy = (p.y - s.center.y) * aspect_;
            if (dx * dx + dy * dy <= s.radius * s.radius) {
                return i;
            }
            continue;
        }
        const int x = static_cast<int>(p.x * gridWidth);
        const int y = static_cast<int>(p.y * gh_);
        if (x >= 0 && y >= 0 && x < gridWidth && y < gh_ &&
            raster(s)[static_cast<std::size_t>(y) * gridWidth + static_cast<std::size_t>(x)] != 0) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace autoneg::ui
