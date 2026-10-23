#pragma once

// Fakes der Ports für Bilddateien und Farbe: einfache, aber vertragstreue Implementierungen
// ohne Fremdbibliothek. Die Vertragstests (K-14) laufen gegen diese Fakes und gegen die echten
// Adapter; Unit-Tests können sie statt der gmock-Attrappen nutzen, wenn Verhalten statt
// Aufrufen zählt.

#include "autoneg/ports/color.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/raw_decoder.hpp"

#include <filesystem>
#include <vector>

namespace autoneg::mocks {

/// Schreibt img (F32, 3 Kanäle) als PFM; Testhilfe für FakeRawDecoder.
bool writePfm(const std::filesystem::path& p, const image::ImageBuffer& img);

/// RAW-Decoder über PFM-Dateien ("Sensordaten" linear Rec. 2020).
/// probe/decode/preview mit den Fehlern des Vertrags: fehlend NotFound, leer Corrupt,
/// kein PFM Unsupported. Halbe Auflösung als 2 × 2-Mittel (aufgerundete Größe).
class FakeRawDecoder final : public ports::IRawDecoder
{
public:
    [[nodiscard]] base::Expected<model::SourceInfo> probe(const std::filesystem::path& p) override;
    [[nodiscard]] base::Expected<image::ImageBuffer> decode(const std::filesystem::path& p,
                                                            const ports::DecodeOptions& o) override;
    [[nodiscard]] base::Expected<image::ImageBuffer> preview(const std::filesystem::path& p) override;
};

/// Schreiber eines einfachen Binärformats ("AUTONEG-FAKE", Kopf, float-Werte) für TIFF und PNG.
/// Bittiefen wie der echte Adapter (TIFF 8/16/32, PNG 8/16); temporäre Datei, dann umbenennen.
class FakeImageWriter final : public ports::IImageWriter
{
public:
    [[nodiscard]] std::span<const ports::FileFormat> formats() const override;
    [[nodiscard]] base::Expected<void> write(const std::filesystem::path& p, const image::ImageBuffer& img,
                                             const ports::WriteOptions& o) override;
    [[nodiscard]] base::Expected<base::Bytes> encode(const image::ImageBuffer& img,
                                                     const ports::WriteOptions& o) override;

private:
    std::vector<ports::FileFormat> formats_{ports::FileFormat::Tiff, ports::FileFormat::Png,
                                            ports::FileFormat::Jpeg};
};

/// Farbmanagement mit eigener Matrix- und Kurvenrechnung (Bradford, D50 als Verbindungsraum).
/// Profile sind kurze Kennungen ("AUTONEG-FAKE-ICC:<n>"); andere Bytes: Corrupt.
class FakeColorEngine final : public ports::IColorEngine
{
public:
    [[nodiscard]] base::Expected<ports::IccProfile> profile(ports::ProfileId id) override;
    [[nodiscard]] base::Expected<ports::IccProfile> display(int screen) override;
    [[nodiscard]] base::Expected<std::shared_ptr<const ports::IColorTransform>>
    transform(const ports::IccProfile& src, const ports::IccProfile& dst, ports::Intent intent) override;
};

} // namespace autoneg::mocks
