#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"

#include <vector>

namespace autoneg::ports {

/// Geschlossener Linienzug in Pixelkoordinaten.
struct Polygon
{
    std::vector<base::Point2> points;

    friend bool operator==(const Polygon&, const Polygon&) = default;
};

/// Strecke in Pixelkoordinaten.
struct Line
{
    base::Point2 a;
    base::Point2 b;

    friend bool operator==(const Line&, const Line&) = default;
};

enum class MorphOp
{
    Erode,
    Dilate,
    Open,
    Close
};

/// Bildanalyse-Grundfunktionen für Bildfeld- und Schräglagenerkennung (LF-GEO).
/// Adapter: OpenCvVision. Threadsicher. Masken sind einkanalige F32-Bilder (0 oder 1);
/// Koordinaten in Pixeln, Pixelmitten bei ganzzahligen Werten (wie OpenCV).
/// Vertrag (K-M5-03), geprüft in K-14 gegen Fake und Adapter:
class IVision
{
public:
    virtual ~IVision() = default;

    /// Dünne Kantenmaske (Canny): Gauß-Glättung mit sigma, Sobel-Gradient, Unterdrückung von
    /// Nicht-Maxima, Hysterese mit 10 % und 20 % des größten Gradientenbetrags.
    /// Eingabe mit 3 Kanälen wird vorher in Luminanz umgerechnet.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> edges(const image::ImageBuffer& img,
                                                                   float sigma) = 0;
    /// Äußere Konturen der Flächen einer Maske (je Fläche ein Linienzug über ihren Rand).
    [[nodiscard]] virtual base::Expected<std::vector<Polygon>> contours(const image::ImageBuffer& mask) = 0;
    /// Strecken einer Kantenmaske (Hough): Auflösung 1 px und 0,25°, mindestens 20 Punkte und
    /// 20 px lang, Lücken bis 3 px werden überbrückt.
    [[nodiscard]] virtual base::Expected<std::vector<Line>> lines(const image::ImageBuffer& edges) = 0;
    /// Morphologie mit elliptischem Strukturelement (2·radius + 1)².
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> morphology(const image::ImageBuffer& mask,
                                                                        MorphOp op, int radius) = 0;
};

} // namespace autoneg::ports
