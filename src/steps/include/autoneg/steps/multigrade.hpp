#pragma once

#include "autoneg/image/pixel_program.hpp"

#include <array>
#include <vector>

namespace autoneg::steps {

/// Kennwerte einer Multigrade-Filterstufe (Architektur 5.7, LF-KON-09).
struct GradeData
{
    double grade = 2.0;        ///< −1 = 00, 0 … 5 in halben Stufen
    int isoR = 110;            ///< ISO-Bereich R nach ISO 6846
    double toe = 1.0;          ///< Form des Fußes (1 = symmetrisch zur Schulter)
    double shoulder = 1.0;     ///< Form der Schulter
    double filterFactor = 1.0; ///< Belichtungsfaktor am Vergrößerer (00–3½: 1, 4–5: 2)
};

/// Anteile einer Split-Grade-Belichtung (LF-KON-12): weich mit Filter 00, hart mit Filter 5.
struct SplitGrade
{
    double soft = 0.0;
    double hard = 0.0;

    friend bool operator==(const SplitGrade&, const SplitGrade&) = default;
};

/// Kennlinie eines Fotopapiers: Dichte über dem Logarithmus der Belichtung.
///
/// Modell: logistische Kennlinie (Richards-Kurve mit Form ν = toe/shoulder)
///   D(x) = dMin + (dMax − dMin) · (1 + e^(−a·(x − x0)))^(−ν),
/// a so gewählt, dass der ISO-Bereich (Abstand der Belichtungen bei dMin + 0,04 und bei
/// dMin + 0,9·(dMax − dMin)) genau isoR/100 beträgt. Split-Grade: gewichtetes Mittel zweier
/// Kennlinien. x = 0 ist die Belichtung des Mitteltons (Abschnitt "Abbildung" unten).
class PaperCurve
{
public:
    struct Component
    {
        double a = 1.0;
        double x0 = 0.0;
        double nu = 1.0;
        double weight = 1.0;
    };

    PaperCurve(double dMin, double dMax, std::vector<Component> components);

    [[nodiscard]] double dMin() const noexcept { return dMin_; }
    [[nodiscard]] double dMax() const noexcept { return dMax_; }
    [[nodiscard]] double density(double logE) const;
    /// Gemessener ISO-Bereich der Kurve (Bisektion), für Tests (U-STP-08).
    [[nodiscard]] double isoRange() const;
    /// Steigung dD/dx an der Stelle x (numerisch).
    [[nodiscard]] double gamma(double logE) const;

    /// Wertetabelle für das Punktprogramm: Eingang v (linearer Wert nach den Tonwertgrenzen,
    /// 0 … 1), Ausgang Remission relativ zum Papierweiß 10^−(D − dMin).
    [[nodiscard]] image::Lut1D toLut(int n = 4096) const;

private:
    double dMin_;
    double dMax_;
    std::vector<Component> components_;
};

/// Abbildung des linearen Werts v (nach DensityRangeStep, 0 … 1) auf die Belichtung des
/// Papiers. n = 1 + log10(v·(1 − 10^−R) + 10^−R)/R ist die normierte Negativdichte (R = 2);
/// x(v) = K·(n(0,18) − n). Der Mittelton v = 0,18 liegt bei x = 0; K ist so gewählt, dass auf
/// Stufe 2 (kompensiert) v = 1 genau dMin + 0,04 (Papierweiß) und v = 0,18 die Remission 0,18
/// ergibt.
[[nodiscard]] double paperExposure(double v);

/// Tabelle der Ilford-Multigrade-Stufen und daraus abgeleitete Kennlinien (Architektur 5.7).
///
/// ISO-R-Werte aus dem Datenblatt ILFORD MULTIGRADE IV RC DeLuxe (ISO 6846): 00 R180,
/// 0 R160, 1 R130, 2 R110, 3 R90, 4 R60, 5 R40; Halbstufen linear interpoliert (L-05).
/// Belichtung 00–3½ gleich, 4–5 doppelt. Papier dMin 0,05, dMax 2,10.
class MultigradeModel
{
public:
    static constexpr double paperDMin = 0.05;
    static constexpr double paperDMax = 2.10;
    /// Mittelton: Remission 0,18 → Dichte über Papierweiß.
    static constexpr double midDensity = 0.744727494896694; // −log10(0,18)

    MultigradeModel();

    /// Zwischenstufen werden linear interpoliert (LF-KON-11). compensate: Mittelton bleibt
    /// bei jeder Stufe gleich (LF-KON-10); sonst wirkt der Filterfaktor (4–5 dunkler … heller).
    [[nodiscard]] PaperCurve curve(double grade, bool compensate = true) const;
    /// Split-Grade aus Stufe 00 und 5 (LF-KON-12).
    [[nodiscard]] PaperCurve split(const SplitGrade& s) const;
    [[nodiscard]] double isoR(double grade) const;
    [[nodiscard]] double factor(double grade) const;
    [[nodiscard]] const std::array<GradeData, 12>& grades() const noexcept { return grades_; }

private:
    [[nodiscard]] PaperCurve::Component component(double grade, bool compensate) const;

    std::array<GradeData, 12> grades_;
};

} // namespace autoneg::steps
