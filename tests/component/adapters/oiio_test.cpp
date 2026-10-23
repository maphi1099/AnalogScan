// Komponententests OiioImageReader/OiioImageWriter mit echter OpenImageIO (K-03, K-04, K-05).
// K-05 prüft die geschriebenen Dateien mit unabhängigen Lesern (Python: tifffile, Pillow,
// imagecodecs) über tests/component/adapters/verify_export.py.

#include "test_files.hpp"

#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>
#include <memory>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace autoneg::adapter::oiio {
namespace {

namespace fs = std::filesystem;
namespace files = autoneg::testing::files;
namespace t = autoneg::testing;

float sampleAt(const image::ImageBuffer& img, int x, int y, int c)
{
    const auto& s = img.spec();
    return img.host()[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                       static_cast<std::size_t>(x)) *
                          static_cast<std::size_t>(s.channels) +
                      static_cast<std::size_t>(c)];
}

// LF-IMP-05
TEST(OiioComponent, K_04_ReadTiffPngJpegWithAndWithoutProfile)
{
    OiioImageReader reader;
    struct Case
    {
        const char* file;
        int channels;
        int bits;
        bool icc;
        double tolerance; // in Stufen der Bittiefe
        model::SourceFormat format;
    };
    const Case cases[] = {
        {"rgb8_srgb.tif", 3, 8, true, 0.0, model::SourceFormat::Tiff},
        {"rgb16.tif", 3, 16, false, 0.0, model::SourceFormat::Tiff},
        {"gray16.tif", 1, 16, false, 0.0, model::SourceFormat::Tiff},
        {"rgb16.png", 3, 16, false, 0.0, model::SourceFormat::Png},
        {"rgb8_icc.jpg", 3, 8, true, 3.0, model::SourceFormat::Jpeg},
        {"rgb8.jpg", 3, 8, false, 3.0, model::SourceFormat::Jpeg},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.file);
        const fs::path p = files::dataDir() / c.file;
        const auto info = reader.probe(p);
        ASSERT_TRUE(info.has_value()) << info.error().message;
        EXPECT_EQ(info->width, files::width);
        EXPECT_EQ(info->height, files::height);
        EXPECT_EQ(info->format, c.format);
        const auto img = reader.read(p);
        ASSERT_TRUE(img.has_value()) << img.error().message;
        ASSERT_EQ(img->spec().channels, c.channels);
        EXPECT_EQ(img->spec().space, image::ColorSpace::Source);
        const double scale = c.bits == 8 ? 255.0 : 65535.0;
        double worst = 0.0;
        double sum = 0.0;
        for (int y = 0; y < files::height; ++y) {
            for (int x = 0; x < files::width; ++x) {
                for (int ch = 0; ch < c.channels; ++ch) {
                    const double expected =
                        c.bits == 8 ? files::pattern8(x, y, ch) : files::pattern16(x, y, ch);
                    const double d = std::abs(sampleAt(*img, x, y, ch) * scale - expected);
                    worst = std::max(worst, d);
                    sum += d;
                }
            }
        }
        if (c.tolerance == 0.0) {
            EXPECT_LT(worst, 0.01) << "verlustfreies Format nicht exakt gelesen";
        } else {
            // JPEG: Muster mit harten Sprüngen (Modulo); mittlere Abweichung prüfen
            EXPECT_LT(sum / (files::width * files::height * c.channels), c.tolerance);
        }
        const auto icc = reader.profile(p);
        ASSERT_TRUE(icc.has_value());
        EXPECT_EQ(!icc->bytes.empty(), c.icc);
        if (c.icc) {
            // Kopf eines ICC-Profils: Signatur "acsp" an Stelle 36
            ASSERT_GT(icc->bytes.size(), 40U);
            EXPECT_EQ(std::string(icc->bytes.begin() + 36, icc->bytes.begin() + 40), "acsp");
        }
    }
}

