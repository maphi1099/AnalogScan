#include "autoneg/analysis/frame_detector.hpp"

#include "planes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>

namespace autoneg::analysis {

namespace {

using detail::Plane;

constexpr double degPerRad = 180.0 / std::numbers::pi;

// --- Schräglage ----------------------------------------------------------------------------

/// Log-Luminanz, normiert auf 0…1 (Kanten der Dichte statt der Helligkeit).
Plane logPlane(const Plane& lum)
{
    Plane out(lum.w, lum.h);
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        out.v[i] = std::log10(std::max(lum.v[i], 1e-4F));
        lo = std::min(lo, out.v[i]);
        hi = std::max(hi, out.v[i]);
    }
    const float span = std::max(hi - lo, 1e-6F);
    for (float& v : out.v) {
        v = (v - lo) / span;
    }
    return out;
}

/// Subpixelgenaue Kantenpunkte entlang einer fast waagerechten Linie y = y0 + k (x − x0);
/// liefert die angepasste Steigung oder nichts.
std::optional<double> refineSlope(const Plane& p, double x0, double y0, double k, double xa, double xb)
{
    std::vector<double> xs;
    std::vector<double> ys;
    const int a = std::max(2, static_cast<int>(std::ceil(std::min(xa, xb))));
    const int b = std::min(p.w - 3, static_cast<int>(std::floor(std::max(xa, xb))));
    // Vorzeichen der Kante aus der Mitte bestimmen
    for (int x = a; x <= b; x += 2) {
        const double yc = y0 + k * (x + 0.5 - x0);
        const int ylo = static_cast<int>(std::floor(yc)) - 4;
        const int yhi = static_cast<int>(std::floor(yc)) + 4;
        if (ylo < 1 || yhi >= p.h - 1) {
            continue;
        }
        int best = -1;
        float bestG = 0.0F;
        std::vector<float> g;
        for (int y = ylo; y <= yhi; ++y) {
            g.push_back(p.at(x, y + 1) - p.at(x, y - 1));
        }
        for (int i = 0; i < static_cast<int>(g.size()); ++i) {
            if (std::abs(g[static_cast<std::size_t>(i)]) > std::abs(bestG)) {
                bestG = g[static_cast<std::size_t>(i)];
                best = i;
            }
        }
        if (best <= 0 || best + 1 >= static_cast<int>(g.size()) || std::abs(bestG) < 0.02F) {
            continue;
        }
        // Parabel durch drei Beträge
        const double gm = std::abs(g[static_cast<std::size_t>(best - 1)]);
        const double g0 = std::abs(bestG);
        const double gp = std::abs(g[static_cast<std::size_t>(best + 1)]);
        const double den = gm - 2.0 * g0 + gp;
        const double off = std::abs(den) > 1e-12 ? 0.5 * (gm - gp) / den : 0.0;
        xs.push_back(x + 0.5);
        ys.push_back(ylo + best + 0.5 + std::clamp(off, -0.5, 0.5));
    }
    if (xs.size() < 10) {
        return std::nullopt;
    }
    // Ausgleichsgerade, zweimal mit Ausreißerausschluss (3 MAD)
    std::vector<bool> use(xs.size(), true);
    double slope = k;
    for (int pass = 0; pass < 3; ++pass) {
        double sx = 0;
        double sy = 0;
        double sxx = 0;
        double sxy = 0;
        int n = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            if (use[i]) {
                sx += xs[i];
                sy += ys[i];
                sxx += xs[i] * xs[i];
                sxy += xs[i] * ys[i];
                ++n;
            }
        }
        const double den = n * sxx - sx * sx;
        if (n < 10 || std::abs(den) < 1e-9) {
            return std::nullopt;
        }
        slope = (n * sxy - sx * sy) / den;
        const double icpt = (sy - slope * sx) / n;
        std::vector<float> res;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            res.push_back(static_cast<float>(std::abs(ys[i] - (icpt + slope * xs[i]))));
        }
        const double mad = std::max(1.4826 * detail::percentile(res, 0.5), 0.05);
        for (std::size_t i = 0; i < xs.size(); ++i) {
            use[i] = res[i] <= 3.0 * mad;
        }
    }
    return slope;
}

