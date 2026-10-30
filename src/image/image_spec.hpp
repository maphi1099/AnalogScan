#pragma once

#include <compare>
#include <cstddef>

namespace autoneg::image {

/// Datentyp eines Abtastwerts. Gerechnet wird in F32 (LF-KON-07); die übrigen Formate
/// kommen nur an den Rändern vor (Dekodierung, Export, Anzeige).
enum class PixelFormat
{
    F32,
    F16,
    U16,
    U8
};

/// Farbraum bzw. Bedeutung der Werte.
enum class ColorSpace
{
    CameraLinear,  ///< linear im Kamerafarbraum (nach Dekodierung)
    WorkingLinear, ///< linear Rec. 2020, Arbeitsfarbraum (AE-11)
    Density,       ///< Dichte D = -log10 T
    Display,       ///< Monitorprofil
    Output,        ///< Ausgabeprofil des Exports
    Source         ///< Farbraum einer gelesenen Datei (Profil: IImageReader::profile)
};

/// Speicherort eines Puffers (NF-ARC-06).
enum class MemoryLocation
{
    Host,
    Device
};

[[nodiscard]] std::size_t bytesPerSample(PixelFormat format) noexcept;

/// Beschreibung eines Bildes. Kanäle liegen verschachtelt (RGBRGB…), Zeilen ohne Auffüllung.
struct ImageSpec
{
    int width = 0;
    int height = 0;
    int channels = 0;
    PixelFormat format = PixelFormat::F32;
    ColorSpace space = ColorSpace::WorkingLinear;

    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0 && channels > 0; }
    [[nodiscard]] std::size_t pixelCount() const noexcept;
    [[nodiscard]] std::size_t samplesPerRow() const noexcept;
    [[nodiscard]] std::size_t rowBytes() const noexcept;
    [[nodiscard]] std::size_t sizeBytes() const noexcept;

    friend bool operator==(const ImageSpec&, const ImageSpec&) = default;
};

/// Rechteckiger Bildausschnitt in Pixeln.
struct Roi
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] bool empty() const noexcept { return w <= 0 || h <= 0; }
    [[nodiscard]] std::size_t area() const noexcept;
    /// Liegt vollständig in einem Bild der Größe width x height.
    [[nodiscard]] bool inside(int width, int height) const noexcept;
    [[nodiscard]] bool contains(int px, int py) const noexcept;
    /// Schnittmenge; leer, wenn sich die Bereiche nicht überlappen.
    [[nodiscard]] Roi intersect(const Roi& other) const noexcept;

    friend auto operator<=>(const Roi&, const Roi&) = default;
};

} // namespace autoneg::image
