#pragma once

#include "autoneg/image/image_spec.hpp"
#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/pipeline/compiled_pipeline.hpp"
#include "autoneg/pipeline/render.hpp"
#include "autoneg/pipeline/step_registry.hpp"

#include <optional>

namespace autoneg::pipeline {

struct CompileOptions
{
    /// Teilausführung bis einschließlich dieses Schritts (Abgriffpunkte A1/A2, U-PIP-14).
    std::optional<model::StepId> stopAfter;
    /// Aufeinanderfolgende P- bzw. G-Stufen zusammenfassen (AE-05); aus nur für Tests (U-PIP-06).
    bool fuse = true;
};

/// Übersetzt eine Bearbeitungsliste in einen Ausführungsplan (Architektur 5.1, 9.4):
///  1. neutrale, ausgeschaltete und unbekannte Einträge entfallen (unbekannte mit Hinweis);
///     interne Stufen (visible = false) sind ohne Eintrag aktiv;
///  2. Steuerstufen legen den Filmtyp fest; Stufen, die er nicht vorsieht, entfallen;
///  3. aufeinanderfolgende P-Stufen werden ein Punktprogramm, G-Stufen eine Verzerrung;
///  4. jede N-Stufe ist ein eigener Durchlauf; bei useCache trennen Cache-Punkte die Gruppen.
///
/// Ergänzung zu 9.4: compile erhält zusätzlich die Beschreibung der Quelle und die
/// Skalierung (StepContext) sowie Optionen.
class PipelineCompiler
{
public:
    explicit PipelineCompiler(const StepRegistry& registry)
        : registry_(registry)
    {}

    [[nodiscard]] CompiledPipeline compile(const model::EditList& l, model::FilmType f,
                                           const RenderProfile& p, const image::ImageSpec& source,
                                           double scale = 1.0, const CompileOptions& options = {}) const;

private:
    const StepRegistry& registry_;
};

} // namespace autoneg::pipeline