double weightedMedian(std::vector<std::pair<double, double>> v) // (Wert, Gewicht)
{
    std::ranges::sort(v);
    double total = 0.0;
    for (const auto& [x, w] : v) {
        total += w;
    }
    double acc = 0.0;
    for (const auto& [x, w] : v) {
        acc += w;
        if (acc >= total / 2.0) {
            return x;
        }
    }
    return v.empty() ? 0.0 : v.back().first;
}

// --- Bildfeld ------------------------------------------------------------------------------

struct Run
{
    int a = 0; ///< erstes Element
    int b = 0; ///< hinter dem letzten
};

/// Läufe mit profile[i] ≥ level.
std::vector<Run> runsAbove(const std::vector<double>& profile, double level)
{
    std::vector<Run> runs;
    int start = -1;
    for (int i = 0; i <= static_cast<int>(profile.size()); ++i) {
        const bool on = i < static_cast<int>(profile.size()) && profile[static_cast<std::size_t>(i)] >= level;
        if (on && start < 0) {
            start = i;
        } else if (!on && start >= 0) {
            runs.push_back({start, i});
            start = -1;
        }
    }
    return runs;
}

/// Kante zwischen Index i−1 (außen) und i (innen) auf halbem Pegel, als Pixelkoordinate.
double edgeAt(const std::vector<double>& p, int i, double level, bool rising)
{
    const int n = static_cast<int>(p.size());
    if (i <= 0 || i >= n) {
        return std::clamp(i, 0, n);
    }
    const double a = p[static_cast<std::size_t>(i - 1)];
    const double b = p[static_cast<std::size_t>(i)];
    if (std::abs(b - a) < 1e-9) {
        return i;
    }
    const double t = std::clamp((level - a) / (b - a), 0.0, 1.0);
    (void)rising;
    return (i - 0.5) + t;
}

/// Ergebnis der Erkennung in waagerechter Lage (Streifen liegt quer).
struct Layout
{
    int s0 = 0; ///< Filmstreifen: Zeilen [s0, s1)
    int s1 = 0;
    int t0 = 0; ///< Halter innerhalb des Laufs (entfernt): Zeilen [t0, s0) und [s1, t1)
    int t1 = 0;
    double top = 0.0; ///< Bildfeld: Zeilen (Subpixel)
    double bottom = 0.0;
    std::vector<std::pair<double, double>> cols; ///< Bildfelder: Spalten (Subpixel)
    double confidence = 0.0;
};

struct Planes
{
    Plane t;       ///< Transmission relativ zum Lichtpegel (NaN außerhalb)
    Plane light;   ///< 1 = Leuchtfläche/Perforation
    Plane zone;    ///< Licht mit Saum 4 px (Randabfall der Löcher), nur für Basis und Rauschen
    Plane exposed; ///< 1 = belichtet (Bildinhalt)
};

