#pragma once

// Skalare Referenzimplementierungen aller Rechenkernels (CLAUDE.md: "Jeder optimierte Kernel
// braucht eine einfache skalare Referenzimplementierung"). Einkernig, ohne SIMD, so direkt wie
// möglich. Sie sind die verbindliche Definition des Ergebnisses; das CpuBackend muss innerhalb
// der Toleranzen der Tests U-BCK-* übereinstimmen.
//
// Definitionen (für alle Kernels gilt: Eingabe F32 mit 1 oder 3 Kanälen, Ausgabe F32):
//  - Punktprogramm: siehe pipeline::PixelProgramBuilder.
//  - warp: Ausgabepixel (x, y) liest an s = A·(x + 0,5; y + 0,5) − 0,5 (Pixelindex). Liegt s
//    außerhalb von [−0,5; B − 0,5] bzw. [−0,5; H − 0,5], ist das Ergebnis 0. Ist A eine exakte
//    Achsenabbildung (Einträge 0/±1, ganzzahlige Lage), wird verlustfrei kopiert. Sonst Filter
//    mit Randwiederholung; Lanczos3 und Bicubic (a = −0,5) mit normierten Gewichten.
//  - resample: getrennt horizontal/vertikal, Lanczos3, beim Verkleinern um den Faktor verbreitert,
//    Quelle s = (x + 0,5)·Faktor − 0,5, Randwiederholung, normierte Gewichte.
//  - gaussian: Radius ceil(3σ), Gewichte exp(−k²/2σ²) normiert, erst horizontal, dann vertikal,
//    Randwiederholung; σ <= 0: unverändert.
//  - unsharp(a, r, t): blur = gaussian(σ = r); d = in − blur; |d| > t ? in + a·d : in.
//  - multiply: in · gain; gain mit 1 Kanal oder gleich vielen Kanälen, gleiche Größe.
//  - localTone(s, h, r): L = 0,2627R + 0,6780G + 0,0593B (1 Kanal: L = v); B = Guided Filter
//    von L mit sich selbst (Box-Radius q = max(1; round(r)), ε = 0,01, Box mit
//    Randwiederholung über (2q + 1)² Werte): a = var/(var + ε), b = mean − a·mean,
//    B = box(a)·L + box(b). Faktor = (1 + s·2·max(0; 0,5 − B))·(1 − h·max(0; B − 0,5));
//    out = in·Faktor. Kantenerhaltend, damit keine Halos entstehen (U-STP-19).
//  - denoise(strength): bilateral 5×5, σs = 1,5, σr = 0,02 + 0,2·strength, Abstand über alle
//    Kanäle; strength <= 0: unverändert.
//  - inpaint(spots): je Stelle nacheinander: R = radius·B; für Pixel im Abstand d < R:
//    f = 1 bei d <= 0,7R, linear auf 0 bei R; out = (1 − f)·in(p) + f·in(p + Versatz·(B, H)).
//  - histogram(bins): Kanäle R, G, B, Luminanz (bei 1 Kanal alle gleich), Bin = floor(v·bins)
//    begrenzt auf [0, bins − 1]; clipLow/High = Anteil v <= 0 bzw. v >= 1 je Kanal.

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/histogram.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/pixel_program.hpp"
#include "autoneg/image/warp_plan.hpp"
#include "autoneg/model/values.hpp"

#include <array>
#include <vector>

namespace autoneg::reference {

/// Wendet ein Punktprogramm auf einen Pixel an (RGB; bei channels == 1 nur v[0]).
std::array<float, 3> applyProgram(const image::PixelProgram& p, std::array<float, 3> v, int channels);

image::ImageBuffer runPixel(const image::PixelProgram& p, const image::ImageBuffer& in);
image::ImageBuffer warp(const image::ImageBuffer& in, const image::WarpPlan& plan);
image::ImageBuffer resample(const image::ImageBuffer& in, base::Size size);
image::ImageBuffer multiply(const image::ImageBuffer& in, const image::ImageBuffer& gain);
image::ImageBuffer gaussian(const image::ImageBuffer& in, float sigma);
image::ImageBuffer unsharp(const image::ImageBuffer& in, float amount, float radius, float threshold);
image::ImageBuffer localTone(const image::ImageBuffer& in, float shadows, float highlights, float radius);
image::ImageBuffer denoise(const image::ImageBuffer& in, float strength);
image::ImageBuffer inpaint(const image::ImageBuffer& in, const model::SpotList& spots);
image::Histogram histogram(const image::ImageBuffer& in, int bins);

/// Gauß-Gewichte nach obiger Definition (Länge 2·ceil(3σ) + 1).
std::vector<float> gaussianWeights(float sigma);

} // namespace autoneg::reference
