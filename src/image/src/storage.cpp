#include "autoneg/image/storage.hpp"

namespace autoneg::image {

HostStorage::HostStorage(std::size_t sizeBytes)
    : data_((sizeBytes + sizeof(float) - 1) / sizeof(float), 0.0F)
    , sizeBytes_(sizeBytes)
{}

std::span<std::byte> HostStorage::bytes() noexcept
{
    return std::as_writable_bytes(std::span(data_)).first(sizeBytes_);
}

std::span<const std::byte> HostStorage::bytes() const noexcept
{
    return std::as_bytes(std::span(data_)).first(sizeBytes_);
}

std::span<float> HostStorage::floats() noexcept
{
    return std::span(data_).first(sizeBytes_ / sizeof(float));
}

std::span<const float> HostStorage::floats() const noexcept
{
    return std::span(data_).first(sizeBytes_ / sizeof(float));
}

} // namespace autoneg::image
