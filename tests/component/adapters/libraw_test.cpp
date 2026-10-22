// Komponententests LibRawDecoder mit echter LibRaw und echten Scans (K-01, K-03).
// Testdaten werden nur gelesen; Dateien zum Verändern entstehen im temporären Ordner.

#include "test_files.hpp"

#include "autoneg/adapter/libraw/libraw_decoder.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace autoneg::adapter::libraw {
namespace {

namespace fs = std::filesystem;
namespace files = autoneg::testing::files;

std::vector<fs::path> allRawFiles()
{
    std::vector<fs::path> out;
    for (const auto& e : fs::recursive_directory_iterator(files::testData())) {
        std::string ext = e.path().extension().string();
        std::ranges::transform(ext, ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (e.is_regular_file() && (ext == ".ARW" || ext == ".RAF")) {
            out.push_back(e.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

/// Bekannte Kameras der Testdaten: Hersteller, Modell, Sensorgröße (quer).
struct Camera
{
    std::string make;
    int width;
    int height;
};

const std::map<std::string, Camera>& cameras()
{
    static const std::map<std::string, Camera> c{
        {"ILCE-7RM4", {"Sony", 9568, 6376}}, {"X-E5", {"Fujifilm", 0, 0}}}; // X-E5: Größe aus probe = decode
    return c;
}

/// Schachbrettenergie in einem gleichmäßigen Feld: (a + d) − (b + c) je 2 × 2, relativ zum
/// Mittelwert. Fehlerhaftes Demosaicing (falsches CFA-Muster) erzeugt hier ein starkes Muster.
double checkerboard(const image::ImageBuffer& img, int x0, int y0, int w, int h)
{
    const auto& s = img.spec();
    const auto v = img.host();
    const auto at = [&](int x, int y, int c) {
        return static_cast<double>(v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                                      static_cast<std::size_t>(x)) *
                                         3 +
                                     static_cast<std::size_t>(c)]);
    };
    double energy = 0.0;
    double mean = 0.0;
    for (int y = y0; y + 1 < y0 + h; y += 2) {
        for (int x = x0; x + 1 < x0 + w; x += 2) {
            for (int c = 0; c < 3; ++c) {
                energy += std::abs(at(x, y, c) + at(x + 1, y + 1, c) - at(x + 1, y, c) - at(x, y + 1, c));
                mean += at(x, y, c);
            }
        }
    }
    return energy / std::max(mean, 1e-9);
}

bool envSet(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    const bool set = _dupenv_s(&value, &size, name) == 0 && value != nullptr;
    std::free(value);
    return set;
#else
    return std::getenv(name) != nullptr;
#endif
}

// LF-IMP-01, LF-IMP-02
TEST(LibRawComponent, K_01_ProbeAllOwnScans)
{
    LibRawDecoder decoder;
    const auto rawFiles = allRawFiles();
    ASSERT_GE(rawFiles.size(), 400U) << "Testdaten fehlen: " << files::testData();
    std::map<std::string, int> perModel;
    for (const fs::path& p : rawFiles) {
        const auto info = decoder.probe(p);
        ASSERT_TRUE(info.has_value()) << p << ": " << info.error().message;
        ++perModel[info->model];
        const auto cam = cameras().find(info->model);
        ASSERT_NE(cam, cameras().end()) << p << ": unerwartetes Modell " << info->model;
        EXPECT_NE(info->make.find(cam->second.make), std::string::npos) << p << ": " << info->make;
        if (cam->second.width > 0) {
            const bool landscape = info->width == cam->second.width && info->height == cam->second.height;
            const bool portrait = info->width == cam->second.height && info->height == cam->second.width;
            EXPECT_TRUE(landscape || portrait) << p << ": " << info->width << " x " << info->height;
        }
    }
    for (const auto& [model, count] : perModel) {
        std::cout << "  " << model << ": " << count << " Dateien\n";
    }
}

// LF-IMP-01, LF-IMP-02: volle Dekodierung je Kamera, Größe und Demosaicing
TEST(LibRawComponent, K_01_FullDecodePerCamera)
{
    LibRawDecoder decoder;
    for (const fs::path& p :
         {files::testData() / "100MSDCF" / "MAT05092.ARW", files::testData() / "Fuji RAW" / "MSCF0358.RAF"}) {
        SCOPED_TRACE(p.string());
        const auto info = decoder.probe(p);
        ASSERT_TRUE(info.has_value()) << info.error().message;
        const auto img = decoder.decode(p, {});
        ASSERT_TRUE(img.has_value()) << img.error().message;
        EXPECT_EQ(img->spec().width, info->width);
        EXPECT_EQ(img->spec().height, info->height);
        double maxValue = 0.0;
        for (const float v : img->host()) {
            ASSERT_TRUE(std::isfinite(v));
            maxValue = std::max(maxValue, static_cast<double>(v));
        }
        EXPECT_GT(maxValue, 0.2); // nicht schwarz
    }
    // Prüfausschnitt: Perforationsloch (gleichmäßige Leuchtfläche) des Dias MAT05092
    const auto img = decoder.decode(files::testData() / "100MSDCF" / "MAT05092.ARW", {});
    ASSERT_TRUE(img.has_value());
    const int w = img->spec().width;
    const int h = img->spec().height;
    const double cb = checkerboard(*img, static_cast<int>(0.128 * w), static_cast<int>(0.006 * h),
                                   static_cast<int>(0.02 * w), static_cast<int>(0.012 * h));
    std::cout << "  Schachbrettenergie im Perforationsloch: " << cb << "\n";
    EXPECT_LT(cb, 0.03);
}

// LF-IMP-01: auf Wunsch alle eigenen Dateien dekodieren (halbe Auflösung; dauert einige Minuten).
TEST(LibRawComponent, K_01_DecodeAllOwnScansOnRequest)
{
    if (!envSet("AUTONEG_K01_ALL")) {
        GTEST_SKIP() << "AUTONEG_K01_ALL=1 setzen, um alle Dateien zu dekodieren (T-12)";
    }
    LibRawDecoder decoder;
    ports::DecodeOptions half;
    half.half = true;
    for (const fs::path& p : allRawFiles()) {
        const auto img = decoder.decode(p, half);
        EXPECT_TRUE(img.has_value()) << p << ": " << (img ? "" : img.error().message);
    }
}

// LF-IMP-03, NF-REL-02
TEST(LibRawComponent, K_03_DefectiveAndForeignFiles)
{
    LibRawDecoder decoder;
    autoneg::testing::TempDir dir;
    // abgeschnittene RAW-Datei (Kopf vollständig, Bilddaten fehlen)
    const fs::path source = files::testData() / "100MSDCF" / "MAT05092.ARW";
    const fs::path cut = dir.path() / "abgeschnitten.ARW";
    {
        std::ifstream in(source, std::ios::binary);
        std::vector<char> head(4 * 1024 * 1024);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::ofstream out(cut, std::ios::binary);
        out.write(head.data(), in.gcount());
    }
    const auto a = decoder.decode(cut, {});
    ASSERT_FALSE(a.has_value()) << "abgeschnittene Datei wurde ohne Fehler dekodiert";
    EXPECT_NE(a.error().message.find("abgeschnitten.ARW"), std::string::npos) << a.error().message;
    std::cout << "  abgeschnitten: " << a.error().message << "\n";

    const fs::path empty = dir.path() / "leer.ARW";
    std::ofstream(empty).close();
    const auto b = decoder.probe(empty);
    ASSERT_FALSE(b.has_value());
    EXPECT_EQ(b.error().code, base::ErrorCode::Corrupt);

    const auto c = decoder.probe(files::dataDir() / "not_a_raw.ARW");
    ASSERT_FALSE(c.has_value());
    EXPECT_EQ(c.error().code, base::ErrorCode::Unsupported);
    EXPECT_NE(c.error().message.find("DNG"), std::string::npos) << "Abhilfe fehlt: " << c.error().message;
    std::cout << "  keine RAW-Datei: " << c.error().message << "\n";

    // TIFF mit RAW-Endung: LibRaw liest TIFF-Container teilweise; Erwartung: Fehler oder Bild
    const fs::path tiff = dir.path() / "bild.ARW";
    fs::copy_file(files::dataDir() / "rgb16.tif", tiff);
    const auto d = decoder.decode(tiff, {});
    if (!d) {
        EXPECT_NE(d.error().message.find("bild.ARW"), std::string::npos);
    }
}

} // namespace
} // namespace autoneg::adapter::libraw
