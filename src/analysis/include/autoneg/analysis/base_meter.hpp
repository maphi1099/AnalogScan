#pragma once

#include "autoneg/analysis/region_map.hpp"
#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/values.hpp"

namespace autoneg::analysis {

/// Herkunft eines Basiswerts (LF-BAS-01, -04, -05).
enum class BaseMethod
{
    Rebate,     ///< automatisch im Filmrand gemessen
    Manual,     ///< Messrechteck bzw. Pipette
    Roll,       ///< aus einem anderen Bild der Rolle
    Preset,     ///< gespeicherter, benannter Basiswert
    Statistical ///< aus dem Bildinhalt geschätzt
};

/// Gemessene Filmbasis (Architektur 9.7).
struct BaseMeasurement
{
    base::Vec3 transmission; ///< Transmission je Kanal (linear, Quellwerte)
    base::Vec3 spread;       ///< robuste Streuung je Kanal (MAD × 1,4826)
    int samples = 0;
    BaseMethod method = BaseMethod::Rebate;
};

/// Basismessung im unbelichteten Filmrand (LF-BAS-01, -02, -04).
///
/// Robust gegen Randbeschriftung, DX-Code, Perforation, Staub und Lichtlecks: Perforation
/// und Leuchtfläche gehören nicht zur Klasse Rebate; Pixel nahe einer Klassengrenze werden
/// verworfen; danach Ausreißer über Median und MAD der Luminanz (3 σ), Ergebnis der Median
/// je Kanal.
class BaseMeter
{
public:
    [[nodiscard]] base::Expected<BaseMeasurement> measure(const image::ImageBuffer& img,
                                                          const RegionMap& m) const;
    /// Robuster Mittelwert im Rechteck (normiert auf das Quellbild), Methode Manual.
    [[nodiscard]] base::Expected<BaseMeasurement> measureRect(const image::ImageBuffer& img,
                                                              const model::NormRect& r) const;
};

/// Statistische Basisschätzung ohne Filmrand (LF-BAS-05 c): die am wenigsten dichten Stellen
/// eines Negativs liegen nahe der Basis. Blockmittel 8 × 8, je Kanal das 99,5-%-Perzentil.
class BaseEstimator
{
public:
    [[nodiscard]] BaseMeasurement estimate(const image::ImageBuffer& img) const;
};

} // namespace autoneg::analysis
