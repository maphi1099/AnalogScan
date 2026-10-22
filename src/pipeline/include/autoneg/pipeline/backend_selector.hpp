#pragma once

#include "autoneg/ports/compute_backend.hpp"

#include <span>

namespace autoneg::pipeline {

/// Wählt das erste verfügbare Rechen-Backend, sonst den Rückfall (CPU) – ohne Fehlermeldung
/// (NF-ARC-05). Ergänzung zu Architektur 9.10; aufgerufen in der Kompositionswurzel.
[[nodiscard]] ports::IComputeBackend& selectBackend(std::span<ports::IComputeBackend* const> preferred,
                                                    ports::IComputeBackend& fallback);

} // namespace autoneg::pipeline
