#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/histogram.hpp"
#include "autoneg/image/image_buffer.hpp"

namespace autoneg::analysis {

/// Dichtegrenzen je Kanal (Parameter dMin/dMax von autoneg.densityrange).
struct DensityRange
{
    base::Vec3 dMin;
    base::Vec3 dMax;
};

/// Automatische Tonwertgrenzen (LF-KON-05): je Kanal die Perzentile clip/2 und 1 − clip/2
/// eines Dichtebilds (D = −log10 T relativ zur Basis). Der Beschnitt verteilt sich gleich auf
/// beide Enden (wie autoneg.densityrange, U-STP-05). Exakte Rangwerte, deterministisch.
class AutoLevels
{
public:
    [[nodiscard]] DensityRange compute(const image::ImageBuffer& density, double clip) const;
};

/// Automatische Farbneutralisierung (LF-KON-06): Farbe, die neutral sein sollte (Eingabe für
/// WhiteBalanceStep::fromNeutral). Iterativ über nahezu neutrale Mitteltöne: Median der
/// Log-Verhältnisse R/G und B/G, dann Mittel der Pixel in dessen Nähe.
class AutoNeutral
{
public:
    [[nodiscard]] base::Vec3 compute(const image::ImageBuffer& img) const;
};

/// Histogramm R, G, B, Luminanz und Beschnittanteile (LF-EDT-13, -14). Werte auf [0, 1];
/// clipLow: Anteil ≤ 0, clipHigh: Anteil ≥ 1. Ganzzahlige Summen, deterministisch.
class HistogramCalculator
{
public:
    [[nodiscard]] image::Histogram compute(const image::ImageBuffer& img, int bins = 256) const;
};

} // namespace autoneg::analysis
