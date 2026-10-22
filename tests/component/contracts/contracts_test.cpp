// Vertragstests der Ports (K-14, NF-ARC-03, NF-ARC-12): dieselbe Suite läuft gegen den Fake
// (tests/mocks/fakes.hpp) und gegen den echten Adapter. So bleibt gesichert, dass Unit-Tests
// mit Fakes dasselbe Verhalten voraussetzen wie die Anwendung.
// Umfang M4/M5: IRawDecoder, IImageWriter, IColorEngine, IVision; die Repositories folgen in M6.

#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/adapter/libraw/libraw_decoder.hpp"
#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/adapter/opencv/opencv_vision.hpp"
#include "autoneg/mocks/fake_vision.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace autoneg {
namespace {

namespace fs = std::filesystem;
namespace t = autoneg::testing;

bool bitEqual(const image::ImageBuffer& a, const image::ImageBuffer& b)
{
    return a.spec() == b.spec() && std::ranges::equal(a.hostBytes(), b.hostBytes());
}

image::ImageBuffer testImage(int w, int h, int channels = 3)
{
    return t::imageFrom(w, h, channels, [](int x, int y, int c) {
        return static_cast<float>(0.05 + 0.9 * (0.5 + 0.5 * std::sin(x * 0.17 + y * 0.11 + c)));
    });
}

void writeBytes(const fs::path& p, std::string_view text)
{
    std::ofstream out(p, std::ios::binary);
    out << text;
}

// --- IRawDecoder ---------------------------------------------------------------------------

struct FakeRaw
{
    static constexpr const char* name = "Fake";
    static std::unique_ptr<ports::IRawDecoder> make() { return std::make_unique<mocks::FakeRawDecoder>(); }
    static fs::path validFile(const fs::path& dir)
    {
        const fs::path p = dir / "sensor.pfm";
        EXPECT_TRUE(mocks::writePfm(p, testImage(101, 67)));
        return p;
    }
};

struct LibRaw
{
    static constexpr const char* name = "LibRaw";
    static std::unique_ptr<ports::IRawDecoder> make()
    {
        return std::make_unique<adapter::libraw::LibRawDecoder>();
    }
    static fs::path validFile(const fs::path& /*dir*/)
    {
        return fs::path(AUTONEG_TESTDATA_DIR) / "100MSDCF" / "MAT05092.ARW"; // nur lesen
    }
};

template <typename Setup>
class RawDecoderContract : public ::testing::Test
{
protected:
    t::TempDir dir;
    std::unique_ptr<ports::IRawDecoder> decoder = Setup::make();
    fs::path valid = Setup::validFile(dir.path());

