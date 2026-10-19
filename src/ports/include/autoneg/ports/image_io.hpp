#pragma once

#include "autoneg/base/bytes.hpp"
#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/film.hpp"

#include <filesystem>
#include <map>
#include <span>
#include <string>

namespace autoneg::ports {

/// Ausgabeformate (LF-EXP-01..-04).
enum class FileFormat
{
    Tiff,
    Jpeg,
    Png,
    JpegXl,
    LinearDng
};

enum class Compression
{
    None,
    Lzw,
    Deflate,
    Lossless, ///< z. B. JPEG XL verlustfrei
    Lossy
};

/// Eingebettetes Farbprofil.
struct IccProfile
{
    base::Bytes bytes;
    std::string name;

    friend bool operator==(const IccProfile&, const IccProfile&) = default;
};

/// Metadaten für den Export (LF-EXP-08).
struct Metadata
{
    std::map<std::string, std::string> exif;
    std::string xmp;

    friend bool operator==(const Metadata&, const Metadata&) = default;
};

struct WriteOptions
{
    FileFormat format = FileFormat::Tiff;
    int bitDepth = 16;
    int quality = 90; ///< 1..100 bei verlustbehafteten Formaten
    Compression compression = Compression::None;
    IccProfile icc;
    Metadata meta;

    friend bool operator==(const WriteOptions&, const WriteOptions&) = default;
};

/// Lesen von TIFF/JPEG/PNG (LF-IMP-05). Adapter: OiioImageReader. Threadsicher.
class IImageReader
{
public:
    virtual ~IImageReader() = default;

    [[nodiscard]] virtual base::Expected<model::SourceInfo> probe(const std::filesystem::path& p) = 0;
    /// Bild als F32 im Farbraum der Datei (Umrechnung in den Arbeitsfarbraum: IColorEngine).
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> read(const std::filesystem::path& p) = 0;
};

/// Schreiben von Exportdateien. Adapter: OiioImageWriter. Threadsicher.
/// write schreibt zuerst in eine temporäre Datei und benennt erst am Ende um, sodass ein
/// Abbruch keine halbe Datei hinterlässt (LF-BAT-09).
class IImageWriter
{
public:
    virtual ~IImageWriter() = default;

    [[nodiscard]] virtual std::span<const FileFormat> formats() const = 0;
    [[nodiscard]] virtual base::Expected<void>
    write(const std::filesystem::path& p, const image::ImageBuffer& img, const WriteOptions& o) = 0;
    [[nodiscard]] virtual base::Expected<base::Bytes> encode(const image::ImageBuffer& img,
                                                             const WriteOptions& o) = 0;
};

} // namespace autoneg::ports
