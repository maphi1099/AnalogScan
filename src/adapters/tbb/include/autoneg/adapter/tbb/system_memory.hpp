#pragma once

#include <cstdint>

namespace autoneg::adapter::tbb {

/// Physischer Arbeitsspeicher des Rechners in Bytes (0, wenn nicht ermittelbar). Grundlage
/// des Standard-Speicherbudgets (NF-PERF-12: 40 % des Arbeitsspeichers). Plattformabhängig.
[[nodiscard]] std::uint64_t physicalMemoryBytes();

} // namespace autoneg::adapter::tbb
