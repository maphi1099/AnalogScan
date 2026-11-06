#pragma once

#include "application/command_bus.hpp"
#include "application/commands.hpp"
#include "application/contact_sheet_service.hpp"
#include "application/jobs.hpp"
#include "application/render_service.hpp"
#include "application/roll_service.hpp"
#include "application/services.hpp"
#include "application/thumbnail_service.hpp"
#include "ports/color.hpp"
#include "ports/repositories.hpp"
#include "ports/services.hpp"
#include "ui/qt_session_bridge.hpp"
#include "ui/ui_controller.hpp"

#include <QtCore/QByteArray>
#include <QtGui/QImage>
#include <QtWidgets/QMainWindow>

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QTableView;
class QToolButton;
class QTabWidget;
class QToolBar;

namespace autoneg::ui {

class AdjustPanel;
class ConversionPanel;
class FilmProfilePanel;
class RetouchPanel;
class CropRotateTool;
class EditListModel;
class FilmstripModel;
class FilmstripView;
class GradationPanel;
class HistogramWidget;
class ImageCanvas;
class ImportPage;
class OverviewView;
class RectPickerTool;
class RetouchTool;

/// Alles, was das Hauptfenster von der Anwendung braucht; die Kompositionswurzel erzeugt es
/// und reicht es als Referenzen herein (Konstruktor-Injektion, keine Singletons).
struct AppServices
{
    const pipeline::StepRegistry& registry;
    application::JobScheduler& jobs;
    application::CommandBus& bus;
    application::Session& session;
    application::SyncController& sync;
    application::EditClipboard& clipboard;
    application::RenderService& render;
    application::RollService& rolls;
    application::ThumbnailService& thumbnails;
    application::AnalysisService& analysis;
    application::ExportService& exports;
    application::ContactSheetService& contactSheet;
    ports::IColorEngine& color;
    ports::IPresetRepository& presets;
    ports::ISettingsStore& settings;
    QtSessionBridge& bridge;
    /// Kopfdaten eines Scans (Größe), aus dem SourceProvider; läuft in einem Job.
    std::function<base::Expected<model::SourceInfo>(const model::Frame&)> sourceInfo;
    /// Meldet das aktive Bild (z. B. damit Miniaturen dessen Quelle nicht freigeben).
    std::function<void(const model::FrameId&)> currentChanged;
    /// Bearbeitungsliste ↔ JSON für Bearbeitungsvorlagen (EditCodec der Datenhaltung).
    std::function<model::JsonValue(const model::EditList&)> encodeEdits;
    std::function<model::EditList(const model::JsonValue&)> decodeEdits;
    /// Protokoll (NF-REL-03); Meldungen der Oberfläche landen auch dort.
    ports::ILogger* logger = nullptr;
    /// Arbeitsspeicher des Rechners (0 = unbekannt), für die Einstellung Speicherbudget.
    std::uint64_t physicalMemory = 0;
};

/// Hauptfenster (Architektur 9.14, LF-UI-01): Filmstreifen, Arbeitsfläche, Werkzeuge,
/// Bearbeitungsliste, Histogramm. Nur im UI-Thread; rechnet keine Bilddaten.
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppServices s, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Öffnet einen Scan-Ordner als Rolle (LF-IMP-07); asynchron.
    void openFolder(const std::filesystem::path& folder);
    void selectFrame(const model::FrameId& f);
    /// Bearbeitungsvorlage auf die Auswahl anwenden (LF-BAT-07).
    bool applyEditPreset(const QString& name);

    // Zugriff für E2E-Tests (Qt Test)
    [[nodiscard]] FilmstripView* filmstrip() const { return filmstrip_; }
    [[nodiscard]] ImageCanvas* canvas() const { return canvas_; }
    [[nodiscard]] EditListModel* editListModel() const { return editModel_; }
    [[nodiscard]] QTableView* editListView() const { return editView_; }
    [[nodiscard]] HistogramWidget* histogram() const { return histogram_; }
    [[nodiscard]] GradationPanel* gradationPanel() const { return gradation_; }
    [[nodiscard]] ConversionPanel* conversionPanel() const { return conversion_; }
    [[nodiscard]] AdjustPanel* adjustPanel() const { return adjust_; }
    [[nodiscard]] ImportPage* importPage() const { return import_; }
    [[nodiscard]] OverviewView* overview() const { return overview_; }
    [[nodiscard]] QComboBox* rollFilmCombo() const { return rollFilm_; }
    [[nodiscard]] QTabBar* viewTabs() const { return viewTabs_; }

