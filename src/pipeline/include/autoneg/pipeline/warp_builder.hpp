#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/warp_plan.hpp"
#include "autoneg/model/values.hpp"

namespace autoneg::pipeline {

/// Fasst aufeinanderfolgende geometrische Stufen zu einer einzigen affinen Abbildung
/// zusammen (Architektur 5.1, AE-05).
///
/// Koordinaten sind kontinuierlich in Pixeln (Pixel (i, j) hat den Mittelpunkt
/// (i + 0,5; j + 0,5)). Jede Operation wirkt auf die aktuelle Zeichenfläche:
///  - orient: zuerst spiegeln (fh: links/rechts, fv: oben/unten), dann q Vierteldrehungen im
///    Uhrzeigersinn; bei ungeradem q tauschen Breite und Höhe;
///  - rotate: Drehung um die Mitte der Zeichenfläche, positive Winkel im Uhrzeigersinn,
///    Größe der Zeichenfläche bleibt;
///  - crop: Ausschnitt normiert auf die aktuelle Zeichenfläche (nach Orientierung und Drehung).
class WarpBuilder
{
public:
    explicit WarpBuilder(base::Size source);

    WarpBuilder& orient(int quarterTurns, bool flipH, bool flipV);
    WarpBuilder& rotate(double degrees);
    WarpBuilder& crop(const model::NormRect& r);
    WarpBuilder& filter(image::Filter f);

    /// Größe der aktuellen Zeichenfläche in Pixeln (nach den bisherigen Operationen).
    [[nodiscard]] base::Point2 canvas() const noexcept { return {width_, height_}; }
    /// Keine Veränderung gegenüber der Quelle (Abbildung und Größe gleich).
    [[nodiscard]] bool isIdentity() const noexcept;
    /// Abbildung Ausgabe -> Eingabe und Ausgabegröße.
    [[nodiscard]] image::WarpPlan build() const;

private:
    base::Size source_;
    base::Mat2x3 forward_; ///< Eingabe -> Zeichenfläche
    double width_;
    double height_;
    image::Filter filter_ = image::Filter::Lanczos3;
};

} // namespace autoneg::pipeline
