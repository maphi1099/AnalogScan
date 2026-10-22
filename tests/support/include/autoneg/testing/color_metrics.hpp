#pragma once

// Farbabstände für Tests: linear Rec. 2020 (D65) → CIE XYZ → CIELAB → CIEDE2000.

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"

namespace autoneg::testing {

struct Lab
{
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

[[nodiscard]] Lab labFromRec2020(const base::Vec3& rgb);
[[nodiscard]] double deltaE2000(const Lab& x, const Lab& y);
[[nodiscard]] double deltaE2000(const base::Vec3& rgbA, const base::Vec3& rgbB);

/// Mittleres und größtes ΔE2000 zweier gleich großer RGB-Bilder.
struct DeltaStats
{
    double mean = 0.0;
    double max = 0.0;
};
[[nodiscard]] DeltaStats compareImages(const image::ImageBuffer& a, const image::ImageBuffer& b);

/// Abstand zum neutralen Grau gleicher Helligkeit (Farbstich eines Graufelds).
[[nodiscard]] double deltaToNeutral(const base::Vec3& rgb);

} // namespace autoneg::testing