    /// Ansichten (K-M10-08): Import einer neuen Rolle, Leuchttisch (alle Bilder, Overview),
    /// Entwickeln (ein Bild, Image). None: alle Ansichten geschlossen.
    enum class View
    {
        Start,
        Import,
        Overview,
        Image,
        None
    };
    [[nodiscard]] View view() const { return view_; }
    void showView(View v);
    /// Ansicht schließen (Reiter am linken Rand); über das Menü Fenster wieder zu öffnen.
    void closeView(View v);
    [[nodiscard]] UiController& controller() { return *ui_; }
    [[nodiscard]] QAction* action(const QString& name) const;
    [[nodiscard]] bool rollOpen() const { return roll_ != nullptr; }
    [[nodiscard]] bool readOnly() const { return readOnly_; }
    [[nodiscard]] std::uint64_t previewsShown() const { return previewsShown_; }
    /// Fehlermeldungen der Sitzung (statt modaler Dialoge auch für Tests sichtbar).
    [[nodiscard]] const QStringList& messages() const { return messages_; }

Q_SIGNALS:
    void rollOpened();
    void previewShown(std::uint64_t seq);
    void jobDone(bool ok);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    void buildActions();
    void requestPreview(const model::EditList& l);
    void refreshCurrent();
    /// Filmtyp eines Bilds (Eintrag "Filmtyp" oder Rollen-Standard).
    [[nodiscard]] model::FilmType filmOf(const model::FrameId& f) const;
    void chooseFilm(const std::string& type);
    /// Eine Automatik des AnalysisService für ein Bild (Filmtyp, Stand des Bilds).
    using Automatic =
        std::function<application::JobHandle(const model::Frame&, model::FilmType, model::EditList)>;
    /// Automatik a des AnalysisService (z. B. &AnalysisService::autoBase) für die geöffnete Rolle.
    [[nodiscard]] Automatic automatic(application::JobHandle (application::AnalysisService::*a)(
        const model::Frame&, std::shared_ptr<const model::Roll>, model::FilmType, model::EditList));
    /// Automatik für die Bilder. button: Schalter der Konvertierung, der bis zum Ende ein
    /// drehendes Symbol zeigt.
    void detect(const std::vector<model::FrameId>& frames, const QString& what, const Automatic& run,
                const QString& button = {});
    /// Automatische Einträge der Schritte neu berechnen (Einfügen mit Neuberechnung).
    void redetect(const std::vector<model::FrameId>& frames, const std::set<model::StepId>& steps);
    void onRollOpened(application::OpenedRoll r);
    /// Tastenkürzel: Standard, überschrieben durch die Einstellungen ("shortcuts").
    void applyShortcuts();
    /// Vorheriges (-1) bzw. nächstes (+1) Bild der Rolle.
    void stepImage(int delta);
    /// Filmtyp der Rolle festlegen (Import, Übersicht): speichern, Miniaturen und Vorschau neu.
    void setRollFilm(model::FilmType film);
    void closeRoll();
    void refreshAllThumbnails();
    void onPreview(const PreviewFrame& p);
    void requestThumbnail(const model::Frame& f);
    void setTool(int which);
    void report(const QString& msg);
    void trackJob(application::JobHandle h, const QString& what);
    void doExport();
    /// Export ohne Dialog mit den aktuellen Exporteinstellungen (Leuchttisch).
    void exportFrames(const std::vector<model::FrameId>& frames);
    void doContactSheet();
    void contactSheetPdf();
    [[nodiscard]] std::vector<application::ContactSheetService::Item> sheetItems() const;
    /// Kopierte Bearbeitungsoptionen mit einem Klick auf Bilder übernehmen. Automatische
    /// Optionen (Bildfeld, Basis, Tonwerte) werden je Bild neu berechnet statt kopiert.
    /// batch: vom Leuchttisch (dort rückgängig zu machen).
    void applyCopiedTo(std::vector<model::FrameId> frames, bool batch);
    /// Markierte Bearbeitungsoptionen (oder alle) des aktiven Bilds kopieren.
    void copyOptions();
    /// Autokonvertierung des aktiven Bilds (alle Automatiken, nicht abgewählt).
    [[nodiscard]] bool autoConverted(const model::FrameId& f) const;
    void setAutoConvert(bool on);
    void refreshFilmProfiles();
    /// Film-Farbprofil wählen (Schlüssel wie FilmProfilePanel); apply: Tonwerte der markierten Bilder neu setzen.
    void chooseFilmProfile(const QString& key, bool apply);
    void saveFilmProfile();
    /// Formen der Staubretusche als Eintrag setzen (leer: Eintrag entfernen).
    void commitShapes(const model::RetouchShapes& shapes);
    /// Debug-Bau: Bedienereignis ins Sitzungsprotokoll.
    void trace(const QString& event) const;
    /// Werkzeug und Reiter Staubretusche auf den Stand der Liste bringen.
    void syncRetouchTool();
    /// Schritte der markierten Bilder entfernen (Schalter abgewählt).
    void removeSteps(std::initializer_list<std::string_view> steps);
    /// Alle Fenster wieder wie beim Start anordnen (auch losgelöste).
    void arrangeWindows();
    void doFrameOptions();
    /// Hinweis (Format, Lage) aus den Einstellungen an die Erkennung geben.
    void applyFrameHint();
    /// Weißabgleich so setzen, dass die Stelle norm (0–1 im gezeigten Bild) neutral wird.
    void pickWhite(QPointF norm);
    void onFrameReport(const model::FrameId& f, const application::AnalysisService::FrameReport& r);
    /// Beim Import abgewählte Scans aus der Rolle nehmen.
    void excludeFrames(const std::vector<model::FrameId>& frames);
    void setStars(int stars);
    void updateRatingButtons();
    void updateExportPanel();
    void doPaste();
    void doSettings();
    void rate(model::Rating r);
    [[nodiscard]] const model::Frame* frame(const model::FrameId& f) const;
    [[nodiscard]] std::vector<model::FrameId> targets() const;

