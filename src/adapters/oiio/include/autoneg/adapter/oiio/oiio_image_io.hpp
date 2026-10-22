#pragma once

#include "autoneg/ports/image_io.hpp"

#include <vector>

namespace autoneg::adapter::oiio {

/// Lesen von TIFF, JPEG und PNG mit OpenImageIO (Architektur 9.9, LF-IMP-05).
/// Ergebnis F32 im Farbraum der Datei (ColorSpace::Source), Ganzzahlformate auf 0…1 normiert;
/// ein oder drei Kanäle (Alpha wird verworfen). Threadsicher (jeder Aufruf öffnet die Datei neu).
class OiioImageReader final : public ports::IImageReader
{
public:
    [[nodiscard]] base::Expected<model::SourceInfo> probe(const std::filesystem::path& p) override;
    [[nodiscard]] base::Expected<image::ImageBuffer> read(const std::filesystem::path& p) override;
    [[nodiscard]] base::Expected<ports::IccProfile> profile(const std::filesystem::path& p) override;
};

/// Schreiben von Exportdateien mit OpenImageIO (Architektur 9.9, LF-EXP-01…08).
///
/// write schreibt in eine temporäre Datei im Zielordner und benennt sie erst nach dem
/// vollständigen Schreiben um (LF-BAT-09); bei einem Fehler bleibt das Ziel unverändert.
/// Bittiefen: TIFF 8/16/32 (32 = float), PNG 8/16, JPEG 8, JPEG XL 8/16.
/// LinearDng wird nicht angeboten (L-04). Metadata::xmp wird noch nicht geschrieben (K-M4-04).
/// Threadsicher.
class OiioImageWriter final : public ports::IImageWriter
{
public:
    OiioImageWriter();

    [[nodiscard]] std::span<const ports::FileFormat> formats() const override;
    [[nodiscard]] base::Expected<void> write(const std::filesystem::path& p, const image::ImageBuffer& img,
                                             const ports::WriteOptions& o) override;
    [[nodiscard]] base::Expected<base::Bytes> encode(const image::ImageBuffer& img,
                                                     const ports::WriteOptions& o) override;

private:
    std::vector<ports::FileFormat> formats_;
};

} // namespace autoneg::adapter::oiio