base::Expected<Layout> findLayout(const Planes& p, model::FilmType film)
{
    const int w = p.t.w;
    const int h = p.t.h;
    // Zeilen: Anteil Licht und Anteil dunkel (Halter) je Zeile
    std::vector<double> lightRow(static_cast<std::size_t>(h), 0.0);
    std::vector<double> darkRow(static_cast<std::size_t>(h), 0.0);
    std::vector<double> textureRow(static_cast<std::size_t>(h), 0.0);
    for (int y = 0; y < h; ++y) {
        int valid = 0;
        int lit = 0;
        int dark = 0;
        double s = 0.0;
        double s2 = 0.0;
        int n = 0;
        for (int x = 0; x < w; ++x) {
            const float t = p.t.at(x, y);
            if (!std::isfinite(t)) {
                continue;
            }
            ++valid;
            if (p.light.at(x, y) > 0.5F) {
                ++lit;
                continue;
            }
            dark += t < 0.03F ? 1 : 0;
            const double l = std::log10(std::max(t, 1e-4F));
            s += l;
            s2 += l * l;
            ++n;
        }
        const auto i = static_cast<std::size_t>(y);
        lightRow[i] = valid > 0 ? static_cast<double>(lit) / valid : 1.0;
        darkRow[i] = n > 0 ? static_cast<double>(dark) / n : 0.0;
        textureRow[i] = n > 1 ? std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n))) * n / w : 0.0;
    }
    // Filmstreifen: Lauf ohne Licht mit der meisten Struktur. Zeilen mit Perforation sind etwa
    // zur Hälfte Licht und gehören zum Streifen; Leuchtfläche erst ab 80 % Licht.
    std::vector<double> notLit(lightRow.size());
    std::ranges::transform(lightRow, notLit.begin(), [](double v) { return 1.0 - v; });
    const auto stripRuns = runsAbove(notLit, 0.2);
    if (stripRuns.empty()) {
        return base::makeError(base::ErrorCode::NotFound, "Kein Filmstreifen erkennbar (nur Leuchtfläche)");
    }
    Run strip = stripRuns.front();
    double bestScore = -1.0;
    for (const Run& r : stripRuns) {
        double score = 0.0;
        for (int y = r.a; y < r.b; ++y) {
            score += textureRow[static_cast<std::size_t>(y)];
        }
        if (score > bestScore) {
            bestScore = score;
            strip = r;
        }
    }
    Layout l;
    l.t0 = strip.a;
    l.t1 = strip.b;
    // Halter am Rand des Laufs abschneiden (gleichmäßig dunkel)
    while (strip.a < strip.b && darkRow[static_cast<std::size_t>(strip.a)] >= 0.8) {
        ++strip.a;
    }
    while (strip.b > strip.a && darkRow[static_cast<std::size_t>(strip.b - 1)] >= 0.8) {
        --strip.b;
    }
    if (strip.b - strip.a < std::max(8, h / 20)) {
        return base::makeError(base::ErrorCode::NotFound, "Kein Filmstreifen erkennbar (zu schmal)");
    }
    l.s0 = strip.a;
    l.s1 = strip.b;

    // Zeilenprofil der Belichtung im Streifen
    std::vector<double> rowExp(static_cast<std::size_t>(h), 0.0);
    for (int y = l.s0; y < l.s1; ++y) {
        int n = 0;
        int e = 0;
        for (int x = 0; x < w; ++x) {
            if (std::isfinite(p.t.at(x, y)) && p.light.at(x, y) < 0.5F) {
                ++n;
                e += p.exposed.at(x, y) > 0.5F ? 1 : 0;
            }
        }
        rowExp[static_cast<std::size_t>(y)] = n > 0 ? static_cast<double>(e) / n : 0.0;
    }
    const double maxRow = *std::max_element(rowExp.begin() + l.s0, rowExp.begin() + l.s1);
    if (maxRow < 0.2) {
        return base::makeError(base::ErrorCode::NotFound, "Kein belichtetes Bildfeld im Filmstreifen");
    }
    std::vector<Run> rowRuns;
    for (Run r : runsAbove(rowExp, 0.5 * maxRow)) {
        r.a = std::max(r.a, l.s0);
        r.b = std::min(r.b, l.s1);
        if (r.b > r.a) {
            rowRuns.push_back(r);
        }
    }
    const Run rows = *std::ranges::max_element(rowRuns, {}, [](const Run& r) { return r.b - r.a; });
    // Pegel innen und außen (Filmrand im Streifen)
    std::vector<float> inside;
    std::vector<float> outside;
    for (int y = l.s0; y < l.s1; ++y) {
        (y >= rows.a && y < rows.b ? inside : outside)
            .push_back(static_cast<float>(rowExp[static_cast<std::size_t>(y)]));
    }
    const double pin = detail::percentile(inside, 0.5);
    const double pout = outside.empty() ? 0.0 : detail::percentile(outside, 0.5);
    const double rowLevel = (pin + pout) / 2.0;
    l.top = rows.a > l.s0 ? edgeAt(rowExp, rows.a, rowLevel, true) : rows.a;
    l.bottom = rows.b < l.s1 ? edgeAt(rowExp, rows.b, rowLevel, false) : rows.b;

    // Spaltenprofil in den Bildzeilen
    const int ya = static_cast<int>(std::ceil(l.top));
    const int yb = static_cast<int>(std::floor(l.bottom));
    std::vector<double> colExp(static_cast<std::size_t>(w), 0.0);
    for (int x = 0; x < w; ++x) {
        int n = 0;
        int e = 0;
        for (int y = ya; y < yb; ++y) {
            if (std::isfinite(p.t.at(x, y)) && p.light.at(x, y) < 0.5F) {
                ++n;
                e += p.exposed.at(x, y) > 0.5F ? 1 : 0;
            }
        }
        colExp[static_cast<std::size_t>(x)] = n > 0 ? static_cast<double>(e) / n : 0.0;
    }
    const double maxCol = *std::ranges::max_element(colExp);
    std::vector<Run> colRuns;
    const int minWidth = std::max(4, (yb - ya) / 4);
    for (const Run& r : runsAbove(colExp, 0.5 * maxCol)) {
        if (r.b - r.a >= minWidth) {
            colRuns.push_back(r);
        }
    }
    if (colRuns.empty()) {
        return base::makeError(base::ErrorCode::NotFound, "Kein Bildfeld erkennbar");
    }
    std::vector<float> cin;
    std::vector<float> cout;
    for (int x = 0; x < w; ++x) {
        const bool in = std::ranges::any_of(colRuns, [x](const Run& r) { return x >= r.a && x < r.b; });
        (in ? cin : cout).push_back(static_cast<float>(colExp[static_cast<std::size_t>(x)]));
    }
    const double qin = detail::percentile(cin, 0.5);
    const double qout = cout.empty() ? 0.0 : detail::percentile(cout, 0.5);
    const double colLevel = (qin + qout) / 2.0;
    for (const Run& r : colRuns) {
        const double left = r.a > 0 ? edgeAt(colExp, r.a, colLevel, true) : 0.0;
        const double right = r.b < w ? edgeAt(colExp, r.b, colLevel, false) : w;
        l.cols.emplace_back(left, right);
    }
    // Ohne jede Grenze (Streifen, Filmrand, Steg) ist kein Bildfeld belegt (LF-GEO-05)
    const bool rowBoundary = l.s0 > 0 || l.s1 < h || rows.a > l.s0 || rows.b < l.s1;
    const bool colBoundary = colRuns.size() > 1 || colRuns.front().a > 0 || colRuns.front().b < w;
    if (!rowBoundary && !colBoundary) {
        return base::makeError(base::ErrorCode::NotFound,
                               "Kein Bildfeld erkennbar (keine Begrenzung gefunden)");
    }
    l.confidence = std::clamp(std::min(rowBoundary ? pin - pout : 1.0, qin - qout), 0.0, 1.0);
    (void)film;
    return l;
}

