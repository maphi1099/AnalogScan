#include "autoneg/adapter/oiio/oiio_image_io.hpp"

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <system_error>

namespace autoneg::adapter::oiio {

namespace {

/// OpenImageIO erwartet Dateinamen in UTF-8 (auch unter Windows).
std::string utf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}

std::string describe(const std::filesystem::path& p)
{
    return p.filename().string();
}

model::SourceFormat formatOf(std::string_view name)
{
    if (name == "tiff") {
        return model::SourceFormat::Tiff;
    }
    if (name == "jpeg") {
        return model::SourceFormat::Jpeg;
    }
    if (name == "png") {
        return model::SourceFormat::Png;
    }
    return model::SourceFormat::Other;
}

base::Expected<std::unique_ptr<OIIO::ImageInput>> openInput(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return base::makeError(base::ErrorCode::NotFound, describe(p) + ": Datei nicht gefunden");
    }
    if (std::filesystem::file_size(p, ec) == 0 || ec) {
        return base::makeError(base::ErrorCode::Corrupt, describe(p) + ": Datei ist leer");
    }
    // Kein eigener Thread-Pool von OpenImageIO: die Anwendung steuert die Nebenläufigkeit
    // (oneTBB, JobScheduler); der Pool von OIIO meldet unter ThreadSanitizer Rennen (B-19).
    auto in = OIIO::ImageInput::open(utf8(p));
    if (in) {
        in->threads(1);
    }
    if (!in) {
        const std::string why = OIIO::geterror();
        const bool unknown = why.find("format") != std::string::npos && why.find("not") != std::string::npos;
        return base::makeError(unknown ? base::ErrorCode::Unsupported : base::ErrorCode::Corrupt,
                               describe(p) + ": Datei nicht lesbar (" + why + ")");
    }
    return in;
}

const char* formatName(ports::FileFormat f)
{
    switch (f) {
    case ports::FileFormat::Tiff:
        return "tiff";
    case ports::FileFormat::Jpeg:
        return "jpeg";
    case ports::FileFormat::Png:
        return "png";
    case ports::FileFormat::JpegXl:
        return "jpegxl";
    case ports::FileFormat::LinearDng:
        return "dng";
    }
    return "";
}

const char* extensionOf(ports::FileFormat f)
{
    switch (f) {
    case ports::FileFormat::Tiff:
        return ".tif";
    case ports::FileFormat::Jpeg:
        return ".jpg";
    case ports::FileFormat::Png:
        return ".png";
    case ports::FileFormat::JpegXl:
        return ".jxl";
    case ports::FileFormat::LinearDng:
        return ".dng";
    }
    return "";
}

base::Expected<OIIO::TypeDesc> typeOf(const ports::WriteOptions& o)
{
    using F = ports::FileFormat;
    const int b = o.bitDepth;
    const bool ok = (o.format == F::Tiff && (b == 8 || b == 16 || b == 32)) ||
                    (o.format == F::Png && (b == 8 || b == 16)) || (o.format == F::Jpeg && b == 8) ||
                    (o.format == F::JpegXl && (b == 8 || b == 16));
    if (!ok) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Bittiefe " + std::to_string(b) +
                                                                     " wird für " + formatName(o.format) +
                                                                     " nicht unterstützt");
    }
    return b == 8    ? OIIO::TypeDesc(OIIO::TypeDesc::UINT8)
           : b == 16 ? OIIO::TypeDesc(OIIO::TypeDesc::UINT16)
                     : OIIO::TypeDesc(OIIO::TypeDesc::FLOAT);
}

