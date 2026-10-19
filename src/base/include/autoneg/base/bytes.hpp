#pragma once

#include <cstdint>
#include <vector>

namespace autoneg::base {

/// Byte-Folge, z. B. kodierte Datei im Speicher oder ICC-Profil.
using Bytes = std::vector<std::uint8_t>;

} // namespace autoneg::base
