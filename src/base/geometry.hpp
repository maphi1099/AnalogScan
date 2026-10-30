#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>

namespace autoneg::base {

/// Dreiervektor, z. B. Farbe je Kanal (x = R, y = G, z = B) oder Dichte.
struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend auto operator<=>(const Vec3&, const Vec3&) = default;
};

/// Punkt in Pixelkoordinaten (Gleitkomma).
struct Point2
{
    double x = 0.0;
    double y = 0.0;

    friend auto operator<=>(const Point2&, const Point2&) = default;
};

/// Bildgröße in Pixeln.
struct Size
{
    int width = 0;
    int height = 0;

    friend auto operator<=>(const Size&, const Size&) = default;
};

/// 3x3-Matrix, zeilenweise gespeichert (Farbmatrizen).
struct Mat3
{
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

    [[nodiscard]] static Mat3 identity() noexcept { return {}; }
    [[nodiscard]] static Mat3 diagonal(const Vec3& d) noexcept;

    [[nodiscard]] double operator()(int row, int col) const noexcept
    {
        return m[static_cast<std::size_t>(row * 3 + col)];
    }
    /// Verkettung: (a * b)(v) = a(b(v)).
    friend Mat3 operator*(const Mat3& a, const Mat3& b) noexcept;
    friend Vec3 operator*(const Mat3& a, const Vec3& v) noexcept;

    friend bool operator==(const Mat3&, const Mat3&) = default;
};

/// Affine Abbildung der Ebene: x' = a*x + b*y + c, y' = d*x + e*y + f.
/// Gespeichert als {a, b, c, d, e, f}.
struct Mat2x3
{
    std::array<double, 6> m{1, 0, 0, 0, 1, 0};

    [[nodiscard]] static Mat2x3 identity() noexcept { return {}; }
    [[nodiscard]] static Mat2x3 translation(double tx, double ty) noexcept;
    [[nodiscard]] static Mat2x3 scale(double sx, double sy) noexcept;
    /// Drehung um den Ursprung, Winkel in Grad, mathematisch positiv.
    [[nodiscard]] static Mat2x3 rotation(double degrees) noexcept;

    [[nodiscard]] Point2 apply(const Point2& p) const noexcept;
    /// Umkehrabbildung; leer, wenn die Abbildung nicht umkehrbar ist.
    [[nodiscard]] std::optional<Mat2x3> inverse() const noexcept;

    /// Verkettung: (a * b)(p) = a(b(p)).
    friend Mat2x3 operator*(const Mat2x3& a, const Mat2x3& b) noexcept;

    friend bool operator==(const Mat2x3&, const Mat2x3&) = default;
};

} // namespace autoneg::base
