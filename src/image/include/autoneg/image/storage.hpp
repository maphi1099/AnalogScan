#pragma once

#include "autoneg/image/image_spec.hpp"

#include <cstddef>
#include <new>
#include <span>
#include <string>
#include <vector>

namespace autoneg::image {

/// Speicher eines Bildpuffers (NF-ARC-06). Implementierungen sind nach der Erzeugung
/// unveränderlich und damit threadsicher lesbar.
class IStorage
{
public:
    virtual ~IStorage() = default;

    [[nodiscard]] virtual MemoryLocation location() const noexcept = 0;
    [[nodiscard]] virtual std::size_t sizeBytes() const noexcept = 0;
    /// Inhalt im Hauptspeicher; leer bei Gerätespeicher.
    [[nodiscard]] virtual std::span<const std::byte> hostBytes() const noexcept { return {}; }
    /// Inhalt als float-Feld im Hauptspeicher; leer bei Gerätespeicher.
    [[nodiscard]] virtual std::span<const float> hostFloats() const noexcept { return {}; }

protected:
    IStorage() = default;
    IStorage(const IStorage&) = default;
    IStorage& operator=(const IStorage&) = default;
    IStorage(IStorage&&) = default;
    IStorage& operator=(IStorage&&) = default;
};

/// Allokator mit fester Ausrichtung (SIMD, Cache-Zeilen).
template <class T, std::size_t Alignment>
struct AlignedAllocator
{
    using value_type = T;

    template <class U>
    struct rebind
    {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;
    template <class U>
    explicit AlignedAllocator(const AlignedAllocator<U, Alignment>& /*other*/) noexcept
    {}

    [[nodiscard]] T* allocate(std::size_t n)
    {
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
    }
    void deallocate(T* p, std::size_t /*n*/) noexcept { ::operator delete(p, std::align_val_t{Alignment}); }

    template <class U>
    friend bool operator==(const AlignedAllocator& /*a*/,
                           const AlignedAllocator<U, Alignment>& /*b*/) noexcept
    {
        return true;
    }
};

/// Ausrichtung der Host-Puffer in Byte.
inline constexpr std::size_t hostAlignment = 64;

/// Bildspeicher im Hauptspeicher, 64-Byte-ausgerichtet.
///
/// Wird vor dem Einpacken in einen ImageBuffer beschrieben; danach nur noch gelesen.
class HostStorage final : public IStorage
{
public:
    explicit HostStorage(std::size_t sizeBytes);

    [[nodiscard]] MemoryLocation location() const noexcept override { return MemoryLocation::Host; }
    [[nodiscard]] std::size_t sizeBytes() const noexcept override { return sizeBytes_; }
    [[nodiscard]] std::span<const std::byte> hostBytes() const noexcept override { return bytes(); }
    [[nodiscard]] std::span<const float> hostFloats() const noexcept override { return floats(); }

    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    /// Als float-Feld; Länge = sizeBytes / 4 (abgerundet).
    [[nodiscard]] std::span<float> floats() noexcept;
    [[nodiscard]] std::span<const float> floats() const noexcept;

private:
    // float als Elementtyp: richtige Ausrichtung und erlaubter Zugriff als float oder Bytes
    std::vector<float, AlignedAllocator<float, hostAlignment>> data_;
    std::size_t sizeBytes_ = 0;
};

/// Bildspeicher eines Rechen-Backends (z. B. Grafikspeicher). Ableitung im Backend.
class DeviceStorage : public IStorage
{
public:
    [[nodiscard]] MemoryLocation location() const noexcept override { return MemoryLocation::Device; }
    /// Kennung des Backends, dem der Speicher gehört.
    [[nodiscard]] virtual std::string backendId() const = 0;
};

} // namespace autoneg::image
