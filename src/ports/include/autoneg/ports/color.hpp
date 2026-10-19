#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/pixel_program.hpp"
#include "autoneg/ports/image_io.hpp"

#include <memory>

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

/// Fertige Farbtransformation. Nach der Erzeugung unveränderlich und threadsicher.
class IColorTransform
{
public:
    virtual ~IColorTransform() = default;

    [[nodiscard]] virtual base::Expected<image::ImageBuffer> apply(const image::ImageBuffer& in) const = 0;
    /// Tabelle für die Ausgabestufe im Punktprogramm (Architektur 5.2, Durchlauf 9).
    [[nodiscard]] virtual image::Lut3D lut3d(int n) const = 0;
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
