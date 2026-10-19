// Unit-Tests autoneg::base (A-04).

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/base/hash.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace autoneg::base {
namespace {

Expected<int> half(int v)
{
    if (v % 2 != 0) {
        return makeError(ErrorCode::InvalidArgument, "ungerade");
    }
    return v / 2;
}

TEST(Error, M1_ExpectedCarriesValueOrError)
{
    EXPECT_EQ(half(4).value(), 2);
    const auto failed = half(3);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(failed.error().message, "ungerade");
    EXPECT_EQ(toString(ErrorCode::Cancelled), "Cancelled");
}

TEST(Hasher, M1_DeterministicAndSensitive)
{
    // Fester Erwartungswert: Ergebnis muss auf allen Plattformen und in allen Läufen gleich sein.
    const std::uint64_t fixed = Hasher{}.add(std::string_view("autoneg")).add(42).add(0.5).value();
    EXPECT_EQ(fixed, Hasher{}.add(std::string_view("autoneg")).add(42).add(0.5).value());
    EXPECT_EQ(fixed, 0x6645ff63acb92a35ULL);

    EXPECT_NE(Hasher{}.add(std::string_view("ab")).add(std::string_view("c")).value(),
              Hasher{}.add(std::string_view("a")).add(std::string_view("bc")).value());
    EXPECT_EQ(Hasher{}.add(0.0).value(), Hasher{}.add(-0.0).value());
    EXPECT_EQ(Hasher{}.add(std::numeric_limits<double>::quiet_NaN()).value(),
              Hasher{}.add(-std::numeric_limits<double>::quiet_NaN()).value());
    EXPECT_NE(Hasher{}.add(1.0).value(), Hasher{}.add(1.0000001).value());
}

TEST(Geometry, M1_AffineComposeAndInverse)
{
    const Mat2x3 m = Mat2x3::translation(10, -4) * Mat2x3::rotation(30) * Mat2x3::scale(2, 0.5);
    const auto inv = m.inverse();
    ASSERT_TRUE(inv.has_value());

    const Point2 p{3.5, -7.25};
    const Point2 back = inv->apply(m.apply(p));
    EXPECT_NEAR(back.x, p.x, 1e-12);
    EXPECT_NEAR(back.y, p.y, 1e-12);

    const Point2 r = Mat2x3::rotation(90).apply({1, 0});
    EXPECT_NEAR(r.x, 0.0, 1e-12);
    EXPECT_NEAR(r.y, 1.0, 1e-12);
    EXPECT_FALSE(Mat2x3::scale(0, 1).inverse().has_value());
}

TEST(Geometry, M1_Mat3Multiply)
{
    const Mat3 d = Mat3::diagonal({2, 3, 4});
    const Vec3 v = (d * Mat3::identity()) * Vec3{1, 1, 1};
    EXPECT_EQ(v, (Vec3{2, 3, 4}));
    EXPECT_EQ(d * Mat3::diagonal({0.5, 1.0 / 3.0, 0.25}), Mat3::diagonal({1, 1, 1}));
}

} // namespace
} // namespace autoneg::base
