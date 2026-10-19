#include "autoneg/base/geometry.hpp"

#include <cmath>
#include <numbers>

namespace autoneg::base {

Mat3 Mat3::diagonal(const Vec3& d) noexcept
{
    return Mat3{{d.x, 0, 0, 0, d.y, 0, 0, 0, d.z}};
}

Mat3 operator*(const Mat3& a, const Mat3& b) noexcept
{
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a(i, k) * b(k, j);
            }
            r.m[static_cast<std::size_t>(i * 3 + j)] = sum;
        }
    }
    return r;
}

Vec3 operator*(const Mat3& a, const Vec3& v) noexcept
{
    return {a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z, a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z,
            a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z};
}

Mat2x3 Mat2x3::translation(double tx, double ty) noexcept
{
    return Mat2x3{{1, 0, tx, 0, 1, ty}};
}

Mat2x3 Mat2x3::scale(double sx, double sy) noexcept
{
    return Mat2x3{{sx, 0, 0, 0, sy, 0}};
}

Mat2x3 Mat2x3::rotation(double degrees) noexcept
{
    const double rad = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    return Mat2x3{{c, -s, 0, s, c, 0}};
}

Point2 Mat2x3::apply(const Point2& p) const noexcept
{
    return {m[0] * p.x + m[1] * p.y + m[2], m[3] * p.x + m[4] * p.y + m[5]};
}

std::optional<Mat2x3> Mat2x3::inverse() const noexcept
{
    const double det = m[0] * m[4] - m[1] * m[3];
    if (det == 0.0 || !std::isfinite(det)) {
        return std::nullopt;
    }
    const double a = m[4] / det;
    const double b = -m[1] / det;
    const double d = -m[3] / det;
    const double e = m[0] / det;
    return Mat2x3{{a, b, -(a * m[2] + b * m[5]), d, e, -(d * m[2] + e * m[5])}};
}

Mat2x3 operator*(const Mat2x3& a, const Mat2x3& b) noexcept
{
    const auto& x = a.m;
    const auto& y = b.m;
    return Mat2x3{{x[0] * y[0] + x[1] * y[3], x[0] * y[1] + x[1] * y[4], x[0] * y[2] + x[1] * y[5] + x[2],
                   x[3] * y[0] + x[4] * y[3], x[3] * y[1] + x[4] * y[4], x[3] * y[2] + x[4] * y[5] + x[5]}};
}

} // namespace autoneg::base
