#pragma once

// Zeilenkernels mit Highway (kernels_hwy.cpp). Jede Funktion bearbeitet genau eine Zeile bzw.
// einen zusammenhängenden Wertebereich; die Parallelisierung über Zeilenblöcke übernimmt
// cpu_backend.cpp. Definitionen der Ergebnisse: tests/reference/include/autoneg/reference/kernels.hpp.

#include "autoneg/image/pixel_program.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace autoneg::backend::cpu::detail {

/// Punktprogramm auf einer Zeile: kopiert in nach out und wendet die Operationen an.
void pixelRow(const image::PixelProgram& p, const float* in, float* out, std::size_t pixels, int channels);
/// out = in · gain (gain mit 1 oder channels Kanälen).
void multiplyRow(const float* in, const float* gain, float* out, std::size_t pixels, int channels,
                 int gainChannels);
/// Horizontale Faltung mit Randwiederholung; weights hat 2r + 1 Einträge.
void convolveRowH(const float* in, float* out, int width, int channels, std::span<const float> weights);
/// Vertikale Faltung: rows[k] zeigt auf die Zeile y + k − r (bereits randbegrenzt).
void convolveRowV(const float* const* rows, float* out, std::size_t len, std::span<const float> weights);
/// Unscharfmaskierung: |in − blur| > t ? in + a·(in − blur) : in.
void unsharpRow(const float* in, const float* blur, float* out, std::size_t len, float a, float t);
/// Luminanz (Rec. 2020) einer Zeile; bei einem Kanal Kopie.
void luminanceRow(const float* in, float* lum, std::size_t pixels, int channels);
/// Lichter/Tiefen: out = in · (1 + s·2·max(0; 0,5 − B))·(1 − h·max(0; B − 0,5)).
void toneRow(const float* in, const float* base, float* out, std::size_t pixels, int channels, float s,
             float h);

/// Highway-Zielauswahl (für CpuBackend::targets usw.).
std::int64_t supportedTargets();
std::int64_t compiledTargets();
const char* targetName(std::int64_t bits);
void forceTargets(std::int64_t bits);
void resetTargets();

} // namespace autoneg::backend::cpu::detail
