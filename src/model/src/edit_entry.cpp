#include "autoneg/model/edit_entry.hpp"

#include "autoneg/base/hash.hpp"

namespace autoneg::model {

std::uint64_t EditEntry::hash() const noexcept
{
    base::Hasher h;
    h.add(std::string_view(step.value()));
    h.add(version).add(rank);
    h.add(params.hash());
    h.add(enabled);
    h.add(static_cast<int>(origin));
    h.add(copiedFrom.has_value());
    if (copiedFrom) {
        h.add(std::as_bytes(copiedFrom->bytes()));
    }
    return h.value();
}

} // namespace autoneg::model