/// Lokale Streuung von log10 T im Fenster (2r+1)² (Summentabellen); NaN, wo das Fenster
/// ungültige Werte enthält.
Plane localStd(const Plane& t, int r)
{
    const int w = t.w;
    const int h = t.h;
    std::vector<double> s1(static_cast<std::size_t>(w + 1) * static_cast<std::size_t>(h + 1), 0.0);
    std::vector<double> s2(s1.size(), 0.0);
    std::vector<int> bad(s1.size(), 0);
    const auto idx = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w + 1) + static_cast<std::size_t>(x);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = t.at(x, y);
            const bool ok = std::isfinite(v);
            const double l = ok ? std::log10(std::max(v, 1e-6F)) : 0.0;
            s1[idx(x + 1, y + 1)] = s1[idx(x, y + 1)] + s1[idx(x + 1, y)] - s1[idx(x, y)] + l;
            s2[idx(x + 1, y + 1)] = s2[idx(x, y + 1)] + s2[idx(x + 1, y)] - s2[idx(x, y)] + l * l;
            bad[idx(x + 1, y + 1)] = bad[idx(x, y + 1)] + bad[idx(x + 1, y)] - bad[idx(x, y)] + (ok ? 0 : 1);
        }
    }
    Plane out(w, h, std::numeric_limits<float>::quiet_NaN());
    for (int y = r; y < h - r; ++y) {
        for (int x = r; x < w - r; ++x) {
            const int x0 = x - r;
            const int y0 = y - r;
            const int x1 = x + r + 1;
            const int y1 = y + r + 1;
            if (bad[idx(x1, y1)] - bad[idx(x0, y1)] - bad[idx(x1, y0)] + bad[idx(x0, y0)] > 0) {
                continue;
            }
            const double n = static_cast<double>((2 * r + 1) * (2 * r + 1));
            const double a = (s1[idx(x1, y1)] - s1[idx(x0, y1)] - s1[idx(x1, y0)] + s1[idx(x0, y0)]) / n;
            const double b = (s2[idx(x1, y1)] - s2[idx(x0, y1)] - s2[idx(x1, y0)] + s2[idx(x0, y0)]) / n;
            out.at(x, y) = static_cast<float>(std::sqrt(std::max(0.0, b - a * a)));
        }
    }
    return out;
}

