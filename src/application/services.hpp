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
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
    std::function<base::Expected<analysis::FrameDetection>(const image::ImageBuffer&, model::FilmType,
                                                           const analysis::FrameHint&)>
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

/// Zwischenspeicher der Automatiken je Bild: verkleinertes Analysebild und letzte
/// Bildfelderkennung (für Filmtyp und Hinweis). Auto-Bildfeld, Auto-Basis und Konvertieren
/// dekodieren und erkennen so nur einmal. Hält die zuletzt benutzten Bilder; threadsicher.
class AnalysisCache
{
public:
    explicit AnalysisCache(std::size_t capacity = 6)
        : capacity_(capacity)
    {}

    [[nodiscard]] std::shared_ptr<const image::ImageBuffer> image(const model::FrameId& f);
    void putImage(const model::FrameId& f, std::shared_ptr<const image::ImageBuffer> img);
    [[nodiscard]] std::optional<analysis::FrameDetection>
    detection(const model::FrameId& f, model::FilmType film, const analysis::FrameHint& hint);
    void putDetection(const model::FrameId& f, model::FilmType film, const analysis::FrameHint& hint,
                      analysis::FrameDetection d);

private:
    struct Entry
    {
        model::FrameId frame;
        std::shared_ptr<const image::ImageBuffer> image;
        std::optional<analysis::FrameDetection> detection;
        model::FilmType film = model::FilmType::Unconverted;
        analysis::FrameHint hint;
    };
    Entry& entry(const model::FrameId& f); ///< nach vorn holen bzw. anlegen; mtx_ gehalten

    std::size_t capacity_;
    std::mutex mtx_;
    std::list<Entry> entries_; ///< zuletzt benutzt vorn
};

/// Automatiken (Architektur 9.13, LF-GEO-01, LF-BAS-01): laufen als Jobs im Hintergrund und
/// schreiben ihre Ergebnisse als Einträge mit Herkunft Automatic über den CommandBus (im
/// UI-Thread). Einträge, die der Anwender gesetzt, kopiert oder aus einer Vorlage hat, werden
/// nicht überschrieben. Jede Automatik trägt nur ihren Teil ein (K-M10-13).
///
/// film ist der Filmtyp des Bilds (steps::FilmModeStep::effective); ohne festgelegten Filmtyp
/// scheitert der Job (K-M10-08). current ist der Stand des Bilds: Tonwerte nutzen dessen
/// Zuschnitt und Basis, sofern vorhanden.
class AnalysisService
{
public:
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

    /// Auto-Bildfeld: Zuschnitt und Feindrehung.
    JobHandle autoFrame(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                        model::EditList current = {});
    /// Auto-Basis: Filmbasis im Filmrand, ohne Filmrand statistisch geschätzt.
    JobHandle autoBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                       model::EditList current = {});
    /// Tonwertgrenzen aus Bild und Basis.
    JobHandle autoLevels(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                         model::EditList current = {});
    /// Schalter Konvertieren: Tonwerte und Umkehrung mit der vorhandenen Basis.
    JobHandle convert(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                      model::EditList current = {});
    /// Autokonvertierung: Bildfeld, Basis, Tonwerte und Umkehrung.
    JobHandle autoConvert(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                          model::FilmType film, model::EditList current = {});
    /// Automatische Einträge der Schritte steps neu berechnen (Einfügen mit Neuberechnung):
    /// Zuschnitt/Feindrehung, Filmbasis, Tonwerte. Mit Basis und Tonwerten nutzen die Tonwerte
    /// die neu gemessene Basis.
    JobHandle redetect(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                       model::EditList current, const std::set<model::StepId>& steps);

    /// Hinweis für die Bildfelderkennung (Format, Lage); gilt für folgende Aufträge.
    void setFrameHint(const analysis::FrameHint& h) { hint_ = h; }

    /// Film-Farbprofil: bestimmt die Tonwertgrenzen (autoneg.densityrange) beim Konvertieren.
    /// Standard „Neutral“ gleicht Farbstiche automatisch je Kanal aus, „Unbearbeitet“ setzt nichts;
    /// alle gesetzten Werte sind als Regler sichtbar und änderbar.
    struct FilmProfile
    {
        enum class Kind
        {
            Unprocessed, ///< „Unbearbeitet“: nur umkehren, keine Streckung (Neutralwerte des Schritts)
            Neutral,     ///< „Neutral“ (Standard): Grenzen automatisch aus dem Bild je Kanal
            Custom       ///< eigenes Profil: Automatik plus Abweichung je Kanal und Gamma
        };
        Kind kind = Kind::Neutral;
        std::string name;                ///< Anzeigename (bei Custom der gespeicherte Name)
        base::Vec3 blackOffset{0, 0, 0}; ///< dMin je Kanal minus Automatik (autoMin)
        base::Vec3 whiteOffset{0, 0, 0}; ///< dMax je Kanal minus Automatik (autoMax)
        base::Vec3 gamma{1, 1, 1};

        /// Tonwertgrenzen für ein Bild mit den automatisch gemessenen Grenzen je Kanal.
        [[nodiscard]] model::ParamSet params(const base::Vec3& black, const base::Vec3& white) const;
        /// Profil aus einem gespeicherten Eintrag autoneg.densityrange (Name aus dem Eintrag).
        [[nodiscard]] static FilmProfile fromEntry(const model::EditEntry& e, std::string name);
    };
    void setFilmProfile(FilmProfile p) { profile_ = std::move(p); }
    [[nodiscard]] const FilmProfile& filmProfile() const { return profile_; }
    /// Meldung je Bild nach der Erkennung (im UI-Thread).
    struct FrameReport
    {
        double quality = 0.0;           ///< Bewertung der Bildfelderkennung 0…1
        std::vector<std::string> notes; ///< Gründe der Bewertung (technisch, nur Debug-Anzeige)
        std::vector<std::string> hints; ///< Hinweise für den Anwender, z. B. "Basis geschätzt"
    };
    using Report = std::function<void(const model::FrameId&, FrameReport)>;
    void setReport(Report r) { report_ = std::move(r); }
    /// Basis im vom Anwender gezogenen Rechteck messen (LF-BAS-04); Eintrag mit Herkunft
    /// manuell und source = "manual".
    JobHandle measureBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                          model::NormRect rect);

private:
    enum Part : unsigned
    {
        Frame = 1U,
        Base = 2U,
        Levels = 4U,
        Convert = 8U, ///< Konvertieren einschalten (autoneg.convert)
        All = 15U
    };
    JobHandle run(const model::Frame& f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                  unsigned parts, model::EditList current);

    JobScheduler& jobs_;
    CommandBus& bus_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0); ///< vor ui_: lebt so lange wie der Dienst
    UiDispatcher ui_;
    AnalysisFunctions fns_;
    std::shared_ptr<AnalysisCache> cache_ = std::make_shared<AnalysisCache>(); ///< teilen sich die Jobs
    analysis::FrameHint hint_;
    FilmProfile profile_;
    Report report_;
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