    image::ImageBuffer half()
    {
        ports::DecodeOptions o;
        o.half = true;
        auto img = decoder->decode(valid, o);
        EXPECT_TRUE(img.has_value()) << (img ? "" : img.error().message);
        return img ? *img : image::ImageBuffer{};
    }
};

struct SetupName
{
    template <typename T>
    static std::string GetName(int /*i*/)
    {
        return T::name;
    }
};

using RawSetups = ::testing::Types<FakeRaw, LibRaw>;
TYPED_TEST_SUITE(RawDecoderContract, RawSetups, SetupName);

TYPED_TEST(RawDecoderContract, K_14_ProbeMatchesFullDecode)
{
    const auto info = this->decoder->probe(this->valid);
    ASSERT_TRUE(info.has_value()) << info.error().message;
    const auto full = this->decoder->decode(this->valid, {});
    ASSERT_TRUE(full.has_value()) << full.error().message;
    EXPECT_EQ(full->spec().width, info->width);
    EXPECT_EQ(full->spec().height, info->height);
    EXPECT_FALSE(info->make.empty());
}

TYPED_TEST(RawDecoderContract, K_14_HalfDecodeIsLinearWorkingF32)
{
    const auto info = this->decoder->probe(this->valid);
    ASSERT_TRUE(info.has_value());
    const image::ImageBuffer img = this->half();
    const auto& s = img.spec();
    EXPECT_TRUE(s.width == info->width / 2 || s.width == (info->width + 1) / 2) << s.width;
    EXPECT_TRUE(s.height == info->height / 2 || s.height == (info->height + 1) / 2) << s.height;
    EXPECT_EQ(s.channels, 3);
    EXPECT_EQ(s.format, image::PixelFormat::F32);
    EXPECT_EQ(s.space, image::ColorSpace::WorkingLinear);
    for (const float v : img.host()) {
        ASSERT_TRUE(std::isfinite(v));
        ASSERT_GE(v, 0.0F);
        ASSERT_LE(v, 1.0F + 1e-6F);
    }
}

TYPED_TEST(RawDecoderContract, K_14_ErrorsForMissingEmptyAndForeignFiles)
{
    const fs::path missing = this->dir.path() / "fehlt.raw";
    const auto a = this->decoder->probe(missing);
    ASSERT_FALSE(a.has_value());
    EXPECT_EQ(a.error().code, base::ErrorCode::NotFound);
    EXPECT_NE(a.error().message.find("fehlt.raw"), std::string::npos);

    const fs::path empty = this->dir.path() / "leer.raw";
    writeBytes(empty, "");
    const auto b = this->decoder->decode(empty, {});
    ASSERT_FALSE(b.has_value());
    EXPECT_EQ(b.error().code, base::ErrorCode::Corrupt);

    const fs::path text = this->dir.path() / "text.raw";
    writeBytes(text, "Dies ist keine Bilddatei, sondern Text.\n");
    const auto c = this->decoder->decode(text, {});
    ASSERT_FALSE(c.has_value());
    EXPECT_TRUE(c.error().code == base::ErrorCode::Unsupported || c.error().code == base::ErrorCode::Corrupt)
        << static_cast<int>(c.error().code) << " " << c.error().message;
    EXPECT_NE(c.error().message.find("text.raw"), std::string::npos);
}

TYPED_TEST(RawDecoderContract, K_14_ConcurrentDecodesAreIdentical)
{
    const image::ImageBuffer reference = this->half();
    std::vector<image::ImageBuffer> results(4);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] {
            ports::DecodeOptions o;
            o.half = true;
            auto img = this->decoder->decode(this->valid, o);
            if (img) {
                results[i] = *img;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const auto& r : results) {
        EXPECT_TRUE(bitEqual(r, reference));
    }
}

TYPED_TEST(RawDecoderContract, K_14_PreviewIsAvailable)
{
    const auto p = this->decoder->preview(this->valid);
    ASSERT_TRUE(p.has_value()) << p.error().message;
    EXPECT_GT(p->spec().width, 0);
    EXPECT_GT(p->spec().height, 0);
}

// --- IImageWriter --------------------------------------------------------------------------

struct FakeWriter
{
    static constexpr const char* name = "Fake";
    static std::unique_ptr<ports::IImageWriter> make() { return std::make_unique<mocks::FakeImageWriter>(); }
};

struct OiioWriter
{
    static constexpr const char* name = "OpenImageIO";
    static std::unique_ptr<ports::IImageWriter> make()
    {
        return std::make_unique<adapter::oiio::OiioImageWriter>();
    }
};

template <typename Setup>
class ImageWriterContract : public ::testing::Test
{
protected:
    t::TempDir dir;
    std::unique_ptr<ports::IImageWriter> writer = Setup::make();

    static ports::WriteOptions tiff16()
    {
        ports::WriteOptions o;
        o.format = ports::FileFormat::Tiff;
        o.bitDepth = 16;
        return o;
    }