/// Basis eines Negativs im Scan: der unbelichtete Filmrand ist die hellste große, schmale
/// Population (Spitze im Histogramm von log10 T); Rückfall auf das 90-%-Perzentil.
struct BaseLevel
{
    double logT = 0.0;  ///< log10 der Basis-Transmission relativ zum Lichtpegel
    double sigma = 0.0; ///< Streuung der Spitze (log10)
};

BaseLevel baseLevel(const Planes& p)
{
    constexpr double binWidth = 0.005;
    constexpr double lo = -4.0;
    std::vector<double> hist(static_cast<std::size_t>(-lo / binWidth) + 1, 0.0);
    std::vector<float> values;
    for (std::size_t i = 0; i < p.t.v.size(); ++i) {
        const float t = p.t.v[i];
        if (std::isfinite(t) && t > 1e-4F && p.zone.v[i] < 0.5F) {
            const double l = std::log10(t);
            values.push_back(static_cast<float>(l));
            const auto b = static_cast<std::size_t>(
                std::clamp((l - lo) / binWidth, 0.0, static_cast<double>(hist.size() - 1)));
            hist[b] += 1.0;
        }
    }
    if (values.empty()) {
        return {0.0, 0.0};
    }
    // glätten (±2 Klassen)
    std::vector<double> smooth(hist.size(), 0.0);
    for (std::size_t i = 0; i < hist.size(); ++i) {
        for (int d = -2; d <= 2; ++d) {
            const auto j = static_cast<std::ptrdiff_t>(i) + d;
            if (j >= 0 && j < static_cast<std::ptrdiff_t>(hist.size())) {
                smooth[i] += hist[static_cast<std::size_t>(j)];
            }
        }
    }
    const double top = *std::ranges::max_element(smooth);
    // von hell nach dunkel: erste lokale Spitze mit mindestens 25 % der höchsten
    for (std::size_t i = smooth.size() - 2; i > 1; --i) {
        if (smooth[i] >= 0.25 * top && smooth[i] >= smooth[i - 1] && smooth[i] >= smooth[i + 1]) {
            // Halbwertsbreite nach unten (dunkle Seite enthält das Bild, helle Seite nur Rauschen)
            std::size_t k = i;
            while (k + 1 < smooth.size() && smooth[k + 1] > smooth[i] / 2.0) {
                ++k;
            }
            const double halfWidth = static_cast<double>(k - i) * binWidth + binWidth / 2.0;
            return {lo + (static_cast<double>(i) + 0.5) * binWidth, halfWidth / 1.1774}; // HWHM -> sigma
        }
    }
    return {detail::percentile(values, 0.90), 0.02};
}

