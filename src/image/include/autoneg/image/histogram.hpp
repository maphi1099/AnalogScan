#pragma once

#include "autoneg/base/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace autoneg::image {

/// Histogramm R, G, B und Luminanz (LF-EDT-13) mit Beschnittanteilen (LF-EDT-14).
/// Liegt nach A-01 in autoneg::image (Rechen-Backend und Analyse nutzen es).
struct Histogram
{
    static constexpr std::size_t red = 0;
    static constexpr std::size_t green = 1;
    static constexpr std::size_t blue = 2;
    static constexpr std::size_t luminance = 3;

    std::array<std::vector<std::uint32_t>, 4> bins;
    base::Vec3 clipLow;  ///< Anteil 0..1 je Kanal an oder unter dem Schwarzpunkt
    base::Vec3 clipHigh; ///< Anteil 0..1 je Kanal an oder über dem Weißpunkt

    friend bool operator==(const Histogram&, const Histogram&) = default;
};

} // namespace autoneg::image
