#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/pipeline/compiled_pipeline.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/render.hpp"
#include "autoneg/pipeline/source_provider.hpp"
#include "autoneg/pipeline/stage_cache.hpp"
#include "autoneg/pipeline/tile_planner.hpp"
#include "autoneg/ports/compute_backend.hpp"

#include <optional>

namespace autoneg::pipeline {

/// Wie ein kompilierter Plan ausgeführt wird.
enum class Execution
{
    Auto,  ///< nach Profil: Export und Detail kachelweise, sonst am Stück
    Whole, ///< Stufe für Stufe über das ganze Bild, mit Cache (runWhole)
    Tiled  ///< jede Kachel durch alle Stufen (runTiled)
};

/// Führt Render-Anfragen aus (Architektur 9.4).
///
/// Vorschau, Miniatur, Analyse: halbe Auflösung (Stufe 1), auf die Zielgröße verkleinert,
/// Stufe für Stufe mit Zwischenbildern im StageCache an den Cache-Punkten (wenn useCache).
/// Export: volle Auflösung, kachelweise (NF-ARC-08, NF-PERF-07). Detail: volle Auflösung,
/// nur der Ausschnitt profile.roi.
/// Abbruch über das CancelToken der Anfrage, geprüft zwischen Stufen und Kacheln.
/// Alle Zwischenbilder müssen F32 sein (LF-KON-07); sonst Fehler Internal.
///
/// Threadsicher, solange Backend, Cache und SourceProvider es sind. Die Kacheln laufen
/// nacheinander; parallel rechnen die Kernels innerhalb einer Kachel (Ebene 1). Parallele
/// Kacheln (Ebene 2) folgen mit dem JobScheduler.
class PipelineExecutor
{
public:
    PipelineExecutor(const PipelineCompiler& compiler, ports::IComputeBackend& backend, StageCache& cache,
                     SourceProvider& sources, TilePlanner tiles = TilePlanner{})
        : compiler_(compiler)
        , backend_(backend)
        , cache_(cache)
        , sources_(sources)
        , tiles_(tiles)
    {}

    [[nodiscard]] base::Expected<image::ImageBuffer> execute(const RenderRequest& r);

    /// Führt einen bereits kompilierten Plan auf einer gegebenen Quelle aus (ohne
    /// SourceProvider; für Analysen und Tests). Der Cache wird nur bei Execution::Whole
    /// bzw. Auto mit useCache genutzt.
    [[nodiscard]] base::Expected<image::ImageBuffer> run(const CompiledPipeline& cp,
                                                         const image::ImageBuffer& src,
                                                         const RenderRequest& r,
                                                         Execution e = Execution::Auto, double scale = 1.0);

private:
    base::Expected<image::ImageBuffer> runWhole(const CompiledPipeline& cp, const image::ImageBuffer& src,
                                                const RenderRequest& r, double scale);
    base::Expected<image::ImageBuffer> runTiled(const CompiledPipeline& cp, const image::ImageBuffer& src,
                                                const RenderRequest& r, double scale,
                                                const std::optional<image::Roi>& region);

    const PipelineCompiler& compiler_;
    ports::IComputeBackend& backend_;
    StageCache& cache_;
    SourceProvider& sources_;
    TilePlanner tiles_;
};

} // namespace autoneg::pipeline