Planes makePlanes(const Plane& lum, model::FilmType film, ports::IVision& vision)
{
    Planes p;
    std::vector<float> valid;
    for (const float v : lum.v) {
        if (std::isfinite(v)) {
            valid.push_back(v);
        }
    }
    const double lightLevel = std::max(detail::percentile(valid, 0.998), 1e-9);
    p.t = Plane(lum.w, lum.h);
    Plane lightMask(lum.w, lum.h);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        p.t.v[i] = static_cast<float>(lum.v[i] / lightLevel);
        lightMask.v[i] = p.t.v[i] >= 0.9F ? 1.0F : 0.0F;
    }
    // Einzelne helle Pixel (Rauschen, Staublücken) zählen nicht als Leuchtfläche; der Randabfall
    // um Perforationslöcher (Vignettierung, knapp unter der Schwelle) gehört dazu (Saum 4 px)
    auto opened = vision.morphology(detail::toImage(lightMask), ports::MorphOp::Open, 1);
    p.light = opened ? detail::fromImage(*opened) : lightMask;
    auto zone = vision.morphology(detail::toImage(p.light), ports::MorphOp::Dilate, 4);
    p.zone = zone ? detail::fromImage(*zone) : p.light;

    // Belichtet (1): Helligkeit - Negativ über der Basis, Dia über dem schwarzen Filmrand
    const BaseLevel base = baseLevel(p);
    const double limit = base.logT - std::max(0.03, 3.0 * base.sigma); // log10 T
    p.exposed = Plane(lum.w, lum.h);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        const float t = p.t.v[i];
        bool e = false;
        if (std::isfinite(t)) {
            e = film == model::FilmType::Slide ? t > 0.015F : std::log10(std::max(t, 1e-6F)) < limit;
        }
        p.exposed.v[i] = e ? 1.0F : 0.0F;
    }
    // Belichtet (2): Struktur - Bildinhalt hat Zeichnung, der Filmrand ist glatt. Unterbelichtete
    // Negativstellen und dunkle Diastellen liegen nahe am Pegel des Filmrands, haben aber Struktur.
    const Plane texture = localStd(p.t, 3);
    std::vector<float> plain;
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        if (std::isfinite(texture.v[i]) && p.exposed.v[i] < 0.5F && p.zone.v[i] < 0.5F) {
            plain.push_back(texture.v[i]);
        }
    }
    const double noise = plain.empty() ? 0.0 : detail::percentile(plain, 0.5);
    const auto tau = static_cast<float>(std::max(0.02, 3.0 * noise));
    Plane textured(lum.w, lum.h);
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        textured.v[i] = std::isfinite(texture.v[i]) && texture.v[i] > tau ? 1.0F : 0.0F;
    }
    // Das Fenster sieht eine Kante schon 3 px vorher: um den Fensterradius zusammenziehen
    auto shrunk = vision.morphology(detail::toImage(textured), ports::MorphOp::Erode, 3);
    if (shrunk) {
        textured = detail::fromImage(*shrunk);
    }
    for (std::size_t i = 0; i < lum.v.size(); ++i) {
        if (textured.v[i] > 0.5F) {
            p.exposed.v[i] = 1.0F;
        }
    }
    return p;
}

Planes transposed(const Planes& p)
{
    return {detail::transpose(p.t), detail::transpose(p.light), detail::transpose(p.zone),
            detail::transpose(p.exposed)};
}

