#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/pixel_program.hpp"
#include "autoneg/ports/image_io.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>

namespace autoneg::ports {

/// Mitgelieferte Profile (LF-EXP-05).
enum class ProfileId
{
    WorkingRec2020Linear, ///< Arbeitsfarbraum (AE-11)
    Srgb,
    AdobeRgb,
    ProPhoto,
    GrayGamma22
};

/// Rendering Intent nach ICC.
enum class Intent
{
    Perceptual,
    RelativeColorimetric,
    Saturation,
    AbsoluteColorimetric
};

/// Geschlossene Form einer Matrix-Kurven-Transformation mit linearem Eingang (D-18):
///   lin = M · rgb (zeilenweise 3×3; bei Grau nur die erste Zeile),
///   out = lin ≥ t ? k1 · lin^p + k0 : s · lin.
/// Die Kodierkurve deckt Gamma (t = 0, s = 0) und sRGB (ICC-Parameterkurve Typ 4, invertiert) ab.
/// Ein Backend darf statt apply diese Formel rechnen (SIMD); der Adapter liefert sie nur, wenn
/// sie seine eigene Rechnung innerhalb von 1e-5 trifft.
struct MatrixShaper
{
    int outChannels = 3; ///< 3 = RGB, 1 = Grau
    std::array<float, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t = 0.0F;
    float p = 1.0F;
    float k1 = 1.0F;
    float k0 = 0.0F;
    float s = 0.0F;

    /// Skalare Auswertung eines Werts der Kodierkurve (Referenz für Tests und Restschleifen).
    [[nodiscard]] float encode(float lin) const
    {
        return lin >= t ? k1 * std::pow(lin, p) + k0 : s * lin;
    }
};

/// Fertige Farbtransformation. Nach der Erzeugung unveränderlich und threadsicher.
class IColorTransform
{
public:
    virtual ~IColorTransform() = default;

    [[nodiscard]] virtual base::Expected<image::ImageBuffer> apply(const image::ImageBuffer& in) const = 0;
    /// Tabelle für die Ausgabestufe im Punktprogramm (Architektur 5.2, Durchlauf 9).
    [[nodiscard]] virtual image::Lut3D lut3d(int n) const = 0;
    /// Geschlossene Form, falls die Transformation eine Matrix-Kurven-Transformation ist.
    [[nodiscard]] virtual std::optional<MatrixShaper> matrixShaper() const { return std::nullopt; }
};

/// Farbmanagement (LF-EXP-05, LF-UI-06). Adapter: LcmsColorEngine. Threadsicher.
class IColorEngine
{
public:
    virtual ~IColorEngine() = default;

    [[nodiscard]] virtual base::Expected<IccProfile> profile(ProfileId id) = 0;
    /// Profil des Bildschirms mit dem Index screen (Systemprofil).
    [[nodiscard]] virtual base::Expected<IccProfile> display(int screen) = 0;
    [[nodiscard]] virtual base::Expected<std::shared_ptr<const IColorTransform>>
    transform(const IccProfile& src, const IccProfile& dst, Intent intent) = 0;
};

} // namespace autoneg::ports
