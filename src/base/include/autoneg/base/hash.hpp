#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace autoneg::base {

/// Deterministische 64-Bit-Prüfsumme für Inhaltsvergleiche (EditList::hash, Cache-Schlüssel).
///
/// Unabhängig von Plattform, Compiler und Programmlauf (anders als std::hash): Zahlen werden
/// byteweise in fester Reihenfolge (little endian) eingerechnet. FNV-1a mit abschließender
/// Durchmischung (splitmix64). Keine kryptographische Prüfsumme.
class Hasher
{
public:
    Hasher& add(std::span<const std::byte> bytes) noexcept;
    Hasher& add(std::uint64_t value) noexcept;
    Hasher& add(std::int64_t value) noexcept;
    Hasher& add(int value) noexcept;
    Hasher& add(bool value) noexcept;
    /// -0.0 wird wie 0.0 behandelt, alle NaN gleich.
    Hasher& add(double value) noexcept;
    /// Mit Längenpräfix, damit "ab"+"c" und "a"+"bc" verschieden sind.
    Hasher& add(std::string_view text) noexcept;

    [[nodiscard]] std::uint64_t value() const noexcept;

private:
    std::uint64_t state_ = 14695981039346656037ULL;
};

} // namespace autoneg::base