RegionMap regionsOf(const Planes& p, const Layout& l, bool vertical, int w, int h, double skewDeg)
{
    // l und p in waagerechter Lage; Ergebnis in Bildlage (w × h)
    const int lw = p.t.w;
    const int lh = p.t.h;
    std::vector<RegionClass> labels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                                    RegionClass::Background);
    for (int y = 0; y < lh; ++y) {
        for (int x = 0; x < lw; ++x) {
            RegionClass c = RegionClass::Background;
            const float t = p.t.at(x, y);
            if (std::isfinite(t) && p.light.at(x, y) < 0.5F) {
                if (y < l.s0 || y >= l.s1) {
                    c = RegionClass::Holder;
                } else {
                    const bool inRows = y + 0.5 >= l.top && y + 0.5 < l.bottom;
                    const bool inCols = std::ranges::any_of(
                        l.cols, [x](const auto& c2) { return x + 0.5 >= c2.first && x + 0.5 < c2.second; });
                    c = inRows && inCols ? RegionClass::Image : RegionClass::Rebate;
                }
            }
            const int ox = vertical ? y : x;
            const int oy = vertical ? x : y;
            labels[static_cast<std::size_t>(oy) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(ox)] = c;
        }
    }
    // Filmrand-Bänder (normiert)
    std::vector<model::NormRect> rebate;
    const auto add = [&](double y0, double y1) {
        if (y1 - y0 >= 1.0) {
            model::NormRect r{0.0, y0 / lh, 1.0, (y1 - y0) / lh};
            rebate.push_back(vertical ? model::NormRect{r.y, r.x, r.h, r.w} : r);
        }
    };
    add(l.s0, l.top);
    add(l.bottom, l.s1);
    return RegionMap(w, h, std::move(labels), skewDeg, std::move(rebate));
}

} // namespace

base::Expected<double> SkewEstimator::estimate(const image::ImageBuffer& img) const
{
    if (img.empty() || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Schräglage ohne Bild");
    }
    const Plane lp = logPlane(detail::luminance(img));
    const auto edges = vision_.edges(detail::toImage(lp), 1.5F);
    if (!edges) {
        return std::unexpected(edges.error());
    }
    const auto lines = vision_.lines(*edges);
    if (!lines) {
        return std::unexpected(lines.error());
    }
    // auch kurze Strecken (Perforationslöcher): viele parallele Kanten tragen den Konsens
    const double minLen = std::max(20.0, 0.03 * std::min(lp.w, lp.h));
    struct Cand
    {
        ports::Line line;
        double skew; ///< Schräglage in Grad (Film gegenüber dem Scan)
        double len;
        bool horizontal;
    };
    std::vector<Cand> cands;
    for (const ports::Line& ln : *lines) {
        const double dx = ln.b.x - ln.a.x;
        const double dy = ln.b.y - ln.a.y;
        const double len = std::hypot(dx, dy);
        if (len < minLen) {
            continue;
        }
        double a = std::atan2(dy, dx) * degPerRad; // (−180, 180]
        if (a > 90.0) {
            a -= 180.0;
        } else if (a <= -90.0) {
            a += 180.0;
        }
        if (std::abs(a) <= 10.0) {
            cands.push_back({ln, a, len, true});
        } else if (std::abs(a) >= 80.0) {
            cands.push_back({ln, a > 0 ? a - 90.0 : a + 90.0, len, false});
        }
    }
    if (cands.empty()) {
        return base::makeError(base::ErrorCode::NotFound, "Keine geraden Kanten für die Schräglage gefunden");
    }
    // Grobwinkel als Konsens: Streifenkanten, Bildfeldkanten und Perforation liegen parallel,
    // gerade Motivlinien (Straßen, Gebäude) streuen. Gewählt wird der Winkel, auf den sich die
    // meiste Kantenlänge (±0,3°) vereinigt; bei Gleichstand der kleinere Betrag.
    double skew0 = 0.0;
    double bestSupport = -1.0;
    for (const Cand& c : cands) {
        double support = 0.0;
        for (const Cand& o : cands) {
            support += std::abs(o.skew - c.skew) <= 0.3 ? o.len : 0.0;
        }
        if (support > bestSupport + 1e-9 ||
            (std::abs(support - bestSupport) <= 1e-9 && std::abs(c.skew) < std::abs(skew0))) {
            bestSupport = support;
            skew0 = c.skew;
        }
    }

    // Feinwinkel an den längsten passenden Kanten
    std::ranges::sort(cands, [](const Cand& a, const Cand& b) { return a.len > b.len; });
    const Plane lpt = detail::transpose(lp);
    std::vector<std::pair<double, double>> fine;
    int used = 0;
    for (const Cand& c : cands) {
        if (used >= 6 || std::abs(c.skew - skew0) > 0.5) {
            continue;
        }
        const double k = std::tan(c.skew / degPerRad);
        std::optional<double> slope;
        if (c.horizontal) {
            const double mx = (c.line.a.x + c.line.b.x) / 2.0;
            const double my = (c.line.a.y + c.line.b.y) / 2.0;
            slope = refineSlope(lp, mx, my, k, c.line.a.x, c.line.b.x);
            if (slope) {
                fine.emplace_back(std::atan(*slope) * degPerRad, c.len);
            }
        } else {
            // senkrechte Kante x = x0 + m (y − y0) mit m = −tan(Schräglage); im transponierten Bild
            // waagerecht
            const double mx = (c.line.a.y + c.line.b.y) / 2.0;
            const double my = (c.line.a.x + c.line.b.x) / 2.0;
            slope = refineSlope(lpt, mx, my, -k, c.line.a.y, c.line.b.y);
            if (slope) {
                fine.emplace_back(-std::atan(*slope) * degPerRad, c.len);
            }
        }
        used += slope ? 1 : 0;
    }
    const double skew = fine.empty() ? skew0 : weightedMedian(fine);
    return -skew; // Korrekturwinkel für autoneg.rotation
}

