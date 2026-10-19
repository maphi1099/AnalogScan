#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/film.hpp"

#include <filesystem>

namespace autoneg::ports {

/// Demosaicing-Verfahren; Auto wählt je Sensor (Bayer, X-Trans) das Standardverfahren.
enum class Demosaic
{
    Auto,
    Ahd,
    Dcb,
    Dht,
    Aahd
};

struct DecodeOptions
{
    bool half = false; ///< halbe Auflösung ohne Demosaicing (Analyse, Miniaturen)
    Demosaic demosaic = Demosaic::Auto;
    bool linear = true; ///< ohne Tonkurve und Helligkeitsautomatik (LF-IMP-06)

    friend bool operator==(const DecodeOptions&, const DecodeOptions&) = default;
};

/// RAW-Dekodierung (LF-IMP-01..-06). Adapter: LibRawDecoder.
/// Threadsicher: beliebig viele gleichzeitige Aufrufe.
class IRawDecoder
{
public:
    virtual ~IRawDecoder() = default;

    /// Kopfdaten ohne Dekodierung. Nicht unterstützte Kameras: Fehler Unsupported mit
    /// Kameramodell und Abhilfe in der Meldung (LF-IMP-03).
    [[nodiscard]] virtual base::Expected<model::SourceInfo> probe(const std::filesystem::path& p) = 0;
    /// Linear dekodiertes Bild (ColorSpace::CameraLinear oder WorkingLinear, F32).
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> decode(const std::filesystem::path& p,
                                                                    const DecodeOptions& o) = 0;
    /// Eingebettetes Vorschaubild (schnell, für Miniaturen).
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> preview(const std::filesystem::path& p) = 0;
};

} // namespace autoneg::ports
