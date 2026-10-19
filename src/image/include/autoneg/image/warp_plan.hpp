#pragma once

#include "autoneg/base/geometry.hpp"

namespace autoneg::image {

/// Interpolationsfilter (NF-QUAL-03).
enum class Filter
{
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos3
};

/// Zusammengefasste geometrische Abbildung aller G-Stufen (Architektur 9.3).
/// affine bildet Ausgabe-Pixelkoordinaten auf Eingabe-Pixelkoordinaten ab (Rückwärtsabbildung).
struct WarpPlan
{
    base::Mat2x3 affine;
    base::Size out;
    Filter filter = Filter::Lanczos3;

    friend bool operator==(const WarpPlan&, const WarpPlan&) = default;
};

} // namespace autoneg::image
