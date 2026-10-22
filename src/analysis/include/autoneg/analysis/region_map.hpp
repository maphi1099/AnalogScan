#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/model/values.hpp"

#include <cstdint>
#include <vector>

namespace autoneg::analysis {

/// Bereichsklassen eines Scans (LF-GEO-05).
enum class RegionClass : std::uint8_t
{
    Background, ///< Leuchtfläche, Perforationslöcher (ungefiltertes Licht)
    Holder,     ///< Filmhalter, Maske
    Rebate,     ///< unbelichteter Filmrand (Basismessung, Kontaktabzug mit Filmrand)
    Image       ///< belichtetes Bildfeld
};

/// Klassenbild eines Scans (Architektur 9.7) in entzerrten Koordinaten.
///
/// Abweichung (K-M5-02): Labels als Byte-Vektor statt ImageBuffer U8 (die Analyse rechnet
/// ohne Backend); zusätzlich Winkel und Quellgröße, damit classOf Punkte des ungedrehten
/// Quellbilds annimmt (die Basismessung läuft auf dem Quellbild).
class RegionMap
{
public:
    RegionMap() = default;
    /// labels: width × height, zeilenweise; skewDeg: Schräglage, mit der entzerrt wurde.
    RegionMap(int width, int height, std::vector<RegionClass> labels, double skewDeg,
              std::vector<model::NormRect> rebate);

    [[nodiscard]] int width() const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }
    [[nodiscard]] bool empty() const noexcept { return labels_.empty(); }

    /// Klasse am Punkt p (normiert auf das Quellbild, also vor der Entzerrung).
    /// Punkte außerhalb gelten als Background.
    [[nodiscard]] RegionClass classOf(model::NormPoint p) const;
    /// Klasse im entzerrten Klassenbild (Pixel).
    [[nodiscard]] RegionClass at(int x, int y) const;
    /// Filmrand-Bereiche (normiert, entzerrte Koordinaten wie FrameDetection::frame).
    [[nodiscard]] const std::vector<model::NormRect>& rebate() const noexcept { return rebate_; }
    /// Anteil der Pixel je Klasse (Background, Holder, Rebate, Image).
    [[nodiscard]] std::vector<double> shares() const;

private:
    int w_ = 0;
    int h_ = 0;
    double skewDeg_ = 0.0;
    std::vector<RegionClass> labels_;
    std::vector<model::NormRect> rebate_;
};

} // namespace autoneg::analysis