// LF-IMP-03, NF-REL-02
TEST(OiioComponent, K_03_DefectiveAndForeignFiles)
{
    OiioImageReader reader;
    t::TempDir dir;
    const fs::path empty = dir.path() / "leer.tif";
    std::ofstream(empty).close();
    for (const fs::path& p : {files::dataDir() / "truncated.tif", files::dataDir() / "not_an_image.tif",
                              empty, dir.path() / "fehlt.tif"}) {
        SCOPED_TRACE(p.string());
        const auto img = reader.read(p);
        ASSERT_FALSE(img.has_value()) << "Fehler erwartet";
        EXPECT_NE(img.error().message.find(p.filename().string()), std::string::npos) << img.error().message;
        std::cout << "  " << img.error().message << "\n";
    }
    // falsche Endung: PNG-Inhalt als .tif wird am Inhalt erkannt oder sauber abgelehnt
    const fs::path renamed = dir.path() / "eigentlich_png.tif";
    fs::copy_file(files::dataDir() / "rgb16.png", renamed);
    const auto r = reader.read(renamed);
    if (r) {
        EXPECT_NEAR(sampleAt(*r, 5, 7, 1) * 65535.0, files::pattern16(5, 7, 1), 0.01);
    } else {
        EXPECT_NE(r.error().message.find("eigentlich_png.tif"), std::string::npos);
    }
}

/// Testbild für K-05: glatter Verlauf mit Feinstruktur, 0…1.
image::ImageBuffer exportImage(int channels)
{
    return t::imageFrom(97, 61, channels, [](int x, int y, int c) {
        return static_cast<float>(std::clamp(0.5 + 0.48 * std::sin(x * 0.13 + y * 0.07 + c * 1.3), 0.0, 1.0));
    });
}

// LF-EXP-01, LF-EXP-02, LF-EXP-03, LF-EXP-05, LF-EXP-08
TEST(OiioComponent, K_05_WriteAndVerifyWithIndependentReaders)
{
    OiioImageWriter writer;
    lcms::LcmsColorEngine color;
    const ports::IccProfile srgb = color.profile(ports::ProfileId::Srgb).value();
    const ports::IccProfile gray = color.profile(ports::ProfileId::GrayGamma22).value();
    t::TempDir dir;

    const image::ImageBuffer rgb = exportImage(3);
    const image::ImageBuffer mono = exportImage(1);
    // Rohwerte für den unabhängigen Vergleich
    for (const auto& [name, img] : {std::pair{"rgb.f32", &rgb}, std::pair{"gray.f32", &mono}}) {
        std::ofstream out(dir.path() / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(img->host().data()),
                  static_cast<std::streamsize>(img->host().size_bytes()));
    }
    ports::Metadata meta;
    meta.exif = {{"Make", "AutoNeg-Test"}, {"ImageDescription", "Kodak Portra 400, Rolle 12, Bild 7"}};
    const auto opts = [&](ports::FileFormat f, int bits, ports::Compression c, const ports::IccProfile& icc) {
        ports::WriteOptions o;
        o.format = f;
        o.bitDepth = bits;
        o.compression = c;
        o.quality = 95;
        o.icc = icc;
        o.meta = meta;
        return o;
    };
    const auto formats = writer.formats();
    const bool jxl = std::ranges::find(formats, ports::FileFormat::JpegXl) != formats.end();
    ASSERT_TRUE(jxl) << "JPEG XL fehlt in OpenImageIO";
    ASSERT_TRUE(writer.write(dir.path() / "rgb16.tif", rgb,
                             opts(ports::FileFormat::Tiff, 16, ports::Compression::Lzw, srgb)));
    ASSERT_TRUE(writer.write(dir.path() / "rgb8.jpg", rgb,
                             opts(ports::FileFormat::Jpeg, 8, ports::Compression::Lossy, srgb)));
    ASSERT_TRUE(writer.write(dir.path() / "rgb16.png", rgb,
                             opts(ports::FileFormat::Png, 16, ports::Compression::Deflate, srgb)));
    ASSERT_TRUE(writer.write(dir.path() / "rgb16.jxl", rgb,
                             opts(ports::FileFormat::JpegXl, 16, ports::Compression::Lossless, srgb)));
    ASSERT_TRUE(writer.write(dir.path() / "gray16.tif", mono,
                             opts(ports::FileFormat::Tiff, 16, ports::Compression::None, gray)));

    const std::string command = std::string("\"") + AUTONEG_PYTHON + "\" -X utf8 \"" + AUTONEG_VERIFY_EXPORT +
                                "\" \"" + dir.path().string() + "\" 97 61";
    std::cout << "  " << command << "\n";
#if defined(_WIN32)
    const int rc = std::system(("\"" + command + "\"").c_str()); // cmd.exe: äußere Anführungszeichen
#else
    const int rc = std::system(command.c_str());
#endif
    EXPECT_EQ(rc, 0) << "unabhängige Prüfung fehlgeschlagen (Ausgabe oben)";
}

