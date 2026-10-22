#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/pixel_program.hpp"
#include "autoneg/image/warp_plan.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/param_set.hpp"
#include "autoneg/pipeline/render.hpp"
#include "autoneg/pipeline/step.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace autoneg::pipeline {

enum class StageKind
{
    Pixel,
    Warp,
    Region,
    Output
};

/// Ein Speicherdurchlauf der kompilierten Pipeline (Architektur 9.4).
class Stage
{
public:
    virtual ~Stage() = default;

    [[nodiscard]] virtual StageKind kind() const noexcept = 0;
    /// Inhalt der Stufe (Parameter, Programm …); Grundlage der Cache-Schlüssel.
    [[nodiscard]] virtual std::uint64_t contentHash() const = 0;
    [[nodiscard]] virtual int halo() const noexcept { return 0; }
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> run(ExecContext& x,
                                                                 const image::ImageBuffer& in) const = 0;

    /// Kachelausführung: in ist der Ausschnitt inRoi des Eingangsbildes; Ergebnis ist der
    /// Ausschnitt outRoi des Ausgangsbildes. Standard: run und auf outRoi zuschneiden.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> runRegion(ExecContext& x,
                                                                       const image::ImageBuffer& in,
                                                                       const image::Roi& inRoi,
                                                                       const image::Roi& outRoi) const;
    /// Benötigter Eingabeausschnitt für den Ausgabeausschnitt out (Bild der Größe inSize).
    [[nodiscard]] virtual image::Roi requiredInput(const image::Roi& out, base::Size inSize) const;
    [[nodiscard]] virtual base::Size outputSize(base::Size in) const { return in; }

    /// Kennung des Schlüssels in der Kette; vom Compiler gesetzt.
    [[nodiscard]] std::uint64_t key() const noexcept { return key_; }
    /// Schritte, die diese Stufe ausführt (in Reihenfolge).
    [[nodiscard]] std::span<const model::StepId> steps() const noexcept { return steps_; }
    /// Nach dieser Stufe liegt ein Cache-Punkt.
    [[nodiscard]] bool cacheAfter() const noexcept { return cacheAfter_; }

private:
    friend class PipelineCompiler;
    std::uint64_t key_ = 0;
    std::vector<model::StepId> steps_;
    bool cacheAfter_ = false;
};

/// Punktprogramm in einem Durchlauf.
class PixelPass final : public Stage
{
public:
    explicit PixelPass(image::PixelProgram program)
        : program_(std::move(program))
    {}
    [[nodiscard]] StageKind kind() const noexcept override { return StageKind::Pixel; }
    [[nodiscard]] std::uint64_t contentHash() const override;
    [[nodiscard]] base::Expected<image::ImageBuffer> run(ExecContext& x,
                                                         const image::ImageBuffer& in) const override;
    [[nodiscard]] const image::PixelProgram& program() const noexcept { return program_; }

private:
    image::PixelProgram program_;
};

/// Zusammengefasste Verzerrung.
class WarpPass final : public Stage
{
public:
    explicit WarpPass(image::WarpPlan plan)
        : plan_(plan)
    {}
    [[nodiscard]] StageKind kind() const noexcept override { return StageKind::Warp; }
    [[nodiscard]] std::uint64_t contentHash() const override;
    [[nodiscard]] base::Expected<image::ImageBuffer> run(ExecContext& x,
                                                         const image::ImageBuffer& in) const override;
    [[nodiscard]] base::Expected<image::ImageBuffer> runRegion(ExecContext& x, const image::ImageBuffer& in,
                                                               const image::Roi& inRoi,
                                                               const image::Roi& outRoi) const override;
    [[nodiscard]] image::Roi requiredInput(const image::Roi& out, base::Size inSize) const override;
    [[nodiscard]] base::Size outputSize(base::Size /*in*/) const override { return plan_.out; }
    [[nodiscard]] const image::WarpPlan& plan() const noexcept { return plan_; }

private:
    image::WarpPlan plan_;
};

/// Eine Stufe mit Nachbarschaftszugriff.
class RegionPass final : public Stage
{
public:
    RegionPass(const RegionStep* step, model::ParamSet params, double scale, base::Size size)
        : step_(step)
        , params_(std::move(params))
        , scale_(scale)
        , size_(size)
    {}
    [[nodiscard]] StageKind kind() const noexcept override { return StageKind::Region; }
    [[nodiscard]] std::uint64_t contentHash() const override;
    [[nodiscard]] int halo() const noexcept override { return step_->halo(params_, scale_, size_); }
    [[nodiscard]] base::Expected<image::ImageBuffer> run(ExecContext& x,
                                                         const image::ImageBuffer& in) const override;
    [[nodiscard]] image::Roi requiredInput(const image::Roi& out, base::Size inSize) const override;

private:
    const RegionStep* step_;
    model::ParamSet params_;
    double scale_;
    base::Size size_;
};

/// Farbtransformation in den Ausgabefarbraum.
class OutputPass final : public Stage
{
public:
    explicit OutputPass(std::shared_ptr<const ports::IColorTransform> xf)
        : xf_(std::move(xf))
    {}
    [[nodiscard]] StageKind kind() const noexcept override { return StageKind::Output; }
    [[nodiscard]] std::uint64_t contentHash() const override;
    [[nodiscard]] base::Expected<image::ImageBuffer> run(ExecContext& x,
                                                         const image::ImageBuffer& in) const override;

private:
    std::shared_ptr<const ports::IColorTransform> xf_;
};

/// Unveränderlicher Ausführungsplan eines Bildes (Architektur 9.4).
class CompiledPipeline
{
public:
    std::vector<std::unique_ptr<Stage>> stages;
    std::vector<std::uint64_t> keys; ///< keys[i] == stages[i]->key()
    RenderProfile profile;
    /// Hinweise der Kompilierung, z. B. übersprungene unbekannte Schritte (U-PIP-17).
    std::vector<std::string> warnings;

    /// Summe der Ränder aller Nachbarschaftsstufen.
    [[nodiscard]] int totalHalo() const noexcept;
    /// Index der ersten Stufe, die sich von o unterscheidet; = Anzahl Stufen bei Gleichheit.
    [[nodiscard]] std::size_t firstDiff(const CompiledPipeline& o) const noexcept;
};

} // namespace autoneg::pipeline
