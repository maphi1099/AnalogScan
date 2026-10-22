#pragma once

#include "autoneg/analysis/region_map.hpp"
#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/values.hpp"
#include "autoneg/ports/vision.hpp"

#include <vector>

namespace autoneg::analysis {

/// Ergebnis der Bildfelderkennung (Architektur 9.7, LF-GEO-01…06).
struct FrameDetection
{
    /// Bildfeld normiert auf den entzerrten Scan (gleiche Größe wie das Quellbild); passt als
    /// Parameter rect von autoneg.crop hinter autoneg.rotation.
    model::NormRect frame;
    /// Korrekturwinkel in Grad für autoneg.rotation (= −Schräglage des Films).
    double angleDeg = 0.0;
    RegionMap regions;
    double confidence = 0.0; ///< 0…1: Trennschärfe zwischen Bildfeld und Umgebung
};

/// Schätzt die Schräglage des Films (LF-GEO-03). Ergebnis: Korrekturwinkel in Grad für
/// autoneg.rotation, gesucht im Bereich ±10°.
///
/// Grobwinkel aus IVision (Kanten, Linien), dann Feinwinkel aus einer robusten Geraden-
/// anpassung an subpixelgenaue Kantenpunkte entlang der längsten waagerechten bzw.
/// senkrechten Kanten. Zustandslos, threadsicher, wenn IVision es ist.
class SkewEstimator
{
public:
    explicit SkewEstimator(ports::IVision& vision)
        : vision_(vision)
    {}

    [[nodiscard]] base::Expected<double> estimate(const image::ImageBuffer& img) const;

private:
    ports::IVision& vision_;
};

/// Bildfelderkennung (LF-GEO-01, -02, -05, -06).
///
/// Ablauf: Schräglage schätzen und entzerren; Lichtpegel aus den hellsten Bereichen
/// (Leuchtfläche, Perforation); Filmstreifen über Zeilen- und Spaltenprofile „Licht“; im
/// Streifen das belichtete Bildfeld als längster Lauf belichteter Zeilen bzw. Spalten
/// (Negativ: Dichte über der Basis; Dia: Transmission über dem Filmrand); Kanten auf
/// Subpixel am halben Pegel des Profils. Zustandslos, threadsicher, wenn IVision es ist.
class FrameDetector
{
public:
    explicit FrameDetector(ports::IVision& vision)
        : vision_(vision)
    {}

    /// Größtes Bildfeld. Fehler NotFound mit Grund, wenn kein Bildfeld erkennbar ist (LF-GEO-05).
    [[nodiscard]] base::Expected<FrameDetection> detect(const image::ImageBuffer& img,
                                                        model::FilmType film) const;
    /// Alle Bildfelder eines Streifens, von links nach rechts bzw. oben nach unten (LF-GEO-06).
    /// Ergänzung K-M5-01.
    [[nodiscard]] base::Expected<std::vector<FrameDetection>> detectAll(const image::ImageBuffer& img,
                                                                        model::FilmType film) const;

private:
    ports::IVision& vision_;
};

} // namespace autoneg::analysis
