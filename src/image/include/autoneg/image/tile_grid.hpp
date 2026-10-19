#pragma once

#include "autoneg/image/image_spec.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace autoneg::image {

/// Eine Kachel: Ergebnisbereich und Lesebereich mit Rand (NF-ARC-08).
struct Tile
{
    Roi core;   ///< Ergebnisbereich; die Kernbereiche aller Kacheln überdecken das Bild genau einmal
    Roi padded; ///< core plus Rand, am Bildrand beschnitten

    friend bool operator==(const Tile&, const Tile&) = default;
};

/// Zerlegung eines Bildes in Kacheln, zeilenweise von links oben.
class TileGrid
{
public:
    /// tile: Kantenlänge des Kernbereichs (mind. 1); halo: Rand je Seite (mind. 0).
    TileGrid(const ImageSpec& spec, int tile, int halo);

    [[nodiscard]] std::span<const Tile> tiles() const noexcept { return tiles_; }
    [[nodiscard]] std::size_t count() const noexcept { return tiles_.size(); }
    [[nodiscard]] int columns() const noexcept { return columns_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }

private:
    std::vector<Tile> tiles_;
    int columns_ = 0;
    int rows_ = 0;
};

} // namespace autoneg::image