    std::size_t fileCount() const
    {
        return static_cast<std::size_t>(
            std::distance(fs::directory_iterator(dir.path()), fs::directory_iterator()));
    }
};

using WriterSetups = ::testing::Types<FakeWriter, OiioWriter>;
TYPED_TEST_SUITE(ImageWriterContract, WriterSetups, SetupName);

TYPED_TEST(ImageWriterContract, K_14_OffersTiff)
{
    const auto f = this->writer->formats();
    EXPECT_NE(std::ranges::find(f, ports::FileFormat::Tiff), f.end());
}

TYPED_TEST(ImageWriterContract, K_14_WriteCreatesFileWithoutLeftovers)
{
    const fs::path p = this->dir.path() / "bild.tif";
    ASSERT_TRUE(this->writer->write(p, testImage(40, 30), this->tiff16()).has_value());
    EXPECT_TRUE(fs::is_regular_file(p));
    EXPECT_GT(fs::file_size(p), 0U);
    EXPECT_EQ(this->fileCount(), 1U);
}

TYPED_TEST(ImageWriterContract, K_14_OverwriteReplacesContent)
{
    const fs::path p = this->dir.path() / "bild.tif";
    ASSERT_TRUE(this->writer->write(p, testImage(40, 30), this->tiff16()).has_value());
    const auto first = fs::file_size(p);
    ASSERT_TRUE(this->writer->write(p, testImage(80, 60), this->tiff16()).has_value());
    EXPECT_NE(fs::file_size(p), first);
    EXPECT_EQ(this->fileCount(), 1U);
}

TYPED_TEST(ImageWriterContract, K_14_InvalidInputLeavesTargetUntouched)
{
    const fs::path p = this->dir.path() / "bild.tif";
    ASSERT_TRUE(this->writer->write(p, testImage(40, 30), this->tiff16()).has_value());
    const auto before = fs::file_size(p);

    ports::WriteOptions bad = this->tiff16();
    bad.bitDepth = 12;
    const auto a = this->writer->write(p, testImage(80, 60), bad);
    ASSERT_FALSE(a.has_value());
    EXPECT_EQ(a.error().code, base::ErrorCode::InvalidArgument);

    const auto b = this->writer->write(p, testImage(80, 60, 2), this->tiff16());
    ASSERT_FALSE(b.has_value());
    EXPECT_EQ(b.error().code, base::ErrorCode::InvalidArgument);

    EXPECT_EQ(fs::file_size(p), before);
    EXPECT_EQ(this->fileCount(), 1U);
}

TYPED_TEST(ImageWriterContract, K_14_MissingFolderFailsWithoutFile)
{
    const fs::path p = this->dir.path() / "fehlt" / "bild.tif";
    const auto r = this->writer->write(p, testImage(40, 30), this->tiff16());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::Io);
    EXPECT_EQ(this->fileCount(), 0U);
}

TYPED_TEST(ImageWriterContract, K_14_EncodeAndGrayscale)
{
    const auto bytes = this->writer->encode(testImage(40, 30), this->tiff16());
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    EXPECT_FALSE(bytes->empty());
    const fs::path p = this->dir.path() / "grau.tif";
    EXPECT_TRUE(this->writer->write(p, testImage(40, 30, 1), this->tiff16()).has_value());
}

