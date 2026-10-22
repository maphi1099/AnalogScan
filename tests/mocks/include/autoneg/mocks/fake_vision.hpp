#pragma once

// Fake von ports::IVision ohne OpenCV (einfache, aber vertragstreue Algorithmen nach
// vision.hpp). Für Unit-Tests der Analyse und die Vertragstests K-14.

#include "autoneg/ports/vision.hpp"

namespace autoneg::mocks {

class FakeVision final : public ports::IVision
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

} // namespace autoneg::mocks
