#pragma once

#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/commands.hpp"
#include "autoneg/application/contact_sheet_service.hpp"
#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/application/roll_service.hpp"
#include "autoneg/application/services.hpp"
#include "autoneg/application/thumbnail_service.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/repositories.hpp"
#include "autoneg/ports/services.hpp"
#include "autoneg/ui/qt_session_bridge.hpp"
#include "autoneg/ui/ui_controller.hpp"

#include <QtWidgets/QMainWindow>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTabWidget;
class QToolBar;

namespace autoneg::ui {

class AdjustPanel;
class ConversionPanel;
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

    /// Ansichten (K-M10-08): Import einer neuen Rolle, Übersicht (alle Bilder), Bildansicht.
    enum class View
    {
        Start,
        Import,
        Overview,
        Image
    };
    [[nodiscard]] View view() const { return view_; }
    void showView(View v);
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
    void detect(const std::vector<model::FrameId>& frames, const QString& what);
    void onRollOpened(application::OpenedRoll r);
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
    void doContactSheet();
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
    QStackedWidget* stack_ = nullptr;
    QWidget* startPage_ = nullptr;
    ImportPage* import_ = nullptr;
    QWidget* overviewPage_ = nullptr;
    OverviewView* overview_ = nullptr;
    QLabel* overviewTitle_ = nullptr;
    QComboBox* rollFilm_ = nullptr;
    QToolBar* viewBar_ = nullptr;
    std::vector<QDockWidget*> editorDocks_; ///< nur in der Bildansicht sichtbar
    View view_ = View::Start;
    QProgressBar* progress_ = nullptr;
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
    bool clipping_ = false;
    std::uint64_t lastSeq_ = 0;
    std::uint64_t previewsShown_ = 0;
    QStringList messages_;
    application::ExportSettings exportSettings_;
    layout::ContactSheetSpec sheetSpec_;
    std::string sheetFormat_ = "Kleinbild 24×36";
    std::vector<application::JobHandle> running_;
};

} // namespace autoneg::ui