TYPED_TEST(ImageWriterContract, K_14_ConcurrentWrites)
{
    std::vector<std::thread> threads;
    std::vector<int> ok(6, 0);
    for (std::size_t i = 0; i < ok.size(); ++i) {
        threads.emplace_back([&, i] {
            const fs::path p = this->dir.path() / ("bild" + std::to_string(i) + ".tif");
            ok[i] =
                this->writer->write(p, testImage(50 + static_cast<int>(i), 30), this->tiff16()).has_value()
                    ? 1
                    : 0;
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(std::ranges::count(ok, 1), 6);
    EXPECT_EQ(this->fileCount(), 6U);
}

// --- IColorEngine --------------------------------------------------------------------------

struct FakeColor
{
    static constexpr const char* name = "Fake";
    static std::unique_ptr<ports::IColorEngine> make() { return std::make_unique<mocks::FakeColorEngine>(); }
};

struct Lcms
{
    static constexpr const char* name = "LittleCMS";
    static std::unique_ptr<ports::IColorEngine> make()
    {
        return std::make_unique<adapter::lcms::LcmsColorEngine>();
    }
};

template <typename Setup>
class ColorEngineContract : public ::testing::Test
{
protected:
    std::unique_ptr<ports::IColorEngine> engine = Setup::make();

    ports::IccProfile profile(ports::ProfileId id)
    {
        auto p = engine->profile(id);
        EXPECT_TRUE(p.has_value());
        return p ? *p : ports::IccProfile{};
    }

    std::shared_ptr<const ports::IColorTransform> transform(ports::ProfileId a, ports::ProfileId b)
    {
        auto x = engine->transform(profile(a), profile(b), ports::Intent::RelativeColorimetric);
        EXPECT_TRUE(x.has_value()) << (x ? "" : x.error().message);
        return x ? *x : nullptr;
    }
};

using ColorSetups = ::testing::Types<FakeColor, Lcms>;
TYPED_TEST_SUITE(ColorEngineContract, ColorSetups, SetupName);

constexpr ports::ProfileId allProfiles[] = {ports::ProfileId::WorkingRec2020Linear, ports::ProfileId::Srgb,
                                            ports::ProfileId::AdobeRgb, ports::ProfileId::ProPhoto,
                                            ports::ProfileId::GrayGamma22};

TYPED_TEST(ColorEngineContract, K_14_ProfilesAreDistinctAndNamed)
{
    std::vector<ports::IccProfile> seen;
    for (const ports::ProfileId id : allProfiles) {
        const ports::IccProfile p = this->profile(id);
        EXPECT_FALSE(p.bytes.empty());
        EXPECT_FALSE(p.name.empty());
        for (const auto& q : seen) {
            EXPECT_NE(p.bytes, q.bytes);
        }
        seen.push_back(p);
    }
}

TYPED_TEST(ColorEngineContract, K_14_RoundTripAndWhitePoint)
{
    // Farben innerhalb von sRGB (linear Rec. 2020)
    const image::ImageBuffer src = t::imageFrom(16, 4, 3, [](int x, int y, int c) {
        return static_cast<float>(0.1 + 0.05 * x * (c == 0 ? 1 : 0.6) + 0.1 * y * (c == 2 ? 1 : 0.5) / 4.0);
    });
    for (const ports::ProfileId id :
         {ports::ProfileId::Srgb, ports::ProfileId::AdobeRgb, ports::ProfileId::ProPhoto}) {
        const auto there = this->transform(ports::ProfileId::WorkingRec2020Linear, id);
        const auto back = this->transform(id, ports::ProfileId::WorkingRec2020Linear);
        ASSERT_TRUE(there && back);
        const auto mid = there->apply(src);
        ASSERT_TRUE(mid.has_value());
        const auto again = back->apply(*mid);
        ASSERT_TRUE(again.has_value());
        const auto a = src.host();
        const auto b = again->host();
        for (std::size_t i = 0; i < a.size(); ++i) {
            ASSERT_NEAR(a[i], b[i], 2e-4) << "Profil " << static_cast<int>(id);
        }
        // Weiß bleibt Weiß (relativ farbmetrisch)
        const image::ImageBuffer white = t::imageFrom(1, 1, 3, [](int, int, int) { return 1.0F; });
        const auto w = there->apply(white);
        ASSERT_TRUE(w.has_value());
        for (const float v : w->host()) {
            EXPECT_NEAR(v, 1.0F, 2e-3) << "Profil " << static_cast<int>(id);
        }
    }
}

TYPED_TEST(ColorEngineContract, K_14_GrayAndIdentity)
{
    const image::ImageBuffer src = testImage(8, 8);
    const auto gray = this->transform(ports::ProfileId::WorkingRec2020Linear, ports::ProfileId::GrayGamma22);
    ASSERT_TRUE(gray);
    const auto g = gray->apply(src);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->spec().channels, 1);
    EXPECT_EQ(g->spec().width, 8);

    const auto same =
        this->transform(ports::ProfileId::WorkingRec2020Linear, ports::ProfileId::WorkingRec2020Linear);
    ASSERT_TRUE(same);
    const auto s = same->apply(src);
    ASSERT_TRUE(s.has_value());
    for (std::size_t i = 0; i < src.host().size(); ++i) {
        ASSERT_NEAR(s->host()[i], src.host()[i], 1e-5);
    }
}

TYPED_TEST(ColorEngineContract, K_14_InvalidProfileIsCorrupt)
{
    const std::string junk = "kein ICC-Profil";
    const ports::IccProfile bad{base::Bytes(junk.begin(), junk.end()), "kaputt"};
    const auto x =
        this->engine->transform(bad, this->profile(ports::ProfileId::Srgb), ports::Intent::Perceptual);
    ASSERT_FALSE(x.has_value());
    EXPECT_EQ(x.error().code, base::ErrorCode::Corrupt);
}

TYPED_TEST(ColorEngineContract, K_14_Lut3dMatchesApply)
{
    const auto x = this->transform(ports::ProfileId::WorkingRec2020Linear, ports::ProfileId::Srgb);
    ASSERT_TRUE(x);
    const int n = 5;
    const image::Lut3D lut = x->lut3d(n);
    ASSERT_EQ(lut.size, n);
    ASSERT_EQ(lut.data.size(), static_cast<std::size_t>(n * n * n * 3));
    // Gitterpunkt (r, g, b) = (3, 1, 2): Index ((b·n + g)·n + r)·3
    const image::ImageBuffer px = t::imageFrom(1, 1, 3, [](int, int, int c) {
        return c == 0 ? 0.75F : c == 1 ? 0.25F : 0.5F;
    });
    const auto out = x->apply(px);
    ASSERT_TRUE(out.has_value());
    const std::size_t i = static_cast<std::size_t>(((2 * n + 1) * n + 3) * 3);
    for (std::size_t c = 0; c < 3; ++c) {
        EXPECT_NEAR(lut.data[i + c], out->host()[c], 1e-5);
    }
}

TYPED_TEST(ColorEngineContract, K_14_ConcurrentApplyIsIdentical)
{
    const auto x = this->transform(ports::ProfileId::WorkingRec2020Linear, ports::ProfileId::AdobeRgb);
    ASSERT_TRUE(x);
    const image::ImageBuffer src = testImage(300, 200);
    const image::ImageBuffer reference = x->apply(src).value();
    std::vector<image::ImageBuffer> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = x->apply(src).value(); });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const auto& r : results) {
        EXPECT_TRUE(bitEqual(r, reference));
    }
}

