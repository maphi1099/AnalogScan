#pragma once

#include "autoneg/base/geometry.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace autoneg::image {

/// Eindimensionale Wertetabelle, linear interpoliert zwischen inMin und inMax
/// (z. B. Papierkennlinie mit 4096 Stützstellen, Architektur 5.7).
struct Lut1D
{
    std::vector<float> values;
    float inMin = 0.0F;
    float inMax = 1.0F;
    /// Kanal, auf den die Tabelle wirkt; -1 = alle Kanäle. Ergänzung zu Architektur 9.5,
    /// nötig für Kurven je Kanal (LF-EDT-08).
    int channel = -1;

    friend bool operator==(const Lut1D&, const Lut1D&) = default;
};

/// Dreidimensionale Farbtabelle (z. B. Ausgabetransformation), size^3 Einträge zu je 3 Werten,
/// Index (r * size + g) * size + b.
/// 3D-Tabelle mit size³ Stützstellen auf [0, 1]³, je drei Werte. Belegung: Index
/// ((b · size + g) · size + r) · 3 + Kanal, R läuft am schnellsten.
struct Lut3D
{
    int size = 0;
    std::vector<float> data;

    friend bool operator==(const Lut3D&, const Lut3D&) = default;
};

/// Bausteine eines Punktprogramms (Architektur 9.3). Liegen nach A-01 in autoneg::image.
struct Matrix3
{
    base::Mat3 m;
    friend bool operator==(const Matrix3&, const Matrix3&) = default;
};
struct Offset3
{
    base::Vec3 v;
    friend bool operator==(const Offset3&, const Offset3&) = default;
};
struct Scale3
{
    base::Vec3 v;
    friend bool operator==(const Scale3&, const Scale3&) = default;
};
struct Log10
{
    friend bool operator==(const Log10&, const Log10&) = default;
};
struct Exp10
{
    friend bool operator==(const Exp10&, const Exp10&) = default;
};
struct Clamp
{
    float lo = 0.0F;
    float hi = 1.0F;
    friend bool operator==(const Clamp&, const Clamp&) = default;
};

/// Sättigung und Dynamik (LF-EDT-09), Ergänzung zu Architektur 9.3: nicht mit Matrizen
/// ausdrückbar, weil die Dynamik von der Sättigung des Pixels abhängt. Je RGB-Pixel:
/// L = 0,2627R + 0,6780G + 0,0593B; c = v − L; m = min(1; max|c| / max(L; 1e-4));
/// v' = L + (1 + saturation)·(1 + vibrance·(1 − m))·c. Bei einem Kanal ohne Wirkung.
struct Saturate
{
    float saturation = 0.0F;
    float vibrance = 0.0F;
    friend bool operator==(const Saturate&, const Saturate&) = default;
};

/// Tabellen werden geteilt, damit Kopien des Programms billig bleiben.
using LutRef = std::shared_ptr<const Lut1D>;

using PixelOp = std::variant<Matrix3, Offset3, Scale3, Log10, Exp10, LutRef, Clamp, Saturate>;

/// Folge punktweiser Operationen, in einem Speicherdurchlauf je Pixel ausgeführt (AE-05).
struct PixelProgram
{
    std::vector<PixelOp> ops;
};

} // namespace autoneg::image