/// Ganzzahliges Bild aus F32 wie OpenImageIO: floor(clamp(x, 0, 1) · max + 0,5) in float.
image::ImageBuffer quantized(const image::ImageBuffer& f, int bits)
{
    image::ImageSpec os = f.spec();
    os.format = bits == 8 ? image::PixelFormat::U8 : image::PixelFormat::U16;
    auto storage = std::make_shared<image::HostStorage>(os.sizeBytes());
    const float top = bits == 8 ? 255.0F : 65535.0F;
    const auto src = f.host();
    for (std::size_t i = 0; i < src.size(); ++i) {
        const float scaled = std::clamp(src[i], 0.0F, 1.0F) * top;
        const auto v = static_cast<unsigned>(scaled + 0.5F);
        if (bits == 8) {
            storage->bytes()[i] = static_cast<std::byte>(v);
        } else {
            const auto u = static_cast<std::uint16_t>(v);
            std::memcpy(storage->bytes().data() + i * 2, &u, 2);
        }
    }
    return image::ImageBuffer::wrap(os, std::move(storage)).value();
}

// K-M10-03, LF-EXP-01: ganzzahlige Bilder (U16/U8 aus dem Backend) ergeben dieselben Pixel wie
// die bisherige Umwandlung aus F32 im Schreiber
TEST(OiioComponent, M10_IntegerImagesWriteLikeFloat)
{
    OiioImageWriter writer;
    OiioImageReader reader;
    t::TempDir dir;
    for (const int channels : {3, 1}) {
        const image::ImageBuffer f = exportImage(channels);
        for (const auto& [format, bits, ext] :
             {std::tuple{ports::FileFormat::Tiff, 16, ".tif"}, std::tuple{ports::FileFormat::Png, 16, ".png"},
              std::tuple{ports::FileFormat::Jpeg, 8, ".jpg"}, std::tuple{ports::FileFormat::Tiff, 8, ".tif"}}) {
            SCOPED_TRACE(std::string(ext) + " " + std::to_string(bits) + " ch=" + std::to_string(channels));
            ports::WriteOptions o;
            o.format = format;
            o.bitDepth = bits;
            o.quality = 90;
            const auto a = dir.path() / ("f" + std::to_string(channels) + std::to_string(bits) + ext);
            const auto b = dir.path() / ("q" + std::to_string(channels) + std::to_string(bits) + ext);
            ASSERT_TRUE(writer.write(a, f, o).has_value());
            ASSERT_TRUE(writer.write(b, quantized(f, bits), o).has_value());
            const auto ra = reader.read(a);
            const auto rb = reader.read(b);
            ASSERT_TRUE(ra && rb);
            EXPECT_TRUE(std::ranges::equal(ra->host(), rb->host()));
        }
    }
}

} // namespace
} // namespace autoneg::adapter::oiio
