#include "autoneg/model/ids.hpp"

namespace autoneg::model {

namespace {

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

base::Expected<FrameId> FrameId::fromHex(std::string_view hex)
{
    if (hex.size() != 2 * size) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Prüfsumme muss 32 Hexadezimalzeichen haben: '" + std::string(hex) + "'");
    }
    std::array<std::uint8_t, size> bytes{};
    for (std::size_t i = 0; i < size; ++i) {
        const int hi = hexDigit(hex[2 * i]);
        const int lo = hexDigit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return base::makeError(base::ErrorCode::InvalidArgument,
                                   "Ungültiges Zeichen in Prüfsumme: '" + std::string(hex) + "'");
        }
        bytes[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return FrameId(bytes);
}

std::string FrameId::toHex() const
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(2 * size);
    for (const std::uint8_t b : hash_) {
        out.push_back(digits[b >> 4U]);
        out.push_back(digits[b & 0x0fU]);
    }
    return out;
}

} // namespace autoneg::model
