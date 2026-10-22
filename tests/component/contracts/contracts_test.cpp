// Vertragstests der Ports (K-14, NF-ARC-03, NF-ARC-12): dieselbe Suite läuft gegen den Fake
// (tests/mocks/fakes.hpp) und gegen den echten Adapter. So bleibt gesichert, dass Unit-Tests
// mit Fakes dasselbe Verhalten voraussetzen wie die Anwendung.
// Umfang M4: IRawDecoder, IImageWriter, IColorEngine; die Repositories folgen in M6.

#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/adapter/libraw/libraw_decoder.hpp"
#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
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

} // namespace
} // namespace autoneg
