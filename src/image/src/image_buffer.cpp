#include "autoneg/image/image_buffer.hpp"

#include <algorithm>
#include <string>

namespace autoneg::image {

namespace {

std::string describe(const ImageSpec& spec)
{
    return std::to_string(spec.width) + " x " + std::to_string(spec.height) + " x " +
           std::to_string(spec.channels);
}

} // namespace

base::Expected<std::shared_ptr<HostStorage>> ImageBuffer::makeHostStorage(const ImageSpec& spec)
{
    if (!spec.valid()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Ungültige Bildgröße " + describe(spec));
    }
    return std::make_shared<HostStorage>(spec.sizeBytes());
}

base::Expected<ImageBuffer> ImageBuffer::wrap(const ImageSpec& spec, std::shared_ptr<const IStorage> storage)
{
    if (!spec.valid()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Ungültige Bildgröße " + describe(spec));
    }
    if (storage == nullptr || storage->sizeBytes() < spec.sizeBytes()) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Speicher zu klein für Bild " + describe(spec));
    }
    return ImageBuffer(spec, std::move(storage));
}

base::Expected<ImageBuffer> ImageBuffer::allocateHost(const ImageSpec& spec)
{
    return allocateHost(spec, [](std::span<std::byte> /*bytes*/) {});
}

base::Expected<ImageBuffer> ImageBuffer::fromFloats(const ImageSpec& spec, std::span<const float> values)
{
    if (spec.format != PixelFormat::F32) {
        return base::makeError(base::ErrorCode::InvalidArgument, "fromFloats erwartet das Format F32");
    }
    if (values.size() != spec.pixelCount() * static_cast<std::size_t>(std::max(spec.channels, 0))) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Anzahl der Werte passt nicht zu Bild " + describe(spec));
    }
    auto storage = makeHostStorage(spec);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    std::ranges::copy(values, (*storage)->floats().begin());
    return ImageBuffer(spec, std::move(*storage));
}

MemoryLocation ImageBuffer::location() const noexcept
{
    return storage_ ? storage_->location() : MemoryLocation::Host;
}

std::span<const std::byte> ImageBuffer::hostBytes() const noexcept
{
    if (!storage_) {
        return {};
    }
    const auto bytes = storage_->hostBytes();
    return bytes.size() < spec_.sizeBytes() ? std::span<const std::byte>{} : bytes.first(spec_.sizeBytes());
}

std::span<const float> ImageBuffer::host() const noexcept
{
    if (!storage_ || spec_.format != PixelFormat::F32) {
        return {};
    }
    const auto floats = storage_->hostFloats();
    const std::size_t count = spec_.sizeBytes() / sizeof(float);
    return floats.size() < count ? std::span<const float>{} : floats.first(count);
}

base::Expected<ImageView> ImageBuffer::view(const Roi& roi) const
{
    if (!roi.inside(spec_.width, spec_.height)) {
        return base::makeError(base::ErrorCode::OutOfRange,
                               "Ausschnitt (" + std::to_string(roi.x) + ", " + std::to_string(roi.y) + ", " +
                                   std::to_string(roi.w) + ", " + std::to_string(roi.h) +
                                   ") liegt nicht im Bild " + describe(spec_));
    }
    return ImageView(*this, roi);
}

base::Expected<ImageBuffer> ImageBuffer::copyRegion(const Roi& roi) const
{
    if (!roi.inside(spec_.width, spec_.height)) {
        return base::makeError(base::ErrorCode::OutOfRange,
                               "Ausschnitt liegt nicht im Bild " + describe(spec_));
    }
    const auto src = hostBytes();
    if (src.empty()) {
        return base::makeError(base::ErrorCode::Unsupported, "copyRegion braucht einen Host-Puffer");
    }
    ImageSpec spec = spec_;
    spec.width = roi.w;
    spec.height = roi.h;
    const std::size_t pixelBytes = static_cast<std::size_t>(spec_.channels) * bytesPerSample(spec_.format);
    return allocateHost(spec, [&](std::span<std::byte> dst) {
        const std::size_t rowBytes = spec.rowBytes();
        for (int y = 0; y < roi.h; ++y) {
            const std::size_t from = static_cast<std::size_t>(roi.y + y) * spec_.rowBytes() +
                                     static_cast<std::size_t>(roi.x) * pixelBytes;
            std::ranges::copy(src.subspan(from, rowBytes),
                              dst.subspan(static_cast<std::size_t>(y) * rowBytes).begin());
        }
    });
}

ImageView ImageBuffer::view() const
{
    return ImageView(*this, Roi{0, 0, spec_.width, spec_.height});
}

std::span<const float> ImageView::row(int y) const noexcept
{
    const std::span<const float> all = buf_.host();
    if (all.empty() || y < 0 || y >= roi_.h) {
        return {};
    }
    const auto& spec = buf_.spec();
    const std::size_t ch = static_cast<std::size_t>(spec.channels);
    const std::size_t offset =
        static_cast<std::size_t>(roi_.y + y) * spec.samplesPerRow() + static_cast<std::size_t>(roi_.x) * ch;
    return all.subspan(offset, static_cast<std::size_t>(roi_.w) * ch);
}

} // namespace autoneg::image
