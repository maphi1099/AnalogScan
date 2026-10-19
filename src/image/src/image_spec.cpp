#include "autoneg/image/image_spec.hpp"

#include <algorithm>

namespace autoneg::image {

std::size_t bytesPerSample(PixelFormat format) noexcept
{
    switch (format) {
    case PixelFormat::F32:
        return 4;
    case PixelFormat::F16:
    case PixelFormat::U16:
        return 2;
    case PixelFormat::U8:
        return 1;
    }
    return 0;
}

std::size_t ImageSpec::pixelCount() const noexcept
{
    return valid() ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) : 0;
}

std::size_t ImageSpec::samplesPerRow() const noexcept
{
    return valid() ? static_cast<std::size_t>(width) * static_cast<std::size_t>(channels) : 0;
}

std::size_t ImageSpec::rowBytes() const noexcept
{
    return samplesPerRow() * bytesPerSample(format);
}

std::size_t ImageSpec::sizeBytes() const noexcept
{
    return valid() ? rowBytes() * static_cast<std::size_t>(height) : 0;
}

std::size_t Roi::area() const noexcept
{
    return empty() ? 0 : static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
}

bool Roi::inside(int width, int height) const noexcept
{
    return !empty() && x >= 0 && y >= 0 && x <= width - w && y <= height - h;
}

bool Roi::contains(int px, int py) const noexcept
{
    return px >= x && py >= y && px - x < w && py - y < h;
}

Roi Roi::intersect(const Roi& other) const noexcept
{
    const int x0 = std::max(x, other.x);
    const int y0 = std::max(y, other.y);
    const int x1 = std::min(x + w, other.x + other.w);
    const int y1 = std::min(y + h, other.y + other.h);
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace autoneg::image
