#pragma once

#include "autoneg/ports/raw_decoder.hpp"

#include <memory>

namespace autoneg::adapter::libraw {

/// RAW-Dekodierung mit LibRaw (Architektur 9.9, LF-IMP-01…06).
///
/// Ausgabe: linear (Gamma 1, ohne Helligkeitsautomatik), Weißabgleich der Aufnahme vor der
/// Farbmatrix (K-M3-15), direkt im Arbeitsfarbraum Rec. 2020 (ColorSpace::WorkingLinear), F32,
/// 0…1 bezogen auf den Weißpunkt des Sensors. Die Ausrichtung der Kamera (Flip) wird angewandt.
///
/// Eine LibRaw-Instanz ist nicht threadsicher; der Decoder hält einen Pool von Instanzen,
/// jeder Aufruf leiht sich eine. Damit ist der Decoder threadsicher.
class LibRawDecoder final : public ports::IRawDecoder
{
public:
    LibRawDecoder();
    ~LibRawDecoder() override;
    LibRawDecoder(const LibRawDecoder&) = delete;
    LibRawDecoder& operator=(const LibRawDecoder&) = delete;
    LibRawDecoder(LibRawDecoder&&) = delete;
    LibRawDecoder& operator=(LibRawDecoder&&) = delete;

    [[nodiscard]] base::Expected<model::SourceInfo> probe(const std::filesystem::path& p) override;
    [[nodiscard]] base::Expected<image::ImageBuffer> decode(const std::filesystem::path& p,
                                                            const ports::DecodeOptions& o) override;
    /// Eingebettetes Vorschaubild, wenn es als Bitmap vorliegt; JPEG-Vorschauen kann der
    /// Adapter nicht dekodieren (eine Bibliothek je Adapter), dann halbe Auflösung ohne
    /// Demosaicing (K-M4-03).
    [[nodiscard]] base::Expected<image::ImageBuffer> preview(const std::filesystem::path& p) override;

private:
    struct Pool;
    std::unique_ptr<Pool> pool_;
};

} // namespace autoneg::adapter::libraw