/// Beschreibung der Ausgabedatei mit Kompression, Profil und Metadaten.
OIIO::ImageSpec specOf(const image::ImageBuffer& img, const ports::WriteOptions& o, OIIO::TypeDesc type)
{
    OIIO::ImageSpec spec(img.spec().width, img.spec().height, img.spec().channels, type);
    const int q = std::clamp(o.quality, 1, 100);
    switch (o.format) {
    case ports::FileFormat::Tiff:
        spec.attribute("compression", o.compression == ports::Compression::Lzw       ? "lzw"
                                      : o.compression == ports::Compression::Deflate ? "zip"
                                                                                     : "none");
        break;
    case ports::FileFormat::Jpeg:
        spec.attribute("compression", "jpeg:" + std::to_string(q));
        break;
    case ports::FileFormat::Png:
        spec.attribute("png:compressionLevel", o.compression == ports::Compression::None ? 0 : 6);
        break;
    case ports::FileFormat::JpegXl:
        if (o.compression == ports::Compression::Lossy) {
            spec.attribute("compression", "jpegxl:" + std::to_string(q));
        } else {
            spec.attribute("compression", "jpegxl:100"); // verlustfrei
        }
        break;
    case ports::FileFormat::LinearDng:
        break;
    }
    if (!o.icc.bytes.empty()) {
        spec.attribute("ICCProfile",
                       OIIO::TypeDesc(OIIO::TypeDesc::UINT8, static_cast<int>(o.icc.bytes.size())),
                       o.icc.bytes.data());
    }
    for (const auto& [key, value] : o.meta.exif) {
        spec.attribute(key, value);
    }
    spec.attribute("Software", "AutoNeg");
    return spec;
}

base::Expected<void> checkImage(const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    if (img.empty() || s.format != image::PixelFormat::F32 || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Export erwartet F32 im Hauptspeicher");
    }
    if (s.channels != 1 && s.channels != 3) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Export erwartet 1 oder 3 Kanäle");
    }
    return {};
}

/// Schreibt img mit out (bereits erzeugt) unter name; ioproxy optional für den Speicher.
base::Expected<void> writeWith(OIIO::ImageOutput& out, const std::string& name, const image::ImageBuffer& img,
                               OIIO::ImageSpec spec)
{
    if (!out.open(name, spec)) {
        return base::makeError(base::ErrorCode::Io, "Datei nicht anlegbar (" + out.geterror() + ")");
    }
    const bool written = out.write_image(OIIO::TypeDesc::FLOAT, img.host().data());
    const bool closed = out.close();
    if (!written || !closed) {
        return base::makeError(base::ErrorCode::Io, "Schreiben fehlgeschlagen (" + out.geterror() + ")");
    }
    return {};
}

std::atomic<unsigned> tempCounter{0};

} // namespace

base::Expected<model::SourceInfo> OiioImageReader::probe(const std::filesystem::path& p)
{
    auto in = openInput(p);
    if (!in) {
        return std::unexpected(in.error());
    }
    const OIIO::ImageSpec& spec = (*in)->spec();
    model::SourceInfo info;
    info.width = spec.width;
    info.height = spec.height;
    info.make = spec.get_string_attribute("Make");
    info.model = spec.get_string_attribute("Model");
    info.format = formatOf((*in)->format_name());
    return info;
}

base::Expected<image::ImageBuffer> OiioImageReader::read(const std::filesystem::path& p)
{
    auto in = openInput(p);
    if (!in) {
        return std::unexpected(in.error());
    }
    const OIIO::ImageSpec& spec = (*in)->spec();
    const int channels = spec.nchannels >= 3 ? 3 : 1;
    std::vector<float> v(static_cast<std::size_t>(spec.width) * static_cast<std::size_t>(spec.height) *
                         static_cast<std::size_t>(channels));
    // has_error: OpenImageIO meldet manche Lesefehler (abgeschnittene Streifen) nur dort
    if (!(*in)->read_image(0, 0, 0, channels, OIIO::TypeDesc::FLOAT, v.data()) || (*in)->has_error()) {
        return base::makeError(base::ErrorCode::Corrupt,
                               describe(p) + ": Bilddaten nicht lesbar (" + (*in)->geterror() + ")");
    }
    return image::ImageBuffer::fromFloats(image::ImageSpec{spec.width, spec.height, channels,
                                                           image::PixelFormat::F32,
                                                           image::ColorSpace::Source},
                                          v);
}

