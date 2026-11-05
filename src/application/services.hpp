#pragma once

#include "analysis/base_meter.hpp"
#include "analysis/frame_detector.hpp"
#include "application/command_bus.hpp"
#include "application/jobs.hpp"
#include "application/render_service.hpp"
#include "ports/color.hpp"
#include "ports/document_renderer.hpp"
#include "ports/film_profile_source.hpp"
#include "ports/image_io.hpp"
#include "ports/inference.hpp"
#include "ports/repositories.hpp"

#include <filesystem>
#include <functional>
#include <optional>
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
    /// Teile der Automatik: Bildfeld (Zuschnitt, Feindrehung), Filmbasis, Tonwertgrenzen.
    enum Part : unsigned
    {
        Frame = 1U,
        Base = 2U,
        Levels = 4U,
        All = 7U
    };

    AnalysisService(JobScheduler& jobs, CommandBus& bus, UiDispatcher ui, AnalysisFunctions fns)
        : jobs_(jobs)
        , bus_(bus)
        , ui_([alive = std::weak_ptr<int>(alive_), ui = std::move(ui)](std::function<void()> f) {
            // Ergebnisse, die erst nach dem Abbau ankommen (Fenster geschlossen), verwerfen
            ui([alive, f = std::move(f)] {
                if (!alive.expired()) {
                    f();
                }
            });
        })
        , fns_(std::move(fns))
    {}

    /// Automatik eines Bildes: nur die gewünschten Teile werden eingetragen (Auto-Bildfeld
    /// schreibt nur den Zuschnitt). current ist der Stand des Bilds; Tonwerte nutzen dessen
    /// Zuschnitt und Basis, sofern vorhanden. film ist der Filmtyp des Bilds
    /// (steps::FilmModeStep::effective); ohne festgelegten Filmtyp scheitert der Job (K-M10-08).
    JobHandle detectFrame(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                          model::FilmType film, unsigned parts = All, model::EditList current = {});
    /// Basis im vom Anwender gezogenen Rechteck messen (LF-BAS-04); Eintrag mit Herkunft
    /// manuell und source = "manual".
    JobHandle measureBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                          model::NormRect rect);

private:
    JobScheduler& jobs_;
    CommandBus& bus_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0); ///< vor ui_: lebt so lange wie der Dienst
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
/// film: Filmtyp des Bilds (Eintrag "Filmtyp" oder Rollen-Standard); ohne Angabe der Rollen-Standard.
[[nodiscard]] std::string exportName(const std::string& pattern, const model::Roll& roll,
                                     const model::Frame& frame,
                                     std::optional<model::FilmType> film = std::nullopt);

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
    /// Nach jedem Bild aufgerufen, damit die dekodierte Quelle freigegeben werden kann
    /// (SourceProvider::release, außer das Bild ist in Bearbeitung; NF-PERF-07).
    using Release = std::function<void(const model::FrameId&)>;
    /// Geschätzter Spitzenbedarf je Quellpixel beim Export (Quelle, Ergebnis, Farbumrechnung,
    /// Ausgabepuffer); bestimmt mit dem Speicherbudget, wie viele Bilder gleichzeitig laufen.
    static constexpr std::size_t exportBytesPerPixel = 40;

    ExportService(JobScheduler& jobs, IRenderer& renderer, ports::IColorEngine& color,
                  ports::IImageWriter& writer, Release release = {})
        : jobs_(jobs)
        , renderer_(renderer)
        , color_(color)
        , writer_(writer)
        , release_(std::move(release))
    {}

    /// Zieldatei; Fehler Conflict, wenn sie existiert und nicht überschrieben werden darf.
    [[nodiscard]] static base::Expected<std::filesystem::path>
    target(const ExportSettings& s, const model::Roll& roll, const model::Frame& frame,
           std::optional<model::FilmType> film = std::nullopt);

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
    Release release_;
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

/// Ohne Qt (Bau ohne AUTONEG_WITH_GUI): kein PDF und kein Druck des Kontaktabzugs.
class NoDocumentRenderer final : public ports::IDocumentRenderer
{
public:
    [[nodiscard]] base::Expected<void> writePdf(std::span<const ports::RenderedPage> /*pages*/,
                                                const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported,
                               "Kontaktabzug als PDF braucht den Bau mit Oberfläche");
    }
    [[nodiscard]] base::Expected<void> print(std::span<const ports::RenderedPage> /*pages*/,
                                             const ports::PrintSettings& /*s*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "Drucken braucht den Bau mit Oberfläche");
    }
};

/// Platzhalter der Filmprofile für Version 1.0 (NF-ARC-15): keine Profile.
class NoFilmProfiles final : public ports::IFilmProfileSource
{
public:
    [[nodiscard]] std::vector<ports::FilmProfileInfo> list() override { return {}; }
    [[nodiscard]] base::Expected<model::JsonValue> load(const std::string& id) override;
};

} // namespace autoneg::application
