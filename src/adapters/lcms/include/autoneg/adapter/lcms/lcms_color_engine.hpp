#pragma once

#include "autoneg/ports/color.hpp"

#include <array>
#include <memory>

namespace autoneg::adapter::lcms {

/// Farbmanagement mit Little CMS 2 (Architektur 9.9, LF-EXP-05).
///
/// Die mitgelieferten Profile (Arbeitsfarbraum linear Rec. 2020, sRGB, Adobe RGB, ProPhoto,
/// Graustufen Gamma 2,2) werden beim Erzeugen im Speicher gebaut. Transformationen rechnen in
/// float ohne Zwischenspeicher (cmsFLAGS_NOCACHE) und sind damit nach der Erzeugung
/// threadsicher. Die Engine selbst ist threadsicher (eigener Little-CMS-Kontext, Profile
/// unveränderlich).
class LcmsColorEngine final : public ports::IColorEngine
{
public:
    LcmsColorEngine();
    ~LcmsColorEngine() override;
    LcmsColorEngine(const LcmsColorEngine&) = delete;
    LcmsColorEngine& operator=(const LcmsColorEngine&) = delete;
    LcmsColorEngine(LcmsColorEngine&&) = delete;
    LcmsColorEngine& operator=(LcmsColorEngine&&) = delete;

    [[nodiscard]] base::Expected<ports::IccProfile> profile(ports::ProfileId id) override;
    /// Systemprofil des Bildschirms: folgt mit der Oberfläche (M8); bis dahin Unsupported,
    /// der Aufrufer fällt auf sRGB zurück.
    [[nodiscard]] base::Expected<ports::IccProfile> display(int screen) override;
    [[nodiscard]] base::Expected<std::shared_ptr<const ports::IColorTransform>>
    transform(const ports::IccProfile& src, const ports::IccProfile& dst, ports::Intent intent) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace autoneg::adapter::lcms
