#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/image/cancel_token.hpp"
#include "autoneg/image/image_spec.hpp"
#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/task_executor.hpp"

#include <filesystem>
#include <memory>
#include <optional>

namespace autoneg::pipeline {

/// Ausführungsprofil (Architektur 5.5).
enum class RenderMode
{
    Preview,
    Detail,
    Export,
    Thumbnail,
    Analysis
};

/// Ausgabe: Farbtransformation in den Zielfarbraum (leer = Arbeitsfarbraum bleibt).
struct OutputSpec
{
    std::shared_ptr<const ports::IColorTransform> transform;
};

struct RenderProfile
{
    RenderMode mode = RenderMode::Preview;
    base::Size target;             ///< Zielgröße der Vorschau; 0 = volle Auflösung der Quelle
    std::optional<image::Roi> roi; ///< Detail: sichtbarer Ausschnitt
    OutputSpec output;
    bool useCache = false;
    ports::Priority priority = ports::Priority::Interactive;
};

/// Anfrage an den PipelineExecutor (Architektur 9.4).
///
/// Ergänzung: path und film, weil SourceProvider und Compiler beides brauchen
/// (der Filmtyp der Rolle gilt, solange kein Eintrag autoneg.filmmode ihn ändert).
struct RenderRequest
{
    model::FrameId frame;
    std::filesystem::path path;
    std::shared_ptr<const model::EditList> list;
    model::FilmType film = model::FilmType::ColorNegative;
    RenderProfile profile;
    std::optional<model::StepId> stopAfter;
    image::CancelToken cancel;
};

} // namespace autoneg::pipeline
