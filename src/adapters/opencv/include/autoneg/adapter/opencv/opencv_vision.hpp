#pragma once

#include "autoneg/ports/vision.hpp"

namespace autoneg::adapter::opencv {

/// Bildanalyse-Grundfunktionen mit OpenCV (Architektur 9.10): Canny, findContours,
/// HoughLinesP, morphologyEx. Vertrag siehe ports/vision.hpp. Threadsicher (keine Zustände).
class OpenCvVision final : public ports::IVision
{
public:
    [[nodiscard]] base::Expected<image::ImageBuffer> edges(const image::ImageBuffer& img,
                                                           float sigma) override;
    [[nodiscard]] base::Expected<std::vector<ports::Polygon>>
    contours(const image::ImageBuffer& mask) override;
    [[nodiscard]] base::Expected<std::vector<ports::Line>> lines(const image::ImageBuffer& edges) override;
    [[nodiscard]] base::Expected<image::ImageBuffer> morphology(const image::ImageBuffer& mask,
                                                                ports::MorphOp op, int radius) override;
};

} // namespace autoneg::adapter::opencv
