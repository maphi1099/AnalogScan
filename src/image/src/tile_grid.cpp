#include "autoneg/image/tile_grid.hpp"

#include <algorithm>

namespace autoneg::image {

TileGrid::TileGrid(const ImageSpec& spec, int tile, int halo)
{
    if (!spec.valid()) {
        return;
    }
    tile = std::max(tile, 1);
    halo = std::max(halo, 0);
    columns_ = (spec.width + tile - 1) / tile;
    rows_ = (spec.height + tile - 1) / tile;
    const Roi image{0, 0, spec.width, spec.height};
    tiles_.reserve(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < columns_; ++c) {
            const Roi core = Roi{c * tile, r * tile, tile, tile}.intersect(image);
            const Roi padded =
                Roi{core.x - halo, core.y - halo, core.w + 2 * halo, core.h + 2 * halo}.intersect(image);
            tiles_.push_back(Tile{core, padded});
        }
    }
}

} // namespace autoneg::image
