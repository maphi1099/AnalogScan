#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/values.hpp"
#include "autoneg/pipeline/step.hpp"
#include "autoneg/pipeline/warp_builder.hpp"
#include "autoneg/ports/compute_backend.hpp"

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>

namespace autoneg::steps {

// --- Steuerung ---------------------------------------------------------------------------

/// Filmtyp (LF-KON-01): "color" (Farbnegativ), "bw" (S/W-Negativ), "slide" (Dia).
class FilmModeStep final : public pipeline::ControlStep
{
public:
    FilmModeStep();
    [[nodiscard]] const pipeline::StepDescriptor& descriptor() const noexcept override { return d_; }
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    [[nodiscard]] base::Expected<void> validate(const model::ParamSet& p) const override;
    void configure(pipeline::StepContext& ctx, const model::ParamSet& p) const override;

    [[nodiscard]] static const char* toString(model::FilmType t) noexcept;

private:
    pipeline::StepDescriptor d_;
};

// --- Geometrie ---------------------------------------------------------------------------

class DescribedGeometric : public pipeline::GeometricStep
{
public:
    explicit DescribedGeometric(pipeline::StepDescriptor d)
        : d_(std::move(d))
    {}
    [[nodiscard]] const pipeline::StepDescriptor& descriptor() const noexcept override { return d_; }

private:
    pipeline::StepDescriptor d_;
};

/// Vierteldrehungen und Spiegeln (LF-EDT-02/-03), verlustfrei.
class OrientationStep final : public DescribedGeometric
{
public:
    OrientationStep();
    void emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Feindrehung ±45° (LF-EDT-02); autoFill vergrößert so, dass keine leeren Ecken entstehen.
class RotationStep final : public DescribedGeometric
{
public:
    RotationStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override;
    void emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
    /// Größter achsparalleler Ausschnitt gleichen Seitenverhältnisses in der gedrehten Fläche
    /// (Anteil der Kantenlänge).
    [[nodiscard]] static double fillScale(double width, double height, double degrees);
};

/// Zuschnitt (LF-EDT-01, LF-GEO-04): rect normiert auf die Fläche nach Orientierung und
/// Drehung; inset verkleinert an allen Kanten um inset · kürzere Seite.
class CropStep final : public DescribedGeometric
{
public:
    CropStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override;
    void emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
    /// Größtes Rechteck mit dem Seitenverhältnis aw:ah (in Pixeln) mittig in rect.
    [[nodiscard]] static model::NormRect fitAspect(const model::NormRect& rect, double aw, double ah,
                                                   base::Size canvas);
    /// rect um inset · kürzere Seite (in Pixeln) an allen Kanten verkleinert.
    [[nodiscard]] static model::NormRect applyInset(const model::NormRect& rect, double inset,
                                                    base::Point2 canvas);
};

// --- Nachbarschaft -----------------------------------------------------------------------

class DescribedRegion : public pipeline::RegionStep
{
public:
    explicit DescribedRegion(pipeline::StepDescriptor d)
        : d_(std::move(d))
    {}
    [[nodiscard]] const pipeline::StepDescriptor& descriptor() const noexcept override { return d_; }

private:
    pipeline::StepDescriptor d_;
};

/// Leeraufnahmen der Leuchtfläche und daraus berechnete Verstärkungskarten (LF-BAS-07).
/// Threadsicher. Laden aus den Vorlagen (IPresetRepository) folgt mit der Datenhaltung (M6).
class FlatFieldCache
{
public:
    /// Leeraufnahme (linear, Kamerafarbraum wie die Scans) unter einer Kennung ablegen.
    void add(const std::string& id, image::ImageBuffer blank);
    [[nodiscard]] bool contains(const std::string& id) const;
    /// Verstärkung je Pixel und Kanal in der Größe size: gain = M / glatt(Leeraufnahme), M =
    /// Mittel aller Kanäle in der Bildmitte. Gleicht Randabfall und Lichtfarbe aus.
    [[nodiscard]] base::Expected<std::shared_ptr<const image::ImageBuffer>>
    gainMap(const std::string& id, base::Size size, ports::IComputeBackend& backend,
            const ports::KernelContext& c);

private:
    mutable std::shared_mutex mtx_;
    std::map<std::string, image::ImageBuffer> blanks_;
    std::map<std::pair<std::string, std::pair<int, int>>, std::shared_ptr<const image::ImageBuffer>> gains_;
};

/// Leuchtflächenkorrektur (Abgriffpunkt A1).
class FlatFieldStep final : public DescribedRegion
{
public:
    explicit FlatFieldStep(FlatFieldCache& cache);
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size image) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const override;

private:
    FlatFieldCache& cache_;
};

/// Manuelle Staubretusche (LF-EDT-12).
class RetouchStep final : public DescribedRegion
{
public:
    RetouchStep();
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size image) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const override;
    /// Stellen normiert auf das ganze Bild → normiert auf den Kachelausschnitt.
    [[nodiscard]] static model::SpotList toTile(const model::SpotList& spots, base::Size image,
                                                const image::Roi& tile);
};

/// Lichter und Tiefen (LF-EDT-07), Radius in Pixeln der vollen Auflösung.
class ShadowsHighlightsStep final : public DescribedRegion
{
public:
    ShadowsHighlightsStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override;
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size image) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const override;
};

/// Rauschminderung (LF-EDT-11).
class DenoiseStep final : public DescribedRegion
{
public:
    DenoiseStep();
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size image) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const override;
};

/// Schärfen per Unscharfmaskierung (LF-EDT-10), Radius in Pixeln der vollen Auflösung.
class SharpenStep final : public DescribedRegion
{
public:
    SharpenStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override;
    [[nodiscard]] int halo(const model::ParamSet& p, double scale, base::Size image) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer>
    process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const override;
};

} // namespace autoneg::steps