// --- IVision -------------------------------------------------------------------------------

struct FakeVis
{
    static constexpr const char* name = "Fake";
    static std::unique_ptr<ports::IVision> make() { return std::make_unique<mocks::FakeVision>(); }
};

struct OpenCv
{
    static constexpr const char* name = "OpenCV";
    static std::unique_ptr<ports::IVision> make()
    {
        return std::make_unique<adapter::opencv::OpenCvVision>();
    }
};

template <typename Setup>
class VisionContract : public ::testing::Test
{
protected:
    std::unique_ptr<ports::IVision> vision = Setup::make();

    static image::ImageBuffer mask(int w, int h, const std::function<bool(int, int)>& on)
    {
        return t::imageFrom(w, h, 1, [&](int x, int y, int) { return on(x, y) ? 1.0F : 0.0F; });
    }
    static int count(const image::ImageBuffer& m)
    {
        return static_cast<int>(std::ranges::count_if(m.host(), [](float v) { return v > 0.5F; }));
    }
};

using VisionSetups = ::testing::Types<FakeVis, OpenCv>;
TYPED_TEST_SUITE(VisionContract, VisionSetups, SetupName);

TYPED_TEST(VisionContract, K_14_EdgesFollowRectangleBoundary)
{
    // helles Rechteck 60..139 x 40..99 auf dunklem Grund
    const image::ImageBuffer img = t::imageFrom(200, 140, 1, [](int x, int y, int) {
        return (x >= 60 && x < 140 && y >= 40 && y < 100) ? 0.8F : 0.2F;
    });
    const auto e = this->vision->edges(img, 1.0F);
    ASSERT_TRUE(e.has_value()) << e.error().message;
    ASSERT_EQ(e->spec().channels, 1);
    int far = 0;
    int total = 0;
    for (int y = 0; y < 140; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (e->host()[static_cast<std::size_t>(y * 200 + x)] < 0.5F) {
                continue;
            }
            ++total;
            const double dx = std::min(std::abs(x + 0.5 - 60.0), std::abs(x + 0.5 - 140.0));
            const double dy = std::min(std::abs(y + 0.5 - 40.0), std::abs(y + 0.5 - 100.0));
            const bool onV = dx <= 2.0 && y >= 38 && y <= 101;
            const bool onH = dy <= 2.0 && x >= 58 && x <= 141;
            far += onV || onH ? 0 : 1;
        }
    }
    // Umfang 280 px; eine geschlossene, ein Pixel dünne Kante hat etwa 276 Pixel
    EXPECT_GE(total, 9 * 2 * (80 + 60) / 10);
    EXPECT_LE(total, 2 * 2 * (80 + 60));
    EXPECT_EQ(far, 0);
}

