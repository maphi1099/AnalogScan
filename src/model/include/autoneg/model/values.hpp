#pragma once

#include <compare>
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

} // namespace autoneg::model