    AppServices s_;
    std::unique_ptr<UiController> ui_;
    std::shared_ptr<const ports::IColorTransform> display_;

    FilmstripModel* stripModel_ = nullptr;
    FilmstripView* filmstrip_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
    EditListModel* editModel_ = nullptr;
    QTableView* editView_ = nullptr;
    HistogramWidget* histogram_ = nullptr;
    QTabWidget* tools_ = nullptr;
    ConversionPanel* conversion_ = nullptr;
    GradationPanel* gradation_ = nullptr;
    AdjustPanel* adjust_ = nullptr;
    FilmProfilePanel* filmProfile_ = nullptr;
    RetouchPanel* retouch_ = nullptr;
    QString filmProfileKey_; ///< gewähltes Film-Farbprofil (FilmProfilePanel::kNeutral usw. oder Name)
    QStackedWidget* stack_ = nullptr;
    QWidget* startPage_ = nullptr;
    ImportPage* import_ = nullptr;
    QWidget* overviewPage_ = nullptr;
    OverviewView* overview_ = nullptr;
    QLabel* overviewTitle_ = nullptr;
    QComboBox* rollFilm_ = nullptr;
    QTabBar* viewTabs_ = nullptr;
    QWidget* emptyPage_ = nullptr;
    QWidget* imageTools_ = nullptr;
    QWidget* retouchTab_ = nullptr;
    QLabel* exportFolder_ = nullptr;
    QComboBox* exportFormat_ = nullptr;
    QLabel* selectionInfo_ = nullptr;
    std::vector<QToolButton*> starButtons_;
    QToolButton* rejectButton_ = nullptr;
    std::vector<QDockWidget*> editorDocks_; ///< nur in der Bildansicht sichtbar
    View view_ = View::Start;
    QProgressBar* progress_ = nullptr;
    QWidget* zoomBar_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QSpinBox* zoomPercent_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* filter_ = nullptr;
    std::map<QString, QAction*> actions_;

    std::unique_ptr<CropRotateTool> cropTool_;
    std::unique_ptr<RectPickerTool> baseTool_;
    std::unique_ptr<RetouchTool> retouchTool_;

    std::shared_ptr<const model::Roll> roll_;
    std::vector<model::Frame> frames_;
    std::map<model::FrameId, model::SourceInfo> sources_;
    bool readOnly_ = false;
    bool compare_ = false;
    bool showArea_ = false; ///< ganzes Bild mit Messbereich der Basis
    std::unique_ptr<RectPickerTool> areaTool_;
    std::set<model::FrameId> manualConversion_; ///< Autokonvertierung abgewählt
    std::optional<model::FrameId> copiedFrom_;
    std::optional<model::FrameId> showFrameAfterDetect_;
    bool pickWhite_ = false;                         ///< Weißabgleich-Pipette wartet auf einen Klick ins Bild
    QImage shown_;                                   ///< zuletzt gezeigte Vorschau (für die Pipette)
    std::optional<QRectF> detail_; ///< Detailansicht: angeforderter Ausschnitt (normiert)
    std::map<model::FrameId, QString> frameReports_; ///< Bewertung der Bildfelderkennung
    QByteArray defaultLayout_;
    std::uint64_t lastSeq_ = 0;
    std::uint64_t previewsShown_ = 0;
    QStringList messages_;
    application::ExportSettings exportSettings_;
    layout::ContactSheetSpec sheetSpec_;
    std::string sheetFormat_ = "Kleinbild 24×36";
    std::vector<application::JobHandle> running_;
    std::map<QString, std::vector<application::JobHandle>> busy_; ///< Schalter → laufende Automatiken
};

} // namespace autoneg::ui
