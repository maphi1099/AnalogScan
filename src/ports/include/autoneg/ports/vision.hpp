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
/// Adapter: OpenCvVision. Threadsicher. Masken sind einkanalige Bilder (0 oder 1).
class IVision
{
public:
    virtual ~IVision() = default;

    [[nodiscard]] virtual base::Expected<image::ImageBuffer> edges(const image::ImageBuffer& img,
                                                                   float sigma) = 0;
    [[nodiscard]] virtual base::Expected<std::vector<Polygon>> contours(const image::ImageBuffer& mask) = 0;
    [[nodiscard]] virtual base::Expected<std::vector<Line>> lines(const image::ImageBuffer& edges) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> morphology(const image::ImageBuffer& mask,
                                                                        MorphOp op, int radius) = 0;
};

} // namespace autoneg::ports
