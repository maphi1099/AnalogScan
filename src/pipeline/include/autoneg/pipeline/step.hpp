#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/cancel_token.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/image_spec.hpp"
#include "autoneg/image/tile_grid.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/param_set.hpp"
#include "autoneg/ports/compute_backend.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace autoneg::pipeline {

class PixelProgramBuilder;
class WarpBuilder;

/// Phase der Stufenfolge (Architektur 5.2).
enum class Phase
{
    Source,     ///< Quelle, Leuchtflächenkorrektur
    Geometry,   ///< Geometrie und Retusche
    Conversion, ///< Konvertierung im Dichteraum
    Print,      ///< Abzug
    Finishing,  ///< Bildgestaltung
    Output      ///< Detail und Ausgabe
};

/// Art eines Schritts (Architektur 9.3).
enum class StepNature
{
    Control,   ///< verändert keine Pixel, wählt Varianten (z. B. Filmtyp)
    Pointwise, ///< punktweise, wird zu einem Punktprogramm zusammengefasst
    Geometric, ///< geometrisch, wird zu einer Verzerrung zusammengefasst
    Region     ///< mit Nachbarschaftszugriff, eigener Durchlauf
};

/// Menge von Filmtypen, für die ein Schritt aktiv ist (Tabelle 1 der Architektur).
class FilmTypeMask
{
public:
    constexpr FilmTypeMask() noexcept = default;
    constexpr FilmTypeMask(std::initializer_list<model::FilmType> types) noexcept
    {
        for (const model::FilmType t : types) {
            bits_ |= bit(t);
        }
    }

    [[nodiscard]] static constexpr FilmTypeMask all() noexcept
    {
        return {model::FilmType::ColorNegative, model::FilmType::BwNegative, model::FilmType::Slide};
    }
    [[nodiscard]] constexpr bool contains(model::FilmType t) const noexcept { return (bits_ & bit(t)) != 0; }

    friend constexpr bool operator==(FilmTypeMask, FilmTypeMask) noexcept = default;

private:
    static constexpr std::uint8_t bit(model::FilmType t) noexcept
    {
        return static_cast<std::uint8_t>(1U << static_cast<unsigned>(t));
    }

    std::uint8_t bits_ = 0;
};

enum class ParamType
{
    Double,
    Int,
    Bool,
    String,
    Vec3,
    Rect,
    Curve,
    Spots
};

/// Beschreibung eines Parameters mit Neutralwert (NF-ARC-04: Parameterschema).
struct ParamSpec
{
    std::string name;
    ParamType type = ParamType::Double;
    model::ParamValue neutral;
    double min = 0.0; ///< nur bei Double/Int; min == max: ohne Grenzen
    double max = 0.0;
    std::string unit;
};

/// Beschreibung eines Schritts (Architektur 9.3).
struct StepDescriptor
{
    model::StepId id;
    int version = 1;
    int rank = 0; ///< Position in der festen Stufenfolge
    std::string name;
    Phase phase = Phase::Finishing;
    std::vector<ParamSpec> params;
    FilmTypeMask films = FilmTypeMask::all();
    bool visible = true; ///< interne Stufen (z. B. Dichte) erscheinen nicht in der Bearbeitungsliste
    /// Auch ohne Eintrag mit Standardparametern aktiv (z. B. Tonwertgrenzen, S/W-Mischung), sofern
    /// der Filmtyp die Stufe vorsieht. Interne Stufen sind immer so. Ergänzung zu 9.3.
    bool alwaysActive = false;
    /// Nach dieser Stufe liegt ein Cache-Punkt (C1…C4, Architektur 5.2). Ergänzung zu 9.3.
    bool cacheAfter = false;
    /// Backends mit eigener Implementierung (NF-ARC-04/-05, L-03). Ergänzung zu 9.3.
    std::vector<std::string> backends{"cpu"};
};

