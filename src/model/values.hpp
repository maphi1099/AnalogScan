#pragma once

#include <compare>
#include <string>
#include <vector>

namespace autoneg::model {

/// Punkt normiert auf das Quellbild, 0..1 (AE-10).
struct NormPoint
{
    double x = 0.0;
    double y = 0.0;

    friend auto operator<=>(const NormPoint&, const NormPoint&) = default;
};

/// Rechteck normiert auf das Quellbild, 0..1 (AE-10). Volles Bild: {0, 0, 1, 1}.
struct NormRect
{
    double x = 0.0;
    double y = 0.0;
    double w = 1.0;
    double h = 1.0;

    [[nodiscard]] static constexpr NormRect full() noexcept { return {}; }

    friend auto operator<=>(const NormRect&, const NormRect&) = default;
};

/// Gradationskurve aus Stützpunkten (LF-EDT-08), x aufsteigend.
struct Curve
{
    std::vector<NormPoint> points;

    friend auto operator<=>(const Curve&, const Curve&) = default;
};

/// Retuschestelle: Zielstelle, Versatz der Quelle, Radius (normiert auf die Bildbreite).
struct Spot
{
    NormPoint center;
    NormPoint offset;
    double radius = 0.0;

    friend auto operator<=>(const Spot&, const Spot&) = default;
};

using SpotList = std::vector<Spot>;

/// Pinselstrich der Retusche: Punkte normiert auf das Bild, Breite normiert auf die Bildbreite;
/// erase = Radierer (nimmt Fläche weg).
struct RetouchStroke
{
    bool erase = false;
    double width = 0.0;
    std::vector<NormPoint> points;

    friend auto operator<=>(const RetouchStroke&, const RetouchStroke&) = default;
};

/// Form der Staubretusche (LF-EDT-12, K-M10-23): Kreis oder freie Fläche aus Pinselstrichen.
/// Innerhalb der Form sucht die Retusche nach Auffälligkeiten (Staub, Fussel, Kratzer) und
/// entfernt nur diese. enabled = Haken in der Liste.
struct RetouchShape
{
    enum class Kind
    {
        Circle,
        Brush
    };
    Kind kind = Kind::Circle;
    std::string name;
    bool enabled = true;
    NormPoint center;                   ///< Kreis: Mittelpunkt
    double radius = 0.0;                ///< Kreis: Radius normiert auf die Bildbreite
    std::vector<RetouchStroke> strokes; ///< Fläche: Striche in Reihenfolge (Radierer wirken auf frühere)

    friend auto operator<=>(const RetouchShape&, const RetouchShape&) = default;
};

using RetouchShapes = std::vector<RetouchShape>;

} // namespace autoneg::model
