#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/pixel_program.hpp"

#include <memory>

namespace autoneg::pipeline {

/// Baut aus den Beiträgen aufeinanderfolgender punktweiser Stufen ein Punktprogramm
/// (Architektur 5.6). Fasst dabei zusammen: Matrix·Matrix, Matrix·Skalierung,
/// Skalierung·Skalierung, Verschiebung+Verschiebung, Begrenzung∩Begrenzung.
///
/// Bedeutung der Operationen je Pixel (RGB; bei einem Kanal wirkt jeweils nur die erste
/// Komponente): matrix: v = M·v; offset: v = v + o; scale: v = s·v (kanalweise);
/// log10: v = log10(max(v, 1e-6)); exp10: v = 10^v; lut: Tabelle, Eingabe auf
/// [inMin, inMax] begrenzt, linear interpoliert; clamp: v = min(max(v, lo), hi).
class PixelProgramBuilder
{
public:
    PixelProgramBuilder& matrix(const base::Mat3& m);
    PixelProgramBuilder& offset(const base::Vec3& v);
    PixelProgramBuilder& scale(const base::Vec3& v);
    PixelProgramBuilder& log10();
    PixelProgramBuilder& exp10();
    PixelProgramBuilder& lut(image::Lut1D l);
    PixelProgramBuilder& lut(image::LutRef l);
    PixelProgramBuilder& clamp(float lo, float hi);

    [[nodiscard]] bool empty() const noexcept { return program_.ops.empty(); }
    [[nodiscard]] image::PixelProgram build() const { return program_; }

private:
    image::PixelProgram program_;
};

} // namespace autoneg::pipeline
