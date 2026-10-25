#pragma once

#include <string>

namespace autoneg::model {

/// Filmtyp (LF-KON-01). Steuert über autoneg.filmmode, welche Stufen aktiv sind.
enum class FilmType
{
    ColorNegative, ///< Farbnegativ C-41
    BwNegative,    ///< S/W-Negativ
    Slide,         ///< Dia E-6
    Unconverted    ///< noch nicht festgelegt: der Scan wird unverändert gezeigt (K-M10-08)
};

/// Bewertung eines Bildes (LF-BAT-10).
enum class Rating
{
    None,
    Keep,
    Reject
};

/// Herkunft eines Eintrags der Bearbeitungsliste (LF-LST-05).
enum class Origin
{
    Manual,
    Automatic,
    Copied,
    RollDefault,
    Preset
};

/// Art der Quelldatei.
enum class SourceFormat
{
    Raw,
    Dng,
    Tiff,
    Jpeg,
    Png,
    Other
};

/// Filmangaben einer Rolle (Kontaktabzug, Export-Metadaten).
struct FilmInfo
{
    std::string filmName;
    int iso = 0;
    std::string developer;
    std::string camera;
    std::string shotDate;
    std::string notes;

    friend bool operator==(const FilmInfo&, const FilmInfo&) = default;
};

/// Eigenschaften der Quelldatei, ermittelt beim Öffnen (IRawDecoder::probe).
struct SourceInfo
{
    int width = 0;
    int height = 0;
    std::string make;
    std::string model;
    SourceFormat format = SourceFormat::Raw;

    friend bool operator==(const SourceInfo&, const SourceInfo&) = default;
};

} // namespace autoneg::model
