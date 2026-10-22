#include "autoneg/adapter/libraw/libraw_decoder.hpp"

#include <libraw/libraw.h>

#include <algorithm>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <vector>

namespace autoneg::adapter::libraw {

namespace {

constexpr int outputRec2020 = 8; ///< LibRaw output_color: 8 = Rec. 2020

std::string describe(const std::filesystem::path& p)
{
    return p.filename().string();
}

/// Fehler von LibRaw in einen Projektfehler mit verständlicher Meldung übersetzen (LF-IMP-03).
base::Error errorOf(int code, const std::filesystem::path& p, const LibRaw* raw)
{
    std::string camera;
    if (raw != nullptr && raw->imgdata.idata.make[0] != '\0') {
        camera = std::string(raw->imgdata.idata.make) + " " + raw->imgdata.idata.model;
    }
    const std::string what = describe(p) + (camera.empty() ? "" : " (" + camera + ")");
    switch (code) {
    case LIBRAW_FILE_UNSUPPORTED:
        return {base::ErrorCode::Unsupported,
                what + ": Dateiformat oder Kamera wird von LibRaw " + LibRaw::version() +
                    " nicht unterstützt. Abhilfe: mit dem Adobe DNG Converter in DNG umwandeln."};
    case LIBRAW_NOT_IMPLEMENTED:
        return {base::ErrorCode::Unsupported,
                what + ": Kompressionsvariante wird nicht unterstützt (" + libraw_strerror(code) +
                    "). Abhilfe: mit dem Adobe DNG Converter in DNG umwandeln."};
    case LIBRAW_UNSUFFICIENT_MEMORY:
    case LIBRAW_TOO_BIG:
        return {base::ErrorCode::Unsupported, what + ": Bild zu groß für den verfügbaren Speicher"};
    case LIBRAW_IO_ERROR: // vorhandene, lesbare Datei: meist zu kurz oder kein RAW (Inhalt, nicht
                          // Datenträger)
        return {base::ErrorCode::Corrupt,
                what + ": Datei zu kurz oder keine RAW-Datei (" + std::string(libraw_strerror(code)) + ")"};
    case LIBRAW_CANCELLED_BY_CALLBACK:
        return {base::ErrorCode::Cancelled, what + ": abgebrochen"};
    default:
        return {base::ErrorCode::Corrupt,
                what + ": Datei beschädigt oder unvollständig (" + std::string(libraw_strerror(code)) + ")"};
    }
}

int demosaicOf(ports::Demosaic d)
{
    switch (d) {
    case ports::Demosaic::Auto:
        return -1; // Standard je Sensor (Bayer: AHD; X-Trans: eigenes Verfahren)
    case ports::Demosaic::Ahd:
        return 3;
    case ports::Demosaic::Dcb:
        return 4;
    case ports::Demosaic::Dht:
        return 11;
    case ports::Demosaic::Aahd:
        return 12;
    }
    return -1;
}

image::ImageBuffer fromProcessed(const libraw_processed_image_t& img, image::ColorSpace space)
{
    const int channels = img.colors == 1 ? 1 : 3;
    const std::size_t pixels = static_cast<std::size_t>(img.width) * img.height;
    std::vector<float> v(pixels * static_cast<std::size_t>(channels));
    if (img.bits == 16) {
        const auto* src = reinterpret_cast<const std::uint16_t*>(img.data);
        for (std::size_t i = 0; i < pixels; ++i) {
            for (int c = 0; c < channels; ++c) {
                v[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] =
                    static_cast<float>(src[i * img.colors + static_cast<std::size_t>(c)]) / 65535.0F;
            }
        }
    } else {
        const unsigned char* src = img.data;
        for (std::size_t i = 0; i < pixels; ++i) {
            for (int c = 0; c < channels; ++c) {
                v[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] =
                    static_cast<float>(src[i * img.colors + static_cast<std::size_t>(c)]) / 255.0F;
            }
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{img.width, img.height, channels, image::PixelFormat::F32, space}, v)
        .value();
}

} // namespace

/// Pool von LibRaw-Instanzen (eine Instanz ist nicht threadsicher; Architektur 6).
struct LibRawDecoder::Pool
{
    std::mutex mutex;
    std::vector<std::unique_ptr<LibRaw>> idle;

