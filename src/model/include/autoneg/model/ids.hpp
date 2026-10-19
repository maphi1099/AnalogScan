#pragma once

#include "autoneg/base/error.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace autoneg::model {

/// Kennung eines Bildes: Inhaltsprüfsumme XXH3-128 des Scans (AE-09).
/// Bleibt beim Umbenennen gleich und ordnet Begleitdateien wieder zu (LF-DAT-06).
class FrameId
{
public:
    static constexpr std::size_t size = 16;

    FrameId() = default;
    explicit FrameId(const std::array<std::uint8_t, size>& hash) noexcept
        : hash_(hash)
    {}

    /// Aus genau 32 Hexadezimalzeichen (Groß- oder Kleinschreibung).
    [[nodiscard]] static base::Expected<FrameId> fromHex(std::string_view hex);

    /// 32 Zeichen, Kleinbuchstaben.
    [[nodiscard]] std::string toHex() const;
    [[nodiscard]] std::span<const std::uint8_t, size> bytes() const noexcept { return hash_; }

    friend auto operator<=>(const FrameId&, const FrameId&) = default;

private:
    std::array<std::uint8_t, size> hash_{};
};

/// Kennung einer Rolle (normierter Ordnerpfad).
struct RollId
{
    std::string value;

    friend auto operator<=>(const RollId&, const RollId&) = default;
};

/// Kennung eines Verarbeitungsschritts, z. B. "autoneg.exposure". Einmal vergeben, nie ändern.
class StepId
{
public:
    StepId() = default;
    explicit StepId(std::string id)
        : id_(std::move(id))
    {}

    [[nodiscard]] const std::string& value() const noexcept { return id_; }

    friend auto operator<=>(const StepId&, const StepId&) = default;

private:
    std::string id_;
};

/// Kennung einer Rückgängig-Gruppe (ein Befehl auf mehrere Bilder, LF-BAT-06).
struct UndoGroupId
{
    std::uint64_t value = 0;

    friend auto operator<=>(const UndoGroupId&, const UndoGroupId&) = default;
};

} // namespace autoneg::model