base::Expected<std::vector<FrameDetection>> FrameDetector::detectAll(const image::ImageBuffer& img,
                                                                     model::FilmType film) const
{
    if (img.empty() || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Bildfelderkennung ohne Bild");
    }
    const auto correction = SkewEstimator(vision_).estimate(img);
    const double skew = correction ? -*correction : 0.0;
    const Plane lum = detail::deskew(detail::luminance(img), std::abs(skew) < 0.01 ? 0.0 : skew);
    Planes p = makePlanes(lum, film, vision_);

    // Lage des Streifens: waagerecht, wenn mehr Zeilen als Spalten überwiegend Licht sind
    int litRows = 0;
    int litCols = 0;
    for (int y = 0; y < p.t.h; ++y) {
        int n = 0;
        for (int x = 0; x < p.t.w; ++x) {
            n += p.light.at(x, y) > 0.5F ? 1 : 0;
        }
        litRows += n * 2 > p.t.w ? 1 : 0;
    }
    for (int x = 0; x < p.t.w; ++x) {
        int n = 0;
        for (int y = 0; y < p.t.h; ++y) {
            n += p.light.at(x, y) > 0.5F ? 1 : 0;
        }
        litCols += n * 2 > p.t.h ? 1 : 0;
    }
    const bool vertical = litCols > litRows || (litCols == litRows && p.t.h > p.t.w);
    const Planes q = vertical ? transposed(p) : std::move(p);
    const auto layout = findLayout(q, film);
    if (!layout) {
        return std::unexpected(layout.error());
    }
    const int w = img.spec().width;
    const int h = img.spec().height;
    const RegionMap regions = regionsOf(q, *layout, vertical, w, h, std::abs(skew) < 0.01 ? 0.0 : skew);
    std::vector<FrameDetection> out;
    const double lw = q.t.w;
    const double lh = q.t.h;
    for (const auto& [left, right] : layout->cols) {
        model::NormRect r{left / lw, layout->top / lh, (right - left) / lw,
                          (layout->bottom - layout->top) / lh};
        if (vertical) {
            r = {r.y, r.x, r.h, r.w};
        }
        FrameDetection d;
        d.frame = r;
        d.angleDeg = correction ? *correction : 0.0;
        d.regions = regions;
        d.confidence = layout->confidence * (correction ? 1.0 : 0.8);
        out.push_back(std::move(d));
    }
    return out;
}

base::Expected<FrameDetection> FrameDetector::detect(const image::ImageBuffer& img,
                                                     model::FilmType film) const
{
    auto all = detectAll(img, film);
    if (!all) {
        return std::unexpected(all.error());
    }
    // größtes Bildfeld
    return *std::ranges::max_element(*all, {}, [](const FrameDetection& d) { return d.frame.w * d.frame.h; });
}

} // namespace autoneg::analysis
