// Unit-Tests autoneg::image (Architektur 9.2).

#include "autoneg/image/cancel_token.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/tile_grid.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

namespace autoneg::image {
namespace {

ImageSpec rgb(int w, int h)
{
    return ImageSpec{w, h, 3, PixelFormat::F32, ColorSpace::WorkingLinear};
}

// NF-ARC-08
TEST(TileGrid, U_IMG_01_CoresCoverImageExactlyOnce)
{
    const ImageSpec spec = rgb(1001, 777);
    const TileGrid grid(spec, 256, 13);

    std::vector<int> hits(static_cast<std::size_t>(spec.width) * static_cast<std::size_t>(spec.height), 0);
    for (const Tile& t : grid.tiles()) {
        ASSERT_TRUE(t.core.inside(spec.width, spec.height));
        for (int y = t.core.y; y < t.core.y + t.core.h; ++y) {
            for (int x = t.core.x; x < t.core.x + t.core.w; ++x) {
                ++hits[static_cast<std::size_t>(y) * static_cast<std::size_t>(spec.width) +
                       static_cast<std::size_t>(x)];
            }
        }
    }
    EXPECT_TRUE(std::ranges::all_of(hits, [](int n) { return n == 1; }));
    EXPECT_EQ(grid.columns(), 4);
    EXPECT_EQ(grid.rows(), 4);
    EXPECT_EQ(grid.count(), 16U);
}

// NF-ARC-08
TEST(TileGrid, U_IMG_02_HaloClippedAtImageBorder)
{
    const ImageSpec spec = rgb(300, 300);
    const TileGrid grid(spec, 256, 40);

    ASSERT_EQ(grid.count(), 4U);
    for (const Tile& t : grid.tiles()) {
        EXPECT_TRUE(t.padded.inside(spec.width, spec.height));
        // innen (nicht am Bildrand) beträgt der Rand 40 px
        if (t.core.x > 0) {
            EXPECT_EQ(t.core.x - t.padded.x, 40);
        } else {
            EXPECT_EQ(t.padded.x, 0);
        }
        if (t.core.x + t.core.w < spec.width) {
            EXPECT_EQ(t.padded.x + t.padded.w - (t.core.x + t.core.w), 40);
        }
        if (t.core.y > 0) {
            EXPECT_EQ(t.core.y - t.padded.y, 40);
        }
        if (t.core.y + t.core.h < spec.height) {
            EXPECT_EQ(t.padded.y + t.padded.h - (t.core.y + t.core.h), 40);
        }
    }
    EXPECT_EQ(grid.tiles()[0].padded, (Roi{0, 0, 296, 296}));
    EXPECT_EQ(grid.tiles()[3].padded, (Roi{216, 216, 84, 84}));
}

// NF-ARC-06
TEST(ImageBuffer, U_IMG_03_LocationSizeAlignment)
{
    const auto buf = ImageBuffer::allocateHost(rgb(100, 50));
    ASSERT_TRUE(buf.has_value());

    EXPECT_EQ(buf->location(), MemoryLocation::Host);
    EXPECT_EQ(buf->sizeBytes(), 60000U);
    ASSERT_EQ(buf->host().size(), 15000U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buf->host().data()) % hostAlignment, 0U);
}

// NF-REL-02
TEST(ImageBuffer, U_IMG_04_ViewAndInvalidRegion)
{
    std::vector<float> values(100 * 100);
    std::iota(values.begin(), values.end(), 0.0F);
    const ImageSpec spec{100, 100, 1, PixelFormat::F32, ColorSpace::Density};
    const auto buf = ImageBuffer::fromFloats(spec, values);
    ASSERT_TRUE(buf.has_value());

    const auto view = buf->view(Roi{10, 10, 20, 20});
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(view->row(0).size(), 20U);
    EXPECT_FLOAT_EQ(view->row(0)[0], 1010.0F);   // Zeile 10, Spalte 10
    EXPECT_FLOAT_EQ(view->row(19)[19], 2929.0F); // Zeile 29, Spalte 29
    EXPECT_TRUE(view->row(20).empty());

    const auto invalid = buf->view(Roi{90, 90, 20, 20});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, base::ErrorCode::OutOfRange);
}

TEST(ImageBuffer, M1_InvalidSpecAndSizeMismatchAreErrors)
{
    EXPECT_FALSE(ImageBuffer::allocateHost(rgb(0, 10)).has_value());
    const std::vector<float> tooFew(5);
    EXPECT_FALSE(ImageBuffer::fromFloats(rgb(2, 2), tooFew).has_value());
    EXPECT_TRUE(ImageBuffer{}.empty());
    EXPECT_TRUE(ImageBuffer{}.host().empty());
}

TEST(ImageBuffer, M1_CopiesShareImmutableStorage)
{
    const auto a = ImageBuffer::allocateHost(
        rgb(4, 4), [](std::span<std::byte> bytes) { std::ranges::fill(bytes, std::byte{0x3f}); });
    ASSERT_TRUE(a.has_value());
    const ImageBuffer b = *a; // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_EQ(a->storage().get(), b.storage().get());
    EXPECT_EQ(b.hostBytes()[0], std::byte{0x3f});
}

TEST(CancelToken, M1_CopiesShareStateAcrossThreads)
{
    const CancelToken token;
    const CancelToken copy = token; // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_FALSE(copy.cancelled());
    std::thread worker([token] { token.cancel(); });
    worker.join();
    EXPECT_TRUE(copy.cancelled());
}

} // namespace
} // namespace autoneg::image