base::Expected<ports::IccProfile> OiioImageReader::profile(const std::filesystem::path& p)
{
    auto in = openInput(p);
    if (!in) {
        return std::unexpected(in.error());
    }
    ports::IccProfile icc;
    const OIIO::ParamValue* attr = (*in)->spec().find_attribute("ICCProfile");
    if (attr != nullptr) {
        const auto* data = static_cast<const std::uint8_t*>(attr->data());
        icc.bytes.assign(data, data + attr->datasize());
        icc.name = (*in)->spec().get_string_attribute("ICCProfile:profile_description");
    }
    return icc;
}

OiioImageWriter::OiioImageWriter()
{
    const std::string list = OIIO::get_string_attribute("format_list");
    const auto has = [&list](std::string_view name) { return list.find(name) != std::string::npos; };
    for (const ports::FileFormat f : {ports::FileFormat::Tiff, ports::FileFormat::Jpeg,
                                      ports::FileFormat::Png, ports::FileFormat::JpegXl}) {
        if (has(formatName(f))) {
            formats_.push_back(f);
        }
    }
}

std::span<const ports::FileFormat> OiioImageWriter::formats() const
{
    return formats_;
}

base::Expected<void> OiioImageWriter::write(const std::filesystem::path& p, const image::ImageBuffer& img,
                                            const ports::WriteOptions& o)
{
    if (auto ok = checkImage(img); !ok) {
        return ok;
    }
    if (std::ranges::find(formats_, o.format) == formats_.end()) {
        return base::makeError(base::ErrorCode::Unsupported,
                               std::string("Format ") + formatName(o.format) + " wird nicht unterstützt");
    }
    const auto type = typeOf(o);
    if (!type) {
        return std::unexpected(type.error());
    }
    auto out = OIIO::ImageOutput::create(formatName(o.format));
    if (out) {
        out->threads(1); // siehe openInput
    }
    if (!out) {
        return base::makeError(base::ErrorCode::Unsupported, OIIO::geterror());
    }
    // Temporäre Datei im Zielordner (gleiches Laufwerk, damit das Umbenennen atomar ist)
    std::filesystem::path tmp = p;
    tmp += ".autoneg-" + std::to_string(tempCounter.fetch_add(1)) + ".tmp" + extensionOf(o.format);
    auto written = writeWith(*out, utf8(tmp), img, specOf(img, o, *type));
    std::error_code ec;
    if (!written) {
        std::filesystem::remove(tmp, ec);
        return base::makeError(written.error().code, describe(p) + ": " + written.error().message);
    }
    std::filesystem::rename(tmp, p, ec); // ersetzt eine vorhandene Datei (PROGRESS: plattformabhängig)
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return base::makeError(base::ErrorCode::Io,
                               describe(p) + ": Umbenennen fehlgeschlagen (" + ec.message() + ")");
    }
    return {};
}

base::Expected<base::Bytes> OiioImageWriter::encode(const image::ImageBuffer& img,
                                                    const ports::WriteOptions& o)
{
    if (auto ok = checkImage(img); !ok) {
        return std::unexpected(ok.error());
    }
    const auto type = typeOf(o);
    if (!type) {
        return std::unexpected(type.error());
    }
    auto out = OIIO::ImageOutput::create(formatName(o.format));
    if (out) {
        out->threads(1); // siehe openInput
    }
    if (!out || !out->supports("ioproxy")) {
        return base::makeError(base::ErrorCode::Unsupported, std::string("Format ") + formatName(o.format) +
                                                                 " kann nicht in den Speicher schreiben");
    }
    std::vector<unsigned char> buffer;
    OIIO::Filesystem::IOVecOutput proxy(buffer);
    OIIO::ImageSpec spec = specOf(img, o, *type);
    OIIO::Filesystem::IOProxy* ptr = &proxy;
    spec.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &ptr);
    if (auto ok = writeWith(*out, std::string("speicher") + extensionOf(o.format), img, spec); !ok) {
        return std::unexpected(ok.error());
    }
    return base::Bytes(buffer.begin(), buffer.end());
}

} // namespace autoneg::adapter::oiio
