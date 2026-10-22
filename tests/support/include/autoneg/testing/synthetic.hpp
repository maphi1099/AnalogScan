#pragma once

// Generator für künstliche Scans (Testdaten "synthetisch" der Testszenarien): aus einem
// bekannten Positiv wird ein Negativ mit bekannter Basis, bekanntem Bildfeld und bekanntem
// Winkel errechnet, eingebettet in Filmrand, Perforation, Randbeschriftung, Filmhalter und
// Leuchtfläche. Alles deterministisch (feste Seeds), nur im Arbeitsspeicher.
//
// Modell (passend zu steps::DensityRangeStep, R = 2):
//   Positiv v ∈ [0, 1] → P = v·(1 − 10^−2) + 10^−2 → D = γ·(log10 P + 2) ∈ [0, 2γ]
//   Negativ: T = Basis · 10^−D; unbelichteter Filmrand: T = Basis.
//   Die Konvertierung mit Basis, dMin = 0 und dMax = 2γ liefert v exakt zurück.
//   Dia: T = v (Rand schwarz).

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/values.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace autoneg::testing {

/// Inhalt des Positivs.
enum class Pattern
{
    Chart,    ///< Graustufen, Farbfelder, Verlauf
    Gray,     ///< nur Graukeil
    Gradient, ///< weicher Verlauf
    Flat      ///< einheitlich 0,5
};

struct NegativeSpec
{
    int width = 600;
    int height = 400;
    model::FilmType film = model::FilmType::ColorNegative;
    /// Bildfeld im (ungedrehten) Scan; Ränder werden auf ganze Pixel gerundet.
    model::NormRect frame{0.1, 0.2, 0.8, 0.6};
    base::Vec3 base{0.71, 0.43,
                    0.28}; ///< Transmission der Filmbasis (Orangemaske; bei S/W neutral mit deren Helligkeit)
    base::Vec3 gamma{0.55, 0.60, 0.65}; ///< Gradation des Negativs je Kanal
    Pattern pattern = Pattern::Chart;
    double angleDeg = 0.0;   ///< Schräglage des ganzen Films, Drehung um die Bildmitte
    bool rebate = true;      ///< Filmrand oberhalb und unterhalb des Bildfelds
    bool perforation = true; ///< Löcher im Filmrand
    bool edgeText = false;   ///< Randbeschriftung (belichtete Zeichen im Filmrand)
    bool holder = true;      ///< dunkler Filmhalter oben und unten
    int dust = 0;            ///< Anzahl Staubkörner
    bool lightLeak = false;  ///< Lichtleck im Filmrand
    std::uint64_t seed = 1;
};

/// Sollwerte zum künstlichen Scan.
struct NegativeTruth
{
    model::NormRect frame;    ///< Bildfeld (vor der Drehung), auf ganze Pixel gerundet
    base::Vec3 base;          ///< Transmission der Basis
    base::Vec3 dMin{0, 0, 0}; ///< Dichtegrenzen nach Basiskorrektur
    base::Vec3 dMax;          ///< = 2γ
    double angleDeg = 0.0;
    model::NormRect rebateTop; ///< Filmrand oberhalb des Bildfelds (vor der Drehung)
    model::NormRect rebateBottom;
};

struct SyntheticScan
{
    image::ImageBuffer scan;
    NegativeTruth truth;
};

/// Positiv an der Stelle (u, w) ∈ [0, 1]² des Bildfelds, linear Rec. 2020.
[[nodiscard]] base::Vec3 positiveAt(Pattern p, double u, double w);
/// Positiv als Bild der Größe w x h (Pixelmitten), z. B. als Vergleich zum zugeschnittenen Ergebnis.
[[nodiscard]] image::ImageBuffer positiveImage(Pattern p, int w, int h, int channels = 3);
[[nodiscard]] SyntheticScan makeNegative(const NegativeSpec& spec);

/// Gleichmäßige Leeraufnahme der Leuchtfläche mit radialem Randabfall (Anteil am Rand).
[[nodiscard]] image::ImageBuffer makeBlank(int w, int h, double falloff, base::Vec3 color = {1.0, 0.95, 0.9});

/// Bild aus einer Funktion (x, y, Kanal) → Wert.
[[nodiscard]] image::ImageBuffer imageFrom(int w, int h, int channels,
                                           const std::function<float(int, int, int)>& f);

/// Mittelwert über ein Rechteck (Pixel).
[[nodiscard]] base::Vec3 meanRgb(const image::ImageBuffer& img, int x, int y, int w, int h);

} // namespace autoneg::testing
