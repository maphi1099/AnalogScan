#include "autoneg/pipeline/backend_selector.hpp"

namespace autoneg::pipeline {

ports::IComputeBackend& selectBackend(std::span<ports::IComputeBackend* const> preferred,
                                      ports::IComputeBackend& fallback)
{
    for (ports::IComputeBackend* b : preferred) {
        if (b != nullptr && b->available()) {
            return *b;
        }
    }
    return fallback;
}

} // namespace autoneg::pipeline
