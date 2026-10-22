#include "autoneg/steps/multigrade.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace autoneg::steps {

namespace {

constexpr double outputRange = 2.0; ///< R der Tonwertgrenzen (DensityRangeStep)
constexpr double isoLow = 0.04;     ///< ISO 6846: Dichte über dMin am Fuß
constexpr double isoHigh = 0.9;     ///< ISO 6846: Anteil der Dichtespanne an der Schulter

double normalizedDensity(double v)
{
    const double floor = std::pow(10.0, -outputRange);
    return 1.0 + std::log10(std::max(v, 0.0) * (1.0 - floor) + floor) / outputRange;
}

double slopeForRange(double isoR, double nu)
{
    const double span = MultigradeModel::paperDMax - MultigradeModel::paperDMin;
    const double y1 = isoLow / span;
    const double y2 = isoHigh;
    return (std::log(std::pow(y1, -1.0 / nu) - 1.0) - std::log(std::pow(y2, -1.0 / nu) - 1.0)) /
           (isoR / 100.0);
}

} // namespace

PaperCurve::PaperCurve(double dMin, double dMax, std::vector<Component> components)
    : dMin_(dMin)
    , dMax_(dMax)
    , components_(std::move(components))
{}

double PaperCurve::density(double logE) const
{
    double sum = 0.0;
    double weights = 0.0;
    for (const Component& c : components_) {
        sum += c.weight * std::pow(1.0 + std::exp(-c.a * (logE - c.x0)), -c.nu);
        weights += c.weight;
    }
    return dMin_ + (dMax_ - dMin_) * (weights > 0.0 ? sum / weights : 0.0);
}

double PaperCurve::gamma(double logE) const
{
    constexpr double h = 1e-4;
    return (density(logE + h) - density(logE - h)) / (2.0 * h);
}

double PaperCurve::isoRange() const
{
    const auto solve = [this](double target) {
        double lo = -20.0;
        double hi = 20.0;
        for (int i = 0; i < 200; ++i) {
            const double mid = 0.5 * (lo + hi);
            (density(mid) < target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    };
    const double x1 = solve(dMin_ + isoLow);
    const double x2 = solve(dMin_ + isoHigh * (dMax_ - dMin_));
    return 100.0 * (x2 - x1);
}

image::Lut1D PaperCurve::toLut(int n) const
{
    image::Lut1D lut;
    lut.inMin = 0.0F;
    lut.inMax = 1.0F;
    lut.values.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double v = static_cast<double>(i) / (n - 1);
        const double d = density(paperExposure(v));
        lut.values[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(10.0, -(d - dMin_)));
    }
    return lut;
}

double paperExposure(double v)
{
    // K einmalig aus der kompensierten Stufe 2 bestimmen (unveränderlich nach der Berechnung)
    static const double k = [] {
        const MultigradeModel model;
        const PaperCurve two = model.curve(2.0, true);
        // Belichtung des Papierweißes (dMin + 0,04) auf Stufe 2; liegt links vom Mittelton (x < 0)
        double lo = -20.0;
        double hi = 0.0;
        for (int i = 0; i < 200; ++i) {
            const double mid = 0.5 * (lo + hi);
            (two.density(mid) < MultigradeModel::paperDMin + isoLow ? lo : hi) = mid;
        }
        const double xWhite = 0.5 * (lo + hi);
        return -xWhite / (1.0 - normalizedDensity(0.18));
    }();
    return k * (normalizedDensity(0.18) - normalizedDensity(v));
}

MultigradeModel::MultigradeModel()
    : grades_{{
          {-1.0, 180, 1.0, 1.0, 1.0}, // 00
          {0.0, 160, 1.0, 1.0, 1.0},
          {0.5, 145, 1.0, 1.0, 1.0}, // interpoliert
          {1.0, 130, 1.0, 1.0, 1.0},
          {1.5, 120, 1.0, 1.0, 1.0}, // interpoliert
          {2.0, 110, 1.0, 1.0, 1.0},
          {2.5, 100, 1.0, 1.0, 1.0}, // interpoliert
          {3.0, 90, 1.0, 1.0, 1.0},
          {3.5, 75, 1.0, 1.0, 1.0}, // interpoliert
          {4.0, 60, 1.0, 1.0, 2.0},
          {4.5, 50, 1.0, 1.0, 2.0}, // interpoliert
          {5.0, 40, 1.0, 1.0, 2.0},
      }}
{}

double MultigradeModel::isoR(double grade) const
{
    grade = std::clamp(grade, grades_.front().grade, grades_.back().grade);
    for (std::size_t i = 1; i < grades_.size(); ++i) {
        const GradeData& a = grades_[i - 1];
        const GradeData& b = grades_[i];
        if (grade <= b.grade) {
            const double t = (grade - a.grade) / (b.grade - a.grade);
            return a.isoR + t * (b.isoR - a.isoR);
        }
    }
    return grades_.back().isoR;
}

double MultigradeModel::factor(double grade) const
{
    // 00 bis 3½: gleiche Belichtung, 4 bis 5: doppelte; dazwischen linear
    if (grade <= 3.5) {
        return 1.0;
    }
    if (grade >= 4.0) {
        return 2.0;
    }
    return 1.0 + (grade - 3.5) / 0.5;
}

PaperCurve::Component MultigradeModel::component(double grade, bool compensate) const
{
    PaperCurve::Component c;
    c.nu = 1.0; // toe/shoulder der Tabelle (derzeit symmetrisch)
    c.a = slopeForRange(isoR(grade), c.nu);
    // Mittelton (x = 0) auf dMin + midDensity legen
    const double yMid = midDensity / (paperDMax - paperDMin);
    c.x0 = std::log(std::pow(yMid, -1.0 / c.nu) - 1.0) / c.a;
    if (!compensate) {
        c.x0 += std::log10(factor(grade)); // mehr Belichtung nötig: Kurve nach rechts
    }
    return c;
}

PaperCurve MultigradeModel::curve(double grade, bool compensate) const
{
    return PaperCurve(paperDMin, paperDMax, {component(grade, compensate)});
}

PaperCurve MultigradeModel::split(const SplitGrade& s) const
{
    PaperCurve::Component soft = component(-1.0, true);
    PaperCurve::Component hard = component(5.0, true);
    soft.weight = std::max(0.0, s.soft);
    hard.weight = std::max(0.0, s.hard);
    if (soft.weight + hard.weight <= 0.0) {
        return curve(2.0, true);
    }
    return PaperCurve(paperDMin, paperDMax, {soft, hard});
}

} // namespace autoneg::steps