/// Zustand während der Kompilierung; Steuerstufen dürfen ihn ändern.
struct StepContext
{
    model::FilmType film = model::FilmType::ColorNegative;
    double scale = 1.0; ///< Breite der verarbeiteten Quelle / Breite des Originals
    image::ImageSpec source;
    std::optional<base::Vec3> base;
};

/// Laufzeitangaben für Nachbarschaftsstufen.
struct ExecContext
{
    ports::IComputeBackend& backend;
    image::CancelToken cancel;
    double scale = 1.0;
    /// Bei Kachelausführung: Lage des Eingabeausschnitts (padded) und des Ergebnisbereichs
    /// (core) im Bild dieser Stufe.
    std::optional<image::Tile> tile;
    /// Größe des ganzen Bildes dieser Stufe (auch bei Kachelausführung).
    base::Size imageSize;

    [[nodiscard]] ports::KernelContext kernel() const { return ports::KernelContext{cancel}; }
};

/// Einheitliche Schnittstelle aller Verarbeitungsschritte (NF-ARC-04). Schritte sind
/// zustandslos und damit beliebig parallel nutzbar.
class IStep
{
public:
    virtual ~IStep() = default;

    [[nodiscard]] virtual const StepDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual StepNature nature() const noexcept = 0;
    /// Neutrale Parameter erzeugen keine Stufe. Standard: alle Werte gleich ihrem Neutralwert.
    [[nodiscard]] virtual bool isNeutral(const model::ParamSet& p) const;
    /// Standard: Typen und Grenzen laut ParamSpec.
    [[nodiscard]] virtual base::Expected<void> validate(const model::ParamSet& p) const;
};

/// Steuerstufe: wählt Varianten, verändert keine Pixel.
class ControlStep : public IStep
{
public:
    [[nodiscard]] StepNature nature() const noexcept final { return StepNature::Control; }
    virtual void configure(StepContext& ctx, const model::ParamSet& p) const = 0;
};

/// Punktweise Stufe: trägt ihre Rechenschritte in das gemeinsame Punktprogramm ein.
class PointwiseStep : public IStep
{
public:
    [[nodiscard]] StepNature nature() const noexcept final { return StepNature::Pointwise; }
    virtual void emit(PixelProgramBuilder& b, const model::ParamSet& p, const StepContext& ctx) const = 0;
};

/// Geometrische Stufe: trägt ihre Abbildung in die gemeinsame Verzerrung ein.
class GeometricStep : public IStep
{
public:
    [[nodiscard]] StepNature nature() const noexcept final { return StepNature::Geometric; }
    virtual void emit(WarpBuilder& b, const model::ParamSet& p, const StepContext& ctx) const = 0;
};

/// Stufe mit Nachbarschaftszugriff: eigener Durchlauf, kachelfähig (NF-ARC-08).
class RegionStep : public IStep
{
public:
    [[nodiscard]] StepNature nature() const noexcept final { return StepNature::Region; }
    /// Benötigter Rand in Pixeln bei der Skalierung scale für ein Stufenbild der Größe image
    /// (Ergänzung zu 9.3: normierte Angaben wie Retuschestellen brauchen die Bildgröße).
    [[nodiscard]] virtual int halo(const model::ParamSet& p, double scale, base::Size image) const = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    process(ExecContext& ctx, const image::ImageBuffer& in, const model::ParamSet& p) const = 0;
};

/// Prüft Typen und Grenzen gegen die ParamSpecs; unbekannte Parameter sind erlaubt
/// (spätere Versionen können Parameter ergänzen).
[[nodiscard]] base::Expected<void> validateParams(const StepDescriptor& d, const model::ParamSet& p);
/// Alle Parameter gleich ihrem Neutralwert; fehlende gelten als neutral.
[[nodiscard]] bool paramsNeutral(const StepDescriptor& d, const model::ParamSet& p);

} // namespace autoneg::pipeline
