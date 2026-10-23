#pragma once

#include "autoneg/analysis/base_meter.hpp"
#include "autoneg/analysis/frame_detector.hpp"
#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/film_profile_source.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/inference.hpp"
#include "autoneg/ports/repositories.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace autoneg::application {

/// Schrittkatalog für die Datenhaltung aus der StepRegistry (K-M6-01).
[[nodiscard]] ports::StepCatalog stepCatalog(const pipeline::StepRegistry& registry);

/// Übergibt eine Aufgabe an den UI-Thread (in der Oberfläche über eine Qt-Warteschlange).
using UiDispatcher = std::function<void(std::function<void()>)>;

/// Analysefunktionen als Naht für Tests (Ergänzung K-M6-08); Standard: FrameDetector/BaseMeter.
struct AnalysisFunctions
{
    std::function<base::Expected<image::ImageBuffer>(const model::Frame&)> image; ///< Analysebild
    std::function<base::Expected<analysis::FrameDetection>(const image::ImageBuffer&, model::FilmType)>
        detect;
    std::function<base::Expected<analysis::BaseMeasurement>(const image::ImageBuffer&,
                                                            const analysis::RegionMap&)>
        measureBase;
    /// Basis im Rechteck (normiert auf das Analysebild, LF-BAS-04).
    std::function<base::Expected<analysis::BaseMeasurement>(const image::ImageBuffer&,
                                                            const model::NormRect&)>
        measureRect;

    /// Echte Analyse: Bildfeld über IVision, Basis im Filmrand.
    [[nodiscard]] static AnalysisFunctions
    standard(ports::IVision& vision,
             std::function<base::Expected<image::ImageBuffer>(const model::Frame&)> image);
};

/// Automatiken (Architektur 9.13, LF-GEO-01, LF-BAS-01): laufen als Jobs im Hintergrund und
/// schreiben ihre Ergebnisse als Einträge mit Herkunft Automatic über den CommandBus (im
/// UI-Thread). Einträge, die der Anwender gesetzt, kopiert oder aus einer Vorlage hat, werden
/// nicht überschrieben.
class AnalysisService
{
public:
    AnalysisService(JobScheduler& jobs, CommandBus& bus, UiDispatcher ui, AnalysisFunctions fns)
        : jobs_(jobs)
        , bus_(bus)
        , ui_(std::move(ui))
        , fns_(std::move(fns))
    {}

    /// Bildfeld, Schräglage und (bei Negativen) Basis eines Bildes.
    JobHandle detectFrame(const model::Frame& f, std::shared_ptr<const model::Roll> roll);
    /// Basis im vom Anwender gezogenen Rechteck messen (LF-BAS-04); Eintrag mit Herkunft
    /// manuell und source = "manual".
    JobHandle measureBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                          model::NormRect rect);

private:
    JobScheduler& jobs_;
    CommandBus& bus_;
    UiDispatcher ui_;
    AnalysisFunctions fns_;
};

/// Setzt Ergebnisse einer Automatik: nur, wo kein Eintrag des Anwenders steht.
class SetAutomaticEntriesCommand final : public ICommand
{
public:
    explicit SetAutomaticEntriesCommand(std::vector<model::EditEntry> entries)
        : entries_(std::move(entries))
    {}
    [[nodiscard]] std::string label() const override { return "Automatik"; }
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    std::vector<model::EditEntry> entries_;
};

/// Dateinamen beim Export (LF-EXP-07): Platzhalter {roll}, {frame} (dreistellig), {name}
/// (Scan ohne Endung), {film}; Zeichen, die Dateisysteme nicht erlauben, werden ersetzt.
[[nodiscard]] std::string exportName(const std::string& pattern, const model::Roll& roll,
                                     const model::Frame& frame);

enum class OverwritePolicy
{
    Never,    ///< vorhandene Datei: Fehler Conflict (Oberfläche fragt nach)
    Overwrite ///< nach Bestätigung des Anwenders
};

struct ExportSettings
{
    std::filesystem::path folder;
    std::string pattern = "{roll}_{frame}_{name}";
    ports::WriteOptions write;
    ports::ProfileId profile = ports::ProfileId::Srgb;
    OverwritePolicy overwrite = OverwritePolicy::Never;
    int longEdge = 0;       ///< Pixel der langen Kante; 0 = Originalgröße (LF-EXP-06)
    bool grayscale = false; ///< einkanalig mit Graustufenprofil (LF-EXP-09)
};

/// Export als Stapel-Job (Architektur 9.13, LF-EXP-01…08): Rendern in voller Auflösung,
/// Farbraum des Ziels, Schreiben über IImageWriter (atomar). Vorhandene Dateien werden ohne
/// Bestätigung nicht überschrieben.
class ExportService
{
public:
    ExportService(JobScheduler& jobs, IRenderer& renderer, ports::IColorEngine& color,
                  ports::IImageWriter& writer)
        : jobs_(jobs)
        , renderer_(renderer)
        , color_(color)
        , writer_(writer)
    {}

    /// Zieldatei; Fehler Conflict, wenn sie existiert und nicht überschrieben werden darf.
    [[nodiscard]] static base::Expected<std::filesystem::path>
    target(const ExportSettings& s, const model::Roll& roll, const model::Frame& frame);

    struct Item
    {
        model::Frame frame;
        model::EditList edits;
    };
    JobHandle exportFrames(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                           ExportSettings s);

private:
    JobScheduler& jobs_;
    IRenderer& renderer_;
    ports::IColorEngine& color_;
    ports::IImageWriter& writer_;
};

/// Platzhalter der KI-Laufzeit für Version 1.0 (NF-ARC-07, D-04): nicht verfügbar.
class NoInferenceRuntime final : public ports::IInferenceRuntime
{
public:
    [[nodiscard]] std::string id() const override { return "none"; }
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] base::Expected<std::unique_ptr<ports::IInferenceModel>>
    load(const std::filesystem::path& model) override;
};

/// Platzhalter der Filmprofile für Version 1.0 (NF-ARC-15): keine Profile.
class NoFilmProfiles final : public ports::IFilmProfileSource
{
public:
    [[nodiscard]] std::vector<ports::FilmProfileInfo> list() override { return {}; }
    [[nodiscard]] base::Expected<model::JsonValue> load(const std::string& id) override;
};

} // namespace autoneg::application
