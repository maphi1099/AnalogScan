#include "autoneg/base/hash.hpp"

#include <bit>
#include <cmath>

namespace autoneg::base {

namespace {

constexpr std::uint64_t fnvPrime = 1099511628211ULL;

std::uint64_t mix(std::uint64_t z) noexcept
{
    // splitmix64-Finalisierer für gute Bitverteilung
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

} // namespace

Hasher& Hasher::add(std::span<const std::byte> bytes) noexcept
{
    for (const std::byte b : bytes) {
        state_ ^= static_cast<std::uint64_t>(b);
        state_ *= fnvPrime;
    }
    return *this;
}

Hasher& Hasher::add(std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        state_ ^= (value >> shift) & 0xffU;
        state_ *= fnvPrime;
    }
    return *this;
}

Hasher& Hasher::add(std::int64_t value) noexcept
{
    return add(static_cast<std::uint64_t>(value));
}

Hasher& Hasher::add(int value) noexcept
{
    return add(static_cast<std::int64_t>(value));
}

Hasher& Hasher::add(bool value) noexcept
{
    return add(static_cast<std::uint64_t>(value ? 1U : 0U));
}

Hasher& Hasher::add(double value) noexcept
{
    if (value == 0.0) {
        value = 0.0; // -0.0 -> 0.0
    }
    if (std::isnan(value)) {
        return add(static_cast<std::uint64_t>(0x7ff8000000000000ULL));
    }
    return add(std::bit_cast<std::uint64_t>(value));
}

Hasher& Hasher::add(std::string_view text) noexcept
{
    add(static_cast<std::uint64_t>(text.size()));
    return add(std::as_bytes(std::span(text.data(), text.size())));
}

std::uint64_t Hasher::value() const noexcept
{
    return mix(state_);
}

} // namespace autoneg::base
