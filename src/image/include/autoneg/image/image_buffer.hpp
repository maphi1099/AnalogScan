#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_spec.hpp"
#include "autoneg/image/storage.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace autoneg::image {

class ImageView;

/// Unveränderlicher Bildpuffer (AE-02). Kopien teilen den Speicher und dürfen ohne Sperre
/// zwischen Threads weitergegeben werden.
class ImageBuffer
{
public:
    ImageBuffer() = default; ///< leerer Puffer

    /// Puffer aus vorhandenem Speicher. Fehler, wenn die Beschreibung ungültig ist oder der
    /// Speicher zu klein.
    [[nodiscard]] static base::Expected<ImageBuffer> wrap(const ImageSpec& spec,
                                                          std::shared_ptr<const IStorage> storage);

    /// Neuer Host-Puffer; fill beschreibt die Bytes vor dem Einfrieren (mit 0 vorbelegt).
    template <class Fill>
    [[nodiscard]] static base::Expected<ImageBuffer> allocateHost(const ImageSpec& spec, Fill&& fill);
    [[nodiscard]] static base::Expected<ImageBuffer> allocateHost(const ImageSpec& spec);

    /// Host-Puffer im Format F32 aus Werten (Länge = Pixel x Kanäle).
    [[nodiscard]] static base::Expected<ImageBuffer> fromFloats(const ImageSpec& spec,
                                                                std::span<const float> values);

    [[nodiscard]] const ImageSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] bool empty() const noexcept { return storage_ == nullptr; }
    /// Ort des Speichers; Host bei leerem Puffer.
    [[nodiscard]] MemoryLocation location() const noexcept;
    [[nodiscard]] std::size_t sizeBytes() const noexcept { return spec_.sizeBytes(); }
    [[nodiscard]] const std::shared_ptr<const IStorage>& storage() const noexcept { return storage_; }

    /// Werte bei Host-Puffern im Format F32, sonst leer.
    [[nodiscard]] std::span<const float> host() const noexcept;
    /// Rohdaten bei Host-Puffern, sonst leer.
    [[nodiscard]] std::span<const std::byte> hostBytes() const noexcept;

    /// Ausschnitt; Fehler OutOfRange, wenn er nicht vollständig im Bild liegt (U-IMG-04).
    [[nodiscard]] base::Expected<ImageView> view(const Roi& roi) const;
    /// Ganzes Bild.
    [[nodiscard]] ImageView view() const;
    /// Kopie eines Ausschnitts als neuer Host-Puffer (Kachelausführung); Fehler wie view().
    [[nodiscard]] base::Expected<ImageBuffer> copyRegion(const Roi& roi) const;

private:
    ImageBuffer(ImageSpec spec, std::shared_ptr<const IStorage> storage)
        : spec_(spec)
        , storage_(std::move(storage))
    {}

    [[nodiscard]] static base::Expected<std::shared_ptr<HostStorage>> makeHostStorage(const ImageSpec& spec);

    ImageSpec spec_;
    std::shared_ptr<const IStorage> storage_;
};

/// Lesender Ausschnitt eines Host-Puffers. Hält den Puffer am Leben.
///
/// Abweichung von Architektur 9.2: hält den ImageBuffer als Wert (teilt den Speicher)
/// statt shared_ptr<const ImageBuffer>, weil ImageBuffer selbst ein leichter Wert ist.
class ImageView
{
public:
    ImageView() = default;

    [[nodiscard]] const Roi& roi() const noexcept { return roi_; }
    [[nodiscard]] int width() const noexcept { return roi_.w; }
    [[nodiscard]] int height() const noexcept { return roi_.h; }
    [[nodiscard]] int channels() const noexcept { return buf_.spec().channels; }
    [[nodiscard]] const ImageBuffer& buffer() const noexcept { return buf_; }

    /// Zeile y des Ausschnitts (0 = obere Zeile des Ausschnitts) als F32-Werte; leer bei
    /// anderem Format, Gerätespeicher oder y außerhalb.
    [[nodiscard]] std::span<const float> row(int y) const noexcept;

private:
    friend class ImageBuffer;
    ImageView(ImageBuffer buf, const Roi& roi)
        : buf_(std::move(buf))
        , roi_(roi)
    {}

    ImageBuffer buf_;
    Roi roi_;
};

template <class Fill>
base::Expected<ImageBuffer> ImageBuffer::allocateHost(const ImageSpec& spec, Fill&& fill)
{
    auto storage = makeHostStorage(spec);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    std::forward<Fill>(fill)((*storage)->bytes());
    return ImageBuffer(spec, std::move(*storage));
}

} // namespace autoneg::image
