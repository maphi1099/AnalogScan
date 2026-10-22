#pragma once

// Interne Hilfen der Analyse: einkanalige float-Ebenen, Perzentile, Drehung, Blockmittel.
// Keine öffentliche Schnittstelle.

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"

#include <cstddef>
#include <vector>

namespace autoneg::analysis::detail {

/// Einkanalige Ebene, zeilenweise.
struct Plane
{
    int w = 0;
    int h = 0;
    std::vector<float> v;

    Plane() = default;
    Plane(int width, int height, float fill = 0.0F)
        : w(width)
        , h(height)
        , v(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill)
    {}

    [[nodiscard]] float& at(int x, int y) { return v[index(x, y)]; }
    [[nodiscard]] float at(int x, int y) const { return v[index(x, y)]; }
    [[nodiscard]] std::size_t index(int x, int y) const
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    }
    /// Bilinear; außerhalb NaN.
    [[nodiscard]] float sample(double x, double y) const;
};

/// Luminanz (Rec. 2020) eines Bilds mit 1 oder 3 Kanälen (F32).
[[nodiscard]] Plane luminance(const image::ImageBuffer& img);
/// Kanal c eines Bilds (bei einem Kanal immer Kanal 0).
[[nodiscard]] Plane channel(const image::ImageBuffer& img, int c);

/// Perzentil q ∈ [0, 1] (Rangwert, ohne Interpolation) der endlichen Werte; NaN bei leerer Menge.
[[nodiscard]] double percentile(std::vector<float> values, double q);
/// Median und MAD (normiert, × 1,4826) der endlichen Werte.
struct Robust
{
    double median = 0.0;
    double mad = 0.0;
};
[[nodiscard]] Robust robust(std::vector<float> values);

/// Dreht die Ebene um ihre Mitte so, dass der Inhalt von Ausgabepunkt q an der Stelle
/// R(skewDeg)(q − c) + c der Eingabe liegt (Ausgleich einer Schräglage skewDeg). Außen NaN.
[[nodiscard]] Plane deskew(const Plane& p, double skewDeg);
/// Abbildung eines Punkts der entzerrten Ebene in die Eingabe (siehe deskew).
[[nodiscard]] base::Point2 deskewToSource(base::Point2 q, double skewDeg, double w, double h);
/// Umkehrung: Punkt der Eingabe in die entzerrte Ebene.
[[nodiscard]] base::Point2 sourceToDeskew(base::Point2 p, double skewDeg, double w, double h);

/// Mittel über block × block (Rest am Rand wird verworfen).
[[nodiscard]] Plane blockMean(const Plane& p, int block);

/// Ebene als einkanaliges F32-Bild (z. B. als Maske für IVision).
[[nodiscard]] image::ImageBuffer toImage(const Plane& p);
[[nodiscard]] Plane fromImage(const image::ImageBuffer& img);

/// Vertauscht Zeilen und Spalten.
[[nodiscard]] Plane transpose(const Plane& p);

} // namespace autoneg::analysis::detail
