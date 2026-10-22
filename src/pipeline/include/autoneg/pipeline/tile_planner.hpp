#pragma once

#include "autoneg/image/image_spec.hpp"
#include "autoneg/image/tile_grid.hpp"

namespace autoneg::pipeline {

/// Plant die Kacheln für Export und Detailansicht (Architektur 5.5, 9.4).
class TilePlanner
{
public:
    explicit TilePlanner(int tileSize = 1024)
        : tileSize_(tileSize)
    {}

    [[nodiscard]] image::TileGrid plan(const image::ImageSpec& spec, int halo) const
    {
        return image::TileGrid(spec, tileSize_, halo);
    }
    [[nodiscard]] int tileSize() const noexcept { return tileSize_; }

private:
    int tileSize_;
};

} // namespace autoneg::pipeline