    /// Leiht eine Instanz; die Rückgabe erfolgt im Destruktor des Leihscheins.
    struct Lease
    {
        Pool* pool;
        std::unique_ptr<LibRaw> raw;
        ~Lease()
        {
            if (raw) {
                raw->recycle();
                const std::scoped_lock lock(pool->mutex);
                pool->idle.push_back(std::move(raw));
            }
        }
        Lease(Pool* p, std::unique_ptr<LibRaw> r)
            : pool(p)
            , raw(std::move(r))
        {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&&) = delete;
        Lease& operator=(Lease&&) = delete;
        LibRaw* operator->() const { return raw.get(); }
    };

    std::unique_ptr<LibRaw> take()
    {
        {
            const std::scoped_lock lock(mutex);
            if (!idle.empty()) {
                auto r = std::move(idle.back());
                idle.pop_back();
                return r;
            }
        }
        return std::make_unique<LibRaw>(0U);
    }
};

LibRawDecoder::LibRawDecoder()
    : pool_(std::make_unique<Pool>())
{}

LibRawDecoder::~LibRawDecoder() = default;

namespace {

/// Datei öffnen; prüft vorher Existenz und Größe, damit die Meldungen eindeutig sind.
base::Expected<void> openFile(LibRaw& raw, const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return base::makeError(base::ErrorCode::NotFound, describe(p) + ": Datei nicht gefunden");
    }
    if (std::filesystem::file_size(p, ec) == 0 || ec) {
        return base::makeError(base::ErrorCode::Corrupt, describe(p) + ": Datei ist leer");
    }
#if defined(_WIN32)
    const int rc = raw.open_file(p.wstring().c_str()); // Umlaute im Pfad (PROGRESS: plattformabhängig)
#else
    const int rc = raw.open_file(p.string().c_str());
#endif
    if (rc != LIBRAW_SUCCESS) {
        return std::unexpected(errorOf(rc, p, &raw));
    }
    return {};
}

/// Abgeschnittene Dateien erkennen: LibRaw füllt fehlende Bilddaten sonst stillschweigend auf
/// (error_count bleibt 0). Die angegebene Datenlänge kann Füllbytes enthalten (RAF: 16 Byte über
/// dem Dateiende), daher erst ein Fehlen von mehr als 1 % bzw. 64 KB als unvollständig werten.
base::Expected<void> checkComplete(LibRaw& raw, const std::filesystem::path& p)
{
    const auto& u = raw.get_internal_data_pointer()->unpacker_data;
    std::error_code ec;
    const auto size = static_cast<INT64>(std::filesystem::file_size(p, ec));
    const INT64 slack = std::max<INT64>(INT64{65536}, u.data_size / 100);
    if (!ec && u.data_size > 0 && u.data_offset + u.data_size > size + slack) {
        return base::makeError(base::ErrorCode::Corrupt,
                               describe(p) +
                                   ": Datei unvollständig (Bilddaten reichen über das Dateiende hinaus: " +
                                   std::to_string(u.data_offset) + " + " + std::to_string(u.data_size) +
                                   " > " + std::to_string(size) + ")");
    }
    return {};
}

model::SourceInfo infoOf(const LibRaw& raw)
{
    const auto& s = raw.imgdata.sizes;
    const bool swap = s.flip == 5 || s.flip == 6;
    model::SourceInfo info;
    info.width = swap ? s.height : s.width;
    info.height = swap ? s.width : s.height;
    info.make = raw.imgdata.idata.make;
    info.model = raw.imgdata.idata.model;
    info.format = raw.imgdata.idata.dng_version != 0 ? model::SourceFormat::Dng : model::SourceFormat::Raw;
    return info;
}

} // namespace

base::Expected<model::SourceInfo> LibRawDecoder::probe(const std::filesystem::path& p)
{
    try {
        const Pool::Lease raw(pool_.get(), pool_->take());
        if (auto ok = openFile(*raw.raw, p); !ok) {
            return std::unexpected(ok.error());
        }
        return infoOf(*raw.raw);
    } catch (const std::bad_alloc&) {
        return base::makeError(base::ErrorCode::Unsupported, describe(p) + ": zu wenig Speicher");
    }
}

base::Expected<image::ImageBuffer> LibRawDecoder::decode(const std::filesystem::path& p,
                                                         const ports::DecodeOptions& o)
{
    try {
        const Pool::Lease raw(pool_.get(), pool_->take());
        if (auto ok = openFile(*raw.raw, p); !ok) {
            return std::unexpected(ok.error());
        }
        libraw_output_params_t& params = raw->imgdata.params;
        params.output_bps = 16;
        params.output_color = outputRec2020;
        params.use_camera_wb = 1; // vor der Farbmatrix (K-M3-15)
        params.use_auto_wb = 0;
        params.half_size = o.half ? 1 : 0;
        params.user_qual = demosaicOf(o.demosaic);
        params.highlight = 0; // beschneiden, keine Rekonstruktion
        if (o.linear) {
            params.gamm[0] = 1.0;
            params.gamm[1] = 1.0;
            params.no_auto_bright = 1;
        }
        if (auto complete = checkComplete(*raw.raw, p); !complete) {
            return std::unexpected(complete.error());
        }
        if (const int rc = raw->unpack(); rc != LIBRAW_SUCCESS) {
            return std::unexpected(errorOf(rc, p, raw.raw.get()));
        }
        if (raw->error_count() > 0) { // Lesefehler beim Entpacken, z. B. Dateiende erreicht
            return base::makeError(base::ErrorCode::Corrupt,
                                   describe(p) + ": Bilddaten unvollständig oder beschädigt");
        }
        if (const int rc = raw->dcraw_process(); rc != LIBRAW_SUCCESS) {
            return std::unexpected(errorOf(rc, p, raw.raw.get()));
        }
        int rc = LIBRAW_SUCCESS;
        libraw_processed_image_t* img = raw->dcraw_make_mem_image(&rc);
        if (img == nullptr) {
            return std::unexpected(errorOf(rc, p, raw.raw.get()));
        }
        image::ImageBuffer out = fromProcessed(*img, image::ColorSpace::WorkingLinear);
        LibRaw::dcraw_clear_mem(img);
        return out;
    } catch (const std::bad_alloc&) {
        return base::makeError(base::ErrorCode::Unsupported, describe(p) + ": zu wenig Speicher");
    }
}

base::Expected<image::ImageBuffer> LibRawDecoder::preview(const std::filesystem::path& p)
{
    try {
        {
            const Pool::Lease raw(pool_.get(), pool_->take());
            if (auto ok = openFile(*raw.raw, p); !ok) {
                return std::unexpected(ok.error());
            }
            if (raw->unpack_thumb() == LIBRAW_SUCCESS) {
                int rc = LIBRAW_SUCCESS;
                libraw_processed_image_t* thumb = raw->dcraw_make_mem_thumb(&rc);
                if (thumb != nullptr) {
                    std::optional<image::ImageBuffer> out;
                    if (thumb->type == LIBRAW_IMAGE_BITMAP && (thumb->colors == 1 || thumb->colors == 3)) {
                        out = fromProcessed(*thumb, image::ColorSpace::Source);
                    }
                    LibRaw::dcraw_clear_mem(thumb);
                    if (out) {
                        return *out;
                    }
                }
            }
        }
        ports::DecodeOptions half;
        half.half = true;
        return decode(p, half);
    } catch (const std::bad_alloc&) {
        return base::makeError(base::ErrorCode::Unsupported, describe(p) + ": zu wenig Speicher");
    }
}

} // namespace autoneg::adapter::libraw