TYPED_TEST(VisionContract, K_14_LinesFindTiltedEdge)
{
    // Kante unter 3 Grad über die ganze Breite
    const double k = std::tan(3.0 * std::numbers::pi / 180.0);
    const image::ImageBuffer img = t::imageFrom(
        300, 200, 1, [k](int x, int y, int) { return (y + 0.5) > 80.0 + k * (x + 0.5) ? 0.8F : 0.2F; });
    const auto e = this->vision->edges(img, 1.0F);
    ASSERT_TRUE(e.has_value());
    const auto l = this->vision->lines(*e);
    ASSERT_TRUE(l.has_value()) << l.error().message;
    bool found = false;
    for (const ports::Line& ln : *l) {
        const double angle = std::atan2(ln.b.y - ln.a.y, ln.b.x - ln.a.x) * 180.0 / std::numbers::pi;
        const double a = angle > 90 ? angle - 180 : angle < -90 ? angle + 180 : angle;
        const double len = std::hypot(ln.b.x - ln.a.x, ln.b.y - ln.a.y);
        found = found || (std::abs(a - 3.0) <= 0.5 && len >= 150.0);
    }
    EXPECT_TRUE(found) << l->size() << " Strecken";
}

TYPED_TEST(VisionContract, K_14_ContoursPerBlob)
{
    const auto m = this->mask(120, 80, [](int x, int y) {
        return (x >= 10 && x < 40 && y >= 10 && y < 30) || (x >= 70 && x < 110 && y >= 40 && y < 75);
    });
    const auto c = this->vision->contours(m);
    ASSERT_TRUE(c.has_value());
    ASSERT_EQ(c->size(), 2U);
    std::vector<std::array<double, 4>> boxes;
    for (const ports::Polygon& p : *c) {
        std::array<double, 4> b{1e9, 1e9, -1e9, -1e9};
        for (const auto& pt : p.points) {
            b[0] = std::min(b[0], pt.x);
            b[1] = std::min(b[1], pt.y);
            b[2] = std::max(b[2], pt.x);
            b[3] = std::max(b[3], pt.y);
        }
        boxes.push_back(b);
    }
    std::ranges::sort(boxes);
    EXPECT_EQ(boxes[0], (std::array<double, 4>{10, 10, 39, 29}));
    EXPECT_EQ(boxes[1], (std::array<double, 4>{70, 40, 109, 74}));
}

TYPED_TEST(VisionContract, K_14_MorphologyWithEllipse)
{
    const auto dot = this->mask(15, 15, [](int x, int y) { return x == 7 && y == 7; });
    const auto dil = this->vision->morphology(dot, ports::MorphOp::Dilate, 1);
    ASSERT_TRUE(dil.has_value());
    EXPECT_EQ(this->count(*dil), 5); // Ellipse 3 x 3 = Kreuz
    const auto square = this->mask(15, 15, [](int x, int y) { return x >= 5 && x < 10 && y >= 5 && y < 10; });
    const auto ero = this->vision->morphology(square, ports::MorphOp::Erode, 1);
    ASSERT_TRUE(ero.has_value());
    EXPECT_EQ(this->count(*ero), 9);
    // Öffnen entfernt Einzelpixel, Schließen füllt ein Loch
    const auto noisy = this->mask(
        15, 15, [](int x, int y) { return (x >= 3 && x < 12 && y >= 3 && y < 12) || (x == 0 && y == 14); });
    const auto open = this->vision->morphology(noisy, ports::MorphOp::Open, 1);
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->host()[14 * 15 + 0], 0.0F);
    const auto holed = this->mask(
        15, 15, [](int x, int y) { return x >= 3 && x < 12 && y >= 3 && y < 12 && !(x == 7 && y == 7); });
    const auto close = this->vision->morphology(holed, ports::MorphOp::Close, 1);
    ASSERT_TRUE(close.has_value());
    EXPECT_EQ(close->host()[7 * 15 + 7], 1.0F);
}

TYPED_TEST(VisionContract, K_14_ConcurrentEdgesAreIdentical)
{
    const image::ImageBuffer img = testImage(160, 120, 1);
    const auto reference = this->vision->edges(img, 1.5F).value();
    std::vector<image::ImageBuffer> results(6);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = this->vision->edges(img, 1.5F).value(); });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const auto& r : results) {
        EXPECT_TRUE(bitEqual(r, reference));
    }
}

} // namespace
} // namespace autoneg
