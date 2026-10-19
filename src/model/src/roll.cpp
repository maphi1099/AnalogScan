#include "autoneg/model/roll.hpp"

#include <algorithm>

namespace autoneg::model {

int Roll::indexOf(const FrameId& f) const noexcept
{
    const auto it = std::ranges::find(frames, f);
    return it == frames.end() ? -1 : static_cast<int>(it - frames.begin());
}

} // namespace autoneg::model
