#include "ui/main_window.hpp"

#include "application/presets.hpp"
#include "steps/other_steps.hpp"
#include "steps/pointwise_steps.hpp"
#include "steps/step_ids.hpp"
#include "ui/canvas_tools.hpp"
#include "ui/dialogs.hpp"
#include "ui/edit_list_model.hpp"
#include "ui/image_canvas.hpp"
#include "ui/import_page.hpp"
#include "ui/panels.hpp"
#include "ui/translations.hpp"
#include "ui/views.hpp"

#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtGui/QCloseEvent>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace autoneg::ui {

namespace id = steps::id;
namespace key = steps::key;

namespace {

constexpr int thumbnailSize = 256;

/// Mehrere Einträge als ein Befehl (eine Rückgängig-Stufe), z. B. Zuschnitt und Drehung.
class SetEntriesCommand final : public application::ICommand
{
public:
    explicit SetEntriesCommand(std::vector<model::EditEntry> entries)
        : entries_(std::move(entries))
    {}
    [[nodiscard]] std::string label() const override { return "Zuschnitt"; }
    [[nodiscard]] model::EditList apply(const model::EditList& l,
                                        const application::CommandContext& c) const override
    {
        model::EditList out = l;
        for (const model::EditEntry& e : entries_) {
            out = application::SetEntryCommand(e).apply(out, c);
        }
        return out;
    }

private:
    std::vector<model::EditEntry> entries_;
};

/// Kleine Aufgabe für den JobScheduler aus einer Funktion.
class FunctionJob final : public application::Job
{
public:
    FunctionJob(std::string name, std::function<base::Expected<void>()> f)
        : name_(std::move(name))
        , f_(std::move(f))
    {}
    std::string name() const override { return name_; }
    base::Expected<void> run(application::JobContext& /*c*/) override { return f_(); }

private:
    std::string name_;
    std::function<base::Expected<void>()> f_;
};

model::EditEntry entryWith(const model::EditList& l, std::string_view step, std::string_view name,
                           model::ParamValue v)
{
    model::EditEntry e;
    if (const model::EditEntry* old = l.get(model::StepId(std::string(step)))) {
        e = *old;
    } else {
        e.step = model::StepId(std::string(step));
    }
    e.params = e.params.with(name, std::move(v));
    e.origin = model::Origin::Manual;
    e.enabled = true;
    e.copiedFrom.reset();
    return e;
}

QString qs(const std::filesystem::path& p)
{
    return QString::fromStdU16String(p.u16string());
}

} // namespace

MainWindow::MainWindow(AppServices s, QWidget* parent)
    : QMainWindow(parent)
    , s_(std::move(s))
    , cropTool_(std::make_unique<CropRotateTool>())
    , baseTool_(std::make_unique<RectPickerTool>())
    , retouchTool_(std::make_unique<RetouchTool>())
    , areaTool_(std::make_unique<RectPickerTool>())
{
    areaTool_->setReadOnly(true);
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("AutoNeg"));
    setAcceptDrops(true);
    ui_ = std::make_unique<UiController>(s_.bus, s_.session, s_.sync, s_.registry,
                                         [this](const model::EditList& l) { requestPreview(l); });
    // Monitorprofil (LF-UI-06), sonst sRGB
    auto working = s_.color.profile(ports::ProfileId::WorkingRec2020Linear);
    auto monitor = s_.color.display(0);
    if (!monitor) {
        monitor = s_.color.profile(ports::ProfileId::Srgb);
    }
    if (working && monitor) {
        if (auto xf = s_.color.transform(*working, *monitor, ports::Intent::Perceptual)) {
            display_ = *xf;
        }
    }
    // dauerhaft eingeschaltete Schalter hellblau hinterlegt
    setStyleSheet(QStringLiteral("QPushButton:checked, QToolButton:checked { background-color: #9fd3ff; "
                                 "color: #10202c; border: 1px solid #5aa9e6; border-radius: 3px; }"));
    buildUi();
    buildActions();
    defaultLayout_ = saveState();
    applyFrameHint();
    // QPointer: Meldungen können nach dem Schließen des Fensters ankommen
    s_.analysis.setReport([self = QPointer<MainWindow>(this)](const model::FrameId& f,
                                                              application::AnalysisService::FrameReport r) {
        if (self) {
            self->onFrameReport(f, r);
        }
    });
    s_.bus.subscribe(&s_.bridge);

    connect(&s_.bridge, &QtSessionBridge::previewReady, this, &MainWindow::onPreview);
    connect(&s_.bridge, &QtSessionBridge::thumbnailReady, this,
            [this](const model::FrameId& f, const QImage& img) { stripModel_->setThumbnail(f, img); });
    connect(&s_.bridge, &QtSessionBridge::editChanged, this, [this](const model::FrameId& f, qint64 /*rev*/) {
        if (f == s_.session.current) {
            refreshCurrent();
        }
        if (const model::Frame* fr = frame(f)) {
            requestThumbnail(*fr);
        }
    });
    connect(canvas_, &ImageCanvas::viewChanged, this, [this] { requestPreview(ui_->currentList()); });
    connect(canvas_, &ImageCanvas::picked, this, [this](QPointF norm) {
        if (pickWhite_) {
            pickWhite_ = false;
            pickWhite(norm);
        }
    });
    connect(adjust_, &AdjustPanel::whitePickerRequested, this, [this] {
        pickWhite_ = true;
        report(tr("Auf eine Stelle klicken, die neutral grau oder weiß sein soll."));
    });

    cropTool_->setCommit([this](const model::NormRect& r, double angle) {
        const model::EditList cur = ui_->currentList();
        ui_->apply(std::make_unique<SetEntriesCommand>(std::vector<model::EditEntry>{
            entryWith(cur, id::crop, key::rect, r), [&] {
                model::EditEntry e = entryWith(cur, id::rotation, key::angle, angle);
                e.params = e.params.with(key::autoFill, false); // der Rahmen bestimmt den Ausschnitt
                return e;
            }()}));
    });
    retouchTool_->setSpot([this](const model::Spot& spot) {
        const model::EditList cur = ui_->currentList();
        model::SpotList spots;
        if (const model::EditEntry* e = cur.get(model::StepId(std::string(id::retouch)))) {
            spots = e->params.get<model::SpotList>(key::spots).value_or(model::SpotList{});
        }
        spots.push_back(spot);
        ui_->apply(
            std::make_unique<application::SetEntryCommand>(entryWith(cur, id::retouch, key::spots, spots)));
    });
    baseTool_->setPicked([this](const model::NormRect& r) {
        // Messrechteck im Filmrand (LF-BAS-04); die Fläche zeigt das ungeschnittene Bild
        if (const model::Frame* fr = frame(s_.session.current)) {
            trackJob(s_.analysis.measureBase(*fr, roll_, r), tr("Basis messen"));
        }
        conversion_->setPickActive(false);
        setTool(0); // zurück zum zugeschnittenen Bild
    });
}

void MainWindow::onFrameReport(const model::FrameId& f, const application::AnalysisService::FrameReport& r)
{
    QString text = tr("Erkennung: %1 %").arg(static_cast<int>(std::lround(r.quality * 100.0)));
    text += r.quality >= 0.8 ? tr(" – gut") : r.quality >= 0.5 ? tr(" – prüfen") : tr(" – unsicher");
    for (const std::string& n : r.notes) {
        text += QStringLiteral("\n• ") + QString::fromStdString(n);
    }
    frameReports_[f] = text;
    if (f != s_.session.current) {
        return;
    }
    conversion_->setQuality(text);
    for (const std::string& h : r.hints) {
        report(translated(h));
    }
    if (showFrameAfterDetect_ == f) {
        showFrameAfterDetect_.reset();
        setTool(1); // erkannten Rahmen zeigen; der Nutzer kann ihn ziehen
    }
}

MainWindow::~MainWindow()
{
    // Beim Abbau melden Filmstreifen und Reiter noch Änderungen; dann gibt es die Panels nicht mehr
    disconnect(filmstrip_, nullptr, this, nullptr);
    disconnect(viewTabs_, nullptr, this, nullptr);
    for (const application::JobHandle& h : running_) {
        h.cancel();
    }
    if (roll_) {
        s_.rolls.close(roll_->folder);
    }
}

void MainWindow::buildUi()
{
    // Seiten der Mitte: Start, Import, Leuchttisch, Entwickeln (K-M10-08); links die Reiter
    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("views"));
    startPage_ = new QLabel(tr("Ordner mit Scans öffnen (Strg+O) oder hierher ziehen."), stack_);
    static_cast<QLabel*>(startPage_)->setAlignment(Qt::AlignCenter);
    startPage_->setObjectName(QStringLiteral("startPage"));
    import_ = new ImportPage(stack_);
    overviewPage_ = new QWidget(stack_);
    overviewPage_->setObjectName(QStringLiteral("overviewPage"));
    canvas_ = new ImageCanvas(stack_);
    canvas_->setObjectName(QStringLiteral("canvas"));
    stack_->addWidget(startPage_);
    stack_->addWidget(import_);
    stack_->addWidget(overviewPage_);
    stack_->addWidget(canvas_);
    emptyPage_ = new QLabel(
        tr("Keine Ansicht geöffnet.\nÜber das Menü Fenster: Leuchttisch (G) oder Entwickeln (E)."), stack_);
    static_cast<QLabel*>(emptyPage_)->setAlignment(Qt::AlignCenter);
    stack_->addWidget(emptyPage_);
    viewTabs_ = new QTabBar(this);
    viewTabs_->setObjectName(QStringLiteral("viewTabs"));
    viewTabs_->setShape(QTabBar::RoundedWest); // senkrechte Namen
    viewTabs_->setTabsClosable(true);
    viewTabs_->setExpanding(true); // die Reiter teilen sich den ganzen Rand
    viewTabs_->setUsesScrollButtons(false);
    viewTabs_->setDrawBase(false);
    viewTabs_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    viewTabs_->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 10px 4px; margin: 2px 0; border: 1px solid palette(mid);"
        " border-radius: 4px; background: palette(window); color: palette(text); font-weight: bold; }"
        "QTabBar::tab:selected { background: palette(highlight); color: palette(highlighted-text); }"
        "QTabBar::tab:hover:!selected { background: palette(midlight); }"));
    auto* center = new QWidget(this);
    auto* centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    centerLayout->addWidget(viewTabs_);
    centerLayout->addWidget(stack_, 1);
    setCentralWidget(center);
    connect(viewTabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (i >= 0) {
            showView(static_cast<View>(viewTabs_->tabData(i).toInt()));
        }
    });
    connect(viewTabs_, &QTabBar::tabCloseRequested, this,
            [this](int i) { closeView(static_cast<View>(viewTabs_->tabData(i).toInt())); });

    stripModel_ = new FilmstripModel(this);
    filmstrip_ = new FilmstripView(this);
    filmstrip_->setStripModel(stripModel_);
    auto* stripDock = new QDockWidget(tr("Filmstreifen"), this);
    stripDock->setObjectName(QStringLiteral("filmstripDock"));
    auto* stripBox = new QWidget(stripDock);
    auto* stripLayout = new QVBoxLayout(stripBox);
    stripLayout->setContentsMargins(2, 2, 2, 2);
    filter_ = new QComboBox(stripBox);
    filter_->setObjectName(QStringLiteral("ratingFilter"));
    filter_->addItem(tr("Alle Bilder"));
    filter_->addItem(tr("Behalten"));
    filter_->addItem(tr("Verworfen"));
    filter_->addItem(tr("Unbewertet"));
    filter_->setToolTip(tr("Filmstreifen nach Bewertung filtern (LF-BAT-10)"));
    // Leiste über dem Filmstreifen: Filter, Sterne, Verwerfen, kopierte Optionen übernehmen
    auto* stripBar = new QHBoxLayout;
    stripBar->addWidget(filter_);
    stripBar->addSpacing(12);
    for (int n = 1; n <= 5; ++n) {
        auto* b = new QToolButton(stripBox);
        b->setObjectName(QStringLiteral("stars%1").arg(n));
        b->setText(QString(QChar(0x2605))); // ★
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(tr("%n Stern(e) (Taste %n); nochmal klicken entfernt die Sterne", nullptr, n));
        connect(b, &QToolButton::clicked, this, [this, n] {
            const model::Frame* f = frame(s_.session.current);
            setStars(f != nullptr && f->stars == n ? 0 : n);
        });
        stripBar->addWidget(b);
        starButtons_.push_back(b);
    }
    stripBar->addSpacing(8);
    rejectButton_ = new QToolButton(stripBox);
    rejectButton_->setObjectName(QStringLiteral("rejectButton"));
    rejectButton_->setText(tr("✗ Verwerfen"));
    rejectButton_->setCheckable(true);
    rejectButton_->setToolTip(tr("Ausgewählte Bilder verwerfen bzw. wieder aufnehmen (X)"));
    connect(rejectButton_, &QToolButton::clicked, this,
            [this](bool on) { rate(on ? model::Rating::Reject : model::Rating::None); });
    stripBar->addWidget(rejectButton_);
    stripBar->addStretch(1);
    stripBar->addWidget(new QLabel(tr("Kopierte Bearbeitungsoptionen übernehmen auf:"), stripBox));
    auto* toSelection = new QPushButton(tr("Auswahl"), stripBox);
    toSelection->setObjectName(QStringLiteral("applyToSelection"));
    toSelection->setToolTip(tr("Kopierte Bearbeitungsoptionen auf die markierten Bilder übernehmen"));
    auto* toAll = new QPushButton(tr("alle"), stripBox);
    toAll->setObjectName(QStringLiteral("applyToAll"));
    toAll->setToolTip(tr("Kopierte Bearbeitungsoptionen auf alle Bilder der Rolle übernehmen"));
    connect(toSelection, &QPushButton::clicked, this,
            [this] { applyCopiedTo(filmstrip_->selectedFrames(), false); });
    connect(toAll, &QPushButton::clicked, this, [this] {
        std::vector<model::FrameId> all;
        for (const model::Frame& f : frames_) {
            all.push_back(f.id);
        }
        applyCopiedTo(all, false);
    });
    stripBar->addWidget(toSelection);
    stripBar->addWidget(toAll);
    stripLayout->addLayout(stripBar);
    stripLayout->addWidget(filmstrip_);
    stripDock->setWidget(stripBox);
    addDockWidget(Qt::BottomDockWidgetArea, stripDock);
    editorDocks_.push_back(stripDock);

    // Leuchttisch: Kopf mit Rolle und Filmtyp der Rolle, darunter alle Bilder im aktuellen Stand,
    // rechts Export und Kontaktabzug
    auto* overviewOuter = new QHBoxLayout(overviewPage_);
    auto* overviewLayout = new QVBoxLayout;
    overviewOuter->addLayout(overviewLayout, 1);
    auto* head = new QHBoxLayout;
    overviewTitle_ = new QLabel(overviewPage_);
    QFont big = overviewTitle_->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    big.setBold(true);
    overviewTitle_->setFont(big);
    head->addWidget(overviewTitle_, 1);
    head->addWidget(new QLabel(tr("Filmtyp der Rolle"), overviewPage_));
    rollFilm_ = new QComboBox(overviewPage_);
    rollFilm_->setObjectName(QStringLiteral("rollFilm"));
    rollFilm_->addItem(tr("– Filmtyp wählen –"), static_cast<int>(model::FilmType::Unconverted));
    rollFilm_->addItem(tr("Farbnegativ"), static_cast<int>(model::FilmType::ColorNegative));
    rollFilm_->addItem(tr("S/W-Negativ"), static_cast<int>(model::FilmType::BwNegative));
    rollFilm_->addItem(tr("Dia"), static_cast<int>(model::FilmType::Slide));
    rollFilm_->setToolTip(tr("Gilt für alle Bilder ohne eigenen Filmtyp (Reiter Konvertierung)"));
    head->addWidget(rollFilm_);
    overviewLayout->addLayout(head);
    overview_ = new OverviewView(overviewPage_);
    overview_->share(filmstrip_);
    overviewLayout->addWidget(overview_, 1);

    auto* side = new QWidget(overviewPage_);
    side->setObjectName(QStringLiteral("lightTableSide"));
    side->setFixedWidth(260);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(4, 0, 0, 0);
    selectionInfo_ = new QLabel(side);
    selectionInfo_->setObjectName(QStringLiteral("selectionInfo"));
    sideLayout->addWidget(selectionInfo_);
    auto* exportBox = new QGroupBox(tr("Exportieren"), side);
    auto* exportLayout = new QVBoxLayout(exportBox);
    exportFolder_ = new QLabel(exportBox);
    exportFolder_->setObjectName(QStringLiteral("exportFolder"));
    auto* folderButton = new QPushButton(tr("Zielordner …"), exportBox);
    folderButton->setObjectName(QStringLiteral("exportFolderButton"));
    exportFormat_ = new QComboBox(exportBox);
    exportFormat_->setObjectName(QStringLiteral("exportFormat"));
    exportFormat_->addItem(tr("JPEG (8 Bit)"));
    exportFormat_->addItem(tr("TIFF (16 Bit)"));
    exportFormat_->addItem(tr("PNG (16 Bit)"));
    auto* exportSelected = new QPushButton(tr("Auswahl exportieren"), exportBox);
    exportSelected->setObjectName(QStringLiteral("exportSelected"));
    auto* exportAll = new QPushButton(tr("Alle exportieren"), exportBox);
    exportAll->setObjectName(QStringLiteral("exportAll"));
    auto* exportMore = new QPushButton(tr("Weitere Einstellungen …"), exportBox);
    exportMore->setObjectName(QStringLiteral("exportMore"));
    exportMore->setFlat(true);
    exportLayout->addWidget(exportFolder_);
    exportLayout->addWidget(folderButton);
    exportLayout->addWidget(exportFormat_);
    exportLayout->addWidget(exportSelected);
    exportLayout->addWidget(exportAll);
    exportLayout->addWidget(exportMore);
    sideLayout->addWidget(exportBox);
    auto* copyBox = new QGroupBox(tr("Kopierte Bearbeitungsoptionen"), side);
    auto* copyLayout = new QHBoxLayout(copyBox);
    auto* lightSelection = new QPushButton(tr("auf Auswahl"), copyBox);
    lightSelection->setObjectName(QStringLiteral("lightApplySelection"));
    lightSelection->setToolTip(tr("Kopierte Bearbeitungsoptionen auf die markierten Bilder übernehmen"));
    auto* lightAll = new QPushButton(tr("auf alle"), copyBox);
    lightAll->setObjectName(QStringLiteral("lightApplyAll"));
    lightAll->setToolTip(tr("Kopierte Bearbeitungsoptionen auf alle Bilder der Rolle übernehmen"));
    copyLayout->addWidget(lightSelection);
    copyLayout->addWidget(lightAll);
    sideLayout->addWidget(copyBox);
    connect(lightSelection, &QPushButton::clicked, this,
            [this] { applyCopiedTo(filmstrip_->selectedFrames(), true); });
    connect(lightAll, &QPushButton::clicked, this, [this] {
        std::vector<model::FrameId> all;
        for (const model::Frame& f : frames_) {
            all.push_back(f.id);
        }
        applyCopiedTo(all, true);
    });
    auto* sheetBox = new QGroupBox(tr("Kontaktabzug"), side);
    auto* sheetLayout = new QVBoxLayout(sheetBox);
    auto* sheetPdf = new QPushButton(tr("Als PDF speichern …"), sheetBox);
    sheetPdf->setObjectName(QStringLiteral("sheetPdf"));
    auto* sheetSetup = new QPushButton(tr("Einrichten und drucken …"), sheetBox);
    sheetSetup->setObjectName(QStringLiteral("sheetSetup"));
    sheetLayout->addWidget(sheetPdf);
    sheetLayout->addWidget(sheetSetup);
    sideLayout->addWidget(sheetBox);
    sideLayout->addStretch(1);
    overviewOuter->addWidget(side);
    connect(folderButton, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Zielordner für den Export"),
                                                            qs(exportSettings_.folder));
        if (!d.isEmpty()) {
            exportSettings_.folder = std::filesystem::path(d.toStdU16String());
            updateExportPanel();
        }
    });
    connect(exportFormat_, &QComboBox::activated, this, [this](int i) {
        static const std::array<std::pair<ports::FileFormat, int>, 3> f{
            std::pair{ports::FileFormat::Jpeg, 8}, std::pair{ports::FileFormat::Tiff, 16},
            std::pair{ports::FileFormat::Png, 16}};
        exportSettings_.write.format = f[static_cast<std::size_t>(i)].first;
        exportSettings_.write.bitDepth = f[static_cast<std::size_t>(i)].second;
    });
    connect(exportSelected, &QPushButton::clicked, this,
            [this] { exportFrames(filmstrip_->selectedFrames()); });
    connect(exportAll, &QPushButton::clicked, this, [this] {
        std::vector<model::FrameId> all;
        for (const model::Frame& f : frames_) {
            all.push_back(f.id);
        }
        exportFrames(all);
    });
    connect(exportMore, &QPushButton::clicked, this, [this] {
        ExportDialog d(this);
        d.setPresets(&s_.presets);
        d.setSettings(exportSettings_);
        if (d.exec() == QDialog::Accepted) {
            exportSettings_ = d.settings();
            updateExportPanel();
        }
    });
    connect(sheetPdf, &QPushButton::clicked, this, [this] { contactSheetPdf(); });
    connect(sheetSetup, &QPushButton::clicked, this, [this] { doContactSheet(); });
    import_->setPreviewModel(stripModel_);
    connect(overview_, &OverviewView::openFrame, this, [this](const model::FrameId& f) {
        selectFrame(f);
        showView(View::Image);
    });
    connect(rollFilm_, &QComboBox::activated, this,
            [this](int i) { setRollFilm(static_cast<model::FilmType>(rollFilm_->itemData(i).toInt())); });
    connect(import_, &ImportPage::accepted, this, [this](model::FilmType film, bool detectNow) {
        excludeFrames(import_->excludedFrames());
        setRollFilm(film);
        showView(View::Image);
        if (detectNow) {
            std::vector<model::FrameId> all;
            for (const model::Frame& f : frames_) {
                all.push_back(f.id);
            }
            detect(all, tr("Bildfeld erkennen"), automatic(&application::AnalysisService::autoConvert));
        }
    });
    connect(import_, &ImportPage::cancelled, this, [this] { closeRoll(); });
    connect(filmstrip_, &FilmstripView::currentFrameChanged, this, &MainWindow::selectFrame);
    connect(filmstrip_, &FilmstripView::selectionChangedFrames, this, [this] {
        s_.session.selection = filmstrip_->selectedFrames();
        updateExportPanel();
    });
    connect(filter_, &QComboBox::activated, this, [this](int i) {
        static const std::array<std::optional<model::Rating>, 4> f{
            std::nullopt, model::Rating::Keep, model::Rating::Reject, model::Rating::None};
        stripModel_->setFilter(f[static_cast<std::size_t>(i)]);
    });

    tools_ = new QTabWidget(this);
    tools_->setObjectName(QStringLiteral("tools"));
    conversion_ = new ConversionPanel(*ui_, tools_);
    gradation_ = new GradationPanel(*ui_, tools_);
    adjust_ = new AdjustPanel(*ui_, tools_);
    tools_->addTab(conversion_, tr("Konvertierung"));
    tools_->addTab(gradation_, tr("Gradation"));
    auto* adjustScroll = new QScrollArea(tools_);
    auto* adjustPage = new QWidget(adjustScroll);
    auto* adjustLayout = new QVBoxLayout(adjustPage);
    adjustLayout->setContentsMargins(0, 0, 0, 0);
    filmProfile_ = new FilmProfilePanel(*ui_, adjustPage);
    filmProfile_->setObjectName(QStringLiteral("filmProfilePanel"));
    adjustLayout->addWidget(adjust_);
    adjustLayout->addWidget(filmProfile_); // am Ende von Licht und Farbe
    adjustScroll->setWidget(adjustPage);
    adjustScroll->setWidgetResizable(true);
    tools_->addTab(adjustScroll, tr("Licht und Farbe"));
    imageTools_ = new QWidget(tools_);
    imageTools_->setObjectName(QStringLiteral("imageTools"));
    tools_->insertTab(1, imageTools_, tr("Zuschnitt")); // Konvertierung bleibt vorn
    retouchTab_ = new QWidget(tools_);
    retouchTab_->setObjectName(QStringLiteral("retouchTools"));
    tools_->insertTab(2, retouchTab_, tr("Staubretusche"));
    auto* toolDock = new QDockWidget(tr("Werkzeuge"), this);
    toolDock->setObjectName(QStringLiteral("toolsDock"));
    toolDock->setWidget(tools_);
    addDockWidget(Qt::RightDockWidgetArea, toolDock);
    editorDocks_.push_back(toolDock);

    histogram_ = new HistogramWidget(this);
    histogram_->setObjectName(QStringLiteral("histogram"));
    auto* histDock = new QDockWidget(tr("Histogramm"), this);
    histDock->setObjectName(QStringLiteral("histogramDock"));
    histDock->setWidget(histogram_);
    addDockWidget(Qt::RightDockWidgetArea, histDock);
    editorDocks_.push_back(histDock);

    editModel_ = new EditListModel(*ui_, this);
    editView_ = new QTableView(this);
    editView_->setObjectName(QStringLiteral("editList"));
    editView_->setModel(editModel_);
    editView_->horizontalHeader()->setStretchLastSection(true);
    editView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    editView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    editView_->verticalHeader()->hide();
    auto* listBox = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listBox);
    listLayout->setContentsMargins(2, 2, 2, 2);
    listLayout->addWidget(editView_);
    auto* listButtons = new QHBoxLayout;
    auto* remove = new QPushButton(tr("Ausgewählte rückgängig machen"), listBox);
    remove->setObjectName(QStringLiteral("removeEntry"));
    remove->setToolTip(tr("Ausgewählte Bearbeitungsoptionen mit einem Klick rückgängig machen (LF-LST-03)"));
    auto* copyEntries = new QPushButton(tr("Ausgewählte kopieren"), listBox);
    copyEntries->setObjectName(QStringLiteral("copyEntries"));
    copyEntries->setToolTip(
        tr("Ausgewählte Bearbeitungsoptionen kopieren (ohne Auswahl: alle); übernehmen am Filmstreifen"));
    listButtons->addWidget(remove);
    listButtons->addWidget(copyEntries);
    listLayout->addLayout(listButtons);
    auto* listDock = new QDockWidget(tr("Bearbeitungsoptionen"), this);
    listDock->setObjectName(QStringLiteral("editListDock"));
    listDock->setWidget(listBox);
    addDockWidget(Qt::RightDockWidgetArea, listDock);
    editorDocks_.push_back(listDock);
    connect(remove, &QPushButton::clicked, this, [this] {
        std::vector<int> rows;
        for (const QModelIndex& i : editView_->selectionModel()->selectedRows()) {
            rows.push_back(i.row());
        }
        if (rows.empty() && editView_->currentIndex().isValid()) {
            rows.push_back(editView_->currentIndex().row());
        }
        editModel_->removeEntries(rows); // ein Schritt im Verlauf
    });
    connect(copyEntries, &QPushButton::clicked, this, [this] { copyOptions(); });

    connect(conversion_, &ConversionPanel::filmTypeChosen, this,
            [this](const QString& type) { chooseFilm(type.toStdString()); });
    // jede Automatik macht nur ihren Schritt
    // Schalter: an = Automatik für die markierten Bilder, aus = Schritt wieder entfernen
    connect(conversion_, &ConversionPanel::autoFrameToggled, this, [this](bool on) {
        removeSteps({id::crop, id::rotation}); // auch einen eigenen Zuschnitt ersetzen
        if (on) {
            showFrameAfterDetect_ = s_.session.current; // erkannten Rahmen zeigen, ziehbar
            detect(targets(), tr("Bildfeld erkennen"), automatic(&application::AnalysisService::autoFrame),
                   QStringLiteral("autoFrame"));
        }
    });
    connect(conversion_, &ConversionPanel::autoBaseToggled, this, [this](bool on) {
        conversion_->setPickActive(false);
        setTool(0); // zurück zum zugeschnittenen Bild
        // vorhandene Basis (auch die gemessene) entfernen: die Automatik überschreibt keine
        // Einträge des Anwenders, sonst bliebe die alte Basis stehen
        removeSteps({id::filmbase});
        if (on) {
            detect(targets(), tr("Basis messen"), automatic(&application::AnalysisService::autoBase),
                   QStringLiteral("autoBase"));
        }
    });
    connect(conversion_, &ConversionPanel::pickBaseToggled, this, [this](bool on) {
        if (on) {
            removeSteps({id::filmbase}); // ersetzt die automatische Basis: nie beide zugleich
            conversion_->setPickActive(true);
            conversion_->bindList(ui_->currentList());
            setTool(2); // ganzes Bild, Rechteck ziehen
        } else {
            conversion_->setPickActive(false);
            setTool(0);
            removeSteps({id::filmbase});
        }
    });
    connect(conversion_, &ConversionPanel::convertToggled, this, [this](bool on) {
        if (!on) {
            removeSteps({id::convert});
            return;
        }
        if (ui_->currentList().get(model::StepId(std::string(id::filmbase))) == nullptr) {
            report(tr("Zuerst die Filmbasis bestimmen (Auto-Basis oder Basis messen)."));
            refreshCurrent();
            return;
        }
        // Tonwerte aus Bild und Basis, dann umkehren
        detect(targets(), tr("Konvertieren"), automatic(&application::AnalysisService::convert),
               QStringLiteral("convert"));
    });
    connect(conversion_, &ConversionPanel::frameOptionsRequested, this, [this] { doFrameOptions(); });
    connect(conversion_, &ConversionPanel::autoConvertToggled, this, [this](bool on) { setAutoConvert(on); });
    // Film-Farbprofil: Wahl in Konvertierung oder Licht und Farbe; gilt für die markierten Bilder
    connect(conversion_, &ConversionPanel::filmProfileChosen, this,
            [this](const QString& key) { chooseFilmProfile(key, true); });
    connect(filmProfile_, &FilmProfilePanel::filmProfileChosen, this,
            [this](const QString& key) { chooseFilmProfile(key, true); });
    connect(filmProfile_, &FilmProfilePanel::saveRequested, this, [this] { saveFilmProfile(); });
    connect(filmProfile_, &FilmProfilePanel::deleteRequested, this, [this](const QString& name) {
        if (name.startsWith(QLatin1Char(':'))) {
            return;
        }
        if (QMessageBox::question(this, tr("Film-Farbprofil löschen"),
                                  tr("Profil „%1“ löschen? Bereits bearbeitete Bilder behalten ihre Werte.")
                                      .arg(name)) != QMessageBox::Yes) {
            return;
        }
        if (auto r = s_.presets.remove({ports::PresetKind::Base, name.toStdString()}); !r) {
            report(QString::fromStdString(r.error().message));
        }
        chooseFilmProfile(FilmProfilePanel::kNeutral, false);
    });
    connect(conversion_, &ConversionPanel::showBaseAreaToggled, this, [this](bool on) {
        showArea_ = on;
        setTool(on ? 4 : 0);
    });

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("jobProgress"));
    progress_->setRange(0, 1000);
    progress_->hide();
    cancel_ = new QPushButton(tr("Abbrechen"), this);
    cancel_->setObjectName(QStringLiteral("cancelJobs"));
    cancel_->setToolTip(tr("Laufende Stapelaufträge abbrechen (Export, Erkennung, Kontaktabzug)"));
    cancel_->hide();
    connect(cancel_, &QPushButton::clicked, this, [this] {
        for (const application::JobHandle& h : running_) {
            h.cancel();
        }
    });
    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(cancel_);

    // Zoom sichtbar und einstellbar: Regler (logarithmisch 2 … 800 %) und Prozentfeld
    zoomBar_ = new QWidget(this);
    zoomBar_->setObjectName(QStringLiteral("zoomBar"));
    auto* zoomLayout = new QHBoxLayout(zoomBar_);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    auto* fitButton = new QToolButton(zoomBar_);
    fitButton->setText(tr("Einpassen"));
    fitButton->setToolTip(tr("Ganzes Bild zeigen (Strg+0)"));
    fitButton->setAutoRaise(true);
    zoomSlider_ = new QSlider(Qt::Horizontal, zoomBar_);
    zoomSlider_->setObjectName(QStringLiteral("zoomSlider"));
    zoomSlider_->setRange(0, 1000);
    zoomSlider_->setFixedWidth(160);
    zoomSlider_->setToolTip(tr("Zoom (Strg+Mausrad, Strg+Plus/Minus)"));
    zoomPercent_ = new QSpinBox(zoomBar_);
    zoomPercent_->setObjectName(QStringLiteral("zoomPercent"));
    zoomPercent_->setRange(static_cast<int>(ImageCanvas::kMinScale * 100), static_cast<int>(ImageCanvas::kMaxScale * 100));
    zoomPercent_->setSuffix(QStringLiteral(" %"));
    zoomPercent_->setKeyboardTracking(false);
    zoomLayout->addWidget(fitButton);
    zoomLayout->addWidget(zoomSlider_);
    zoomLayout->addWidget(zoomPercent_);
    statusBar()->addPermanentWidget(zoomBar_);
    static const double lo = std::log(ImageCanvas::kMinScale);
    static const double hi = std::log(ImageCanvas::kMaxScale);
    connect(fitButton, &QToolButton::clicked, this, [this] { canvas_->setZoom(ImageCanvas::Zoom::Fit); });
    connect(zoomSlider_, &QSlider::valueChanged, this, [this](int v) {
        canvas_->setScale(std::exp(lo + (hi - lo) * v / 1000.0));
    });
    connect(zoomPercent_, &QSpinBox::valueChanged, this, [this](int p) { canvas_->setScale(p / 100.0); });
    connect(canvas_, &ImageCanvas::zoomChanged, this, [this] {
        const double s = canvas_->scale();
        const QSignalBlocker b1(zoomSlider_);
        const QSignalBlocker b2(zoomPercent_);
        zoomSlider_->setValue(static_cast<int>(std::lround((std::log(s) - lo) / (hi - lo) * 1000.0)));
        zoomPercent_->setValue(static_cast<int>(std::lround(s * 100.0)));
    });
    resize(1400, 900);
}

QAction* MainWindow::action(const QString& name) const
{
    const auto it = actions_.find(name);
    return it != actions_.end() ? it->second : nullptr;
}

void MainWindow::buildActions()
{
    const auto add = [this](QMenu* menu, const QString& name, const QString& text, const QKeySequence& key,
                            auto&& slot) {
        auto* a = new QAction(text, this);
        a->setObjectName(name);
        a->setShortcut(key);
        a->setShortcutContext(Qt::ApplicationShortcut);
        menu->addAction(a);
        connect(a, &QAction::triggered, this, std::forward<decltype(slot)>(slot));
        actions_[name] = a;
        return a;
    };
    QMenu* file = menuBar()->addMenu(tr("&Datei"));
    add(file, QStringLiteral("openFolder"), tr("Ordner öffnen …"), QKeySequence::Open, [this] {
#ifdef AUTONEG_DEBUG_OPEN_DIR
        const QString start = QStringLiteral(AUTONEG_DEBUG_OPEN_DIR); // Debug: Testdaten
#else
        const QString start;
#endif
        const QString d = QFileDialog::getExistingDirectory(this, tr("Scan-Ordner öffnen"), start);
        if (!d.isEmpty()) {
            openFolder(std::filesystem::path(d.toStdU16String()));
        }
    });
    add(file, QStringLiteral("export"), tr("Exportieren …"), QKeySequence(Qt::CTRL | Qt::Key_E),
        [this] { doExport(); });
    add(file, QStringLiteral("contactSheet"), tr("Kontaktabzug …"), QKeySequence(Qt::CTRL | Qt::Key_K),
        [this] { doContactSheet(); });
    file->addSeparator();
    add(file, QStringLiteral("settings"), tr("Einstellungen …"), QKeySequence::Preferences,
        [this] { doSettings(); });
    add(file, QStringLiteral("quit"), tr("Beenden"), QKeySequence::Quit, [this] { close(); });

    QMenu* edit = menuBar()->addMenu(tr("&Bearbeiten"));
    add(edit, QStringLiteral("undo"), tr("↶ Rückgängig"), QKeySequence(Qt::CTRL | Qt::Key_Z),
        [this] { ui_->undo(); });
    add(edit, QStringLiteral("redo"), tr("↷ Wiederholen"), QKeySequence(Qt::CTRL | Qt::Key_Y),
        [this] { ui_->redo(); });
    edit->addSeparator();
    add(edit, QStringLiteral("copy"), tr("Bearbeitungsoptionen kopieren"), QKeySequence::Copy,
        [this] { copyOptions(); });
    add(edit, QStringLiteral("paste"), tr("Bearbeitungsoptionen einfügen …"), QKeySequence::Paste,
        [this] { doPaste(); });
    add(edit, QStringLiteral("resetAll"), tr("Bild zurücksetzen"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), [this] {
            // alle Einträge entfernen: ein Befehl je Eintrag wäre mehrstufig, deshalb einer für alle
            class ResetCommand final : public application::ICommand
            {
            public:
                std::string label() const override { return "Zurücksetzen"; }
                model::EditList apply(const model::EditList& /*l*/,
                                      const application::CommandContext& /*c*/) const override
                {
                    return {};
                }
            };
            ui_->apply(std::make_unique<ResetCommand>());
        });
    edit->addSeparator();
    add(edit, QStringLiteral("saveEditPreset"), tr("Als Vorlage speichern …"), QKeySequence(), [this] {
        // Bearbeitungsvorlage (LF-BAT-07) ohne bildbezogene Einträge (Bildfeld, Basis, Retusche)
        if (!s_.encodeEdits || s_.session.current == model::FrameId{}) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Vorlage speichern"), tr("Name der Vorlage"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        model::EditList l;
        for (const model::EditEntry& e : ui_->currentList().entries()) {
            if (!application::isFrameSpecific(e.step)) {
                l = l.with(e);
            }
        }
        if (auto r =
                s_.presets.save(ports::PresetKind::Edit, name.trimmed().toStdString(), s_.encodeEdits(l));
            !r) {
            report(QString::fromStdString(r.error().message));
        }
    });
    QMenu* apply = edit->addMenu(tr("Vorlage anwenden"));
    apply->setObjectName(QStringLiteral("applyEditPreset"));
    connect(apply, &QMenu::aboutToShow, this, [this, apply] {
        apply->clear();
        for (const ports::PresetInfo& p : s_.presets.list(ports::PresetKind::Edit)) {
            const QString name = QString::fromStdString(p.id.name);
            apply->addAction(name, this, [this, name] { applyEditPreset(name); });
        }
    });
    auto* sync = add(edit, QStringLiteral("sync"), tr("Synchronisieren"), QKeySequence(Qt::CTRL | Qt::Key_L),
                     [this](bool on) { s_.sync.setEnabled(on); });
    sync->setCheckable(true);
    edit->addSeparator();
    add(edit, QStringLiteral("rateKeep"), tr("Behalten"), QKeySequence(Qt::Key_P),
        [this] { rate(model::Rating::Keep); });
    add(edit, QStringLiteral("rateReject"), tr("Verwerfen"), QKeySequence(Qt::Key_X),
        [this] { rate(model::Rating::Reject); });
    add(edit, QStringLiteral("rateNone"), tr("Bewertung entfernen"), QKeySequence(Qt::Key_U),
        [this] { rate(model::Rating::None); });
    QMenu* stars = edit->addMenu(tr("Sterne"));
    for (int n = 0; n <= 5; ++n) {
        add(stars, QStringLiteral("stars%1Action").arg(n),
            n == 0 ? tr("Keine Sterne") : QString(n, QChar(0x2605)), QKeySequence(Qt::Key_0 + n),
            [this, n] { setStars(n); });
    }
    edit->addSeparator();
    add(edit, QStringLiteral("applyToSelection"), tr("Kopierte Bearbeitungsoptionen auf Auswahl übernehmen"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
        [this] { applyCopiedTo(filmstrip_->selectedFrames(), view_ == View::Overview); });
    add(edit, QStringLiteral("applyToAll"), tr("Kopierte Bearbeitungsoptionen auf alle übernehmen"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V), [this] {
            std::vector<model::FrameId> all;
            for (const model::Frame& f : frames_) {
                all.push_back(f.id);
            }
            applyCopiedTo(all, view_ == View::Overview);
        });

    QMenu* view = menuBar()->addMenu(tr("&Ansicht"));
    add(view, QStringLiteral("prevImage"), tr("Vorheriges Bild"), QKeySequence(Qt::CTRL | Qt::Key_Left),
        [this] { stepImage(-1); });
    add(view, QStringLiteral("nextImage"), tr("Nächstes Bild"), QKeySequence(Qt::CTRL | Qt::Key_Right),
        [this] { stepImage(1); });
    view->addSeparator();
    add(view, QStringLiteral("zoomIn"), tr("Vergrößern"), QKeySequence(Qt::CTRL | Qt::Key_Plus),
        [this] { canvas_->zoomBy(1.25); });
    add(view, QStringLiteral("zoomOut"), tr("Verkleinern"), QKeySequence(Qt::CTRL | Qt::Key_Minus),
        [this] { canvas_->zoomBy(0.8); });
    add(view, QStringLiteral("zoomFit"), tr("Einpassen"), QKeySequence(Qt::CTRL | Qt::Key_0),
        [this] { canvas_->setZoom(ImageCanvas::Zoom::Fit); });
    add(view, QStringLiteral("zoom100"), tr("100 %"), QKeySequence(Qt::CTRL | Qt::Key_1),
        [this] { canvas_->setZoom(ImageCanvas::Zoom::Actual); });
    add(view, QStringLiteral("zoom200"), tr("200 %"), QKeySequence(Qt::CTRL | Qt::Key_2),
        [this] { canvas_->setZoom(ImageCanvas::Zoom::Double); });
    auto* compare = add(view, QStringLiteral("compare"), tr("Vorher/Nachher"),
                        QKeySequence(Qt::Key_Backslash), [this](bool on) {
                            compare_ = on;
                            requestPreview(ui_->currentList());
                        });
    compare->setCheckable(true);
    view->addSeparator();
    auto* crop = add(view, QStringLiteral("toolCrop"), tr("Zuschneiden und Drehen"), QKeySequence(Qt::Key_C),
                     [this](bool on) { setTool(on ? 1 : 0); });
    crop->setCheckable(true);
    auto* retouch = add(view, QStringLiteral("toolRetouch"), tr("Staubretusche"), QKeySequence(Qt::Key_R),
                        [this](bool on) { setTool(on ? 3 : 0); });
    retouch->setCheckable(true);
    // Drehen in 90°-Schritten und Spiegeln (LF-EDT-02, LF-EDT-03): ein Eintrag Orientierung
    const auto orient = [this](int turns, bool flipH) {
        const model::EditList cur = ui_->currentList();
        int q = 0;
        bool fh = false;
        bool fv = false;
        if (const model::EditEntry* e = cur.get(model::StepId(std::string(id::orientation)))) {
            q = e->params.get<int>(key::quarterTurns).value_or(0);
            fh = e->params.get<bool>(key::flipH).value_or(false);
            fv = e->params.get<bool>(key::flipV).value_or(false);
        }
        q = ((q + turns) % 4 + 4) % 4;
        fh = flipH ? !fh : fh;
        model::EditEntry e = entryWith(cur, id::orientation, key::quarterTurns, q);
        e.params = e.params.with(key::flipH, fh).with(key::flipV, fv);
        if (q == 0 && !fh && !fv) {
            ui_->apply(std::make_unique<application::RemoveEntryCommand>(e.step)); // neutral: kein Eintrag
        } else {
            ui_->apply(std::make_unique<application::SetEntryCommand>(std::move(e)));
        }
    };
    add(view, QStringLiteral("rotateLeft"), tr("90° nach links drehen"),
        QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [orient] { orient(3, false); });
    add(view, QStringLiteral("rotateRight"), tr("90° nach rechts drehen"),
        QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [orient] { orient(1, false); });
    add(view, QStringLiteral("flipH"), tr("Waagerecht spiegeln"), QKeySequence(Qt::CTRL | Qt::Key_H),
        [orient] { orient(0, true); });
    // Kamera-Scans einer Rolle liegen meist alle gleich: Drehung des Bilds auf die ganze Rolle
    add(view, QStringLiteral("orientAll"), tr("Drehung für alle übernehmen"), QKeySequence(), [this] {
        if (!roll_ || frames_.empty()) {
            return;
        }
        const model::StepId step{std::string(id::orientation)};
        std::vector<model::FrameId> all;
        for (const model::Frame& f : frames_) {
            all.push_back(f.id);
        }
        if (const model::EditEntry* e = ui_->currentList().get(step)) {
            s_.bus.execute(application::SetEntryCommand(*e), all, *roll_, true);
        } else {
            s_.bus.execute(application::RemoveEntryCommand(step), all, *roll_, true);
        }
        refreshAllThumbnails();
        report(tr("Drehung auf %n Bild(er) übernommen", nullptr, static_cast<int>(all.size())));
    });

    for (QAction* a : {action(QStringLiteral("openFolder")), action(QStringLiteral("undo")),
                       action(QStringLiteral("redo")), action(QStringLiteral("toolCrop")),
                       action(QStringLiteral("toolRetouch")), action(QStringLiteral("compare")),
                       action(QStringLiteral("export")), action(QStringLiteral("contactSheet"))}) {
        a->setToolTip(a->text() + QStringLiteral(" (") + a->shortcut().toString(QKeySequence::NativeText) +
                      QStringLiteral(")"));
    }
    // Rückgängig/Wiederholen als Pfeile; Ordner öffnen und Kontaktabzug nur auf dem Leuchttisch
    QToolBar* bar = addToolBar(tr("Werkzeugleiste"));
    bar->setObjectName(QStringLiteral("toolbar"));
    for (const char* n : {"openFolder", "undo", "redo", "compare", "export", "contactSheet"}) {
        bar->addAction(action(QString::fromLatin1(n)));
    }
    action(QStringLiteral("undo"))->setIconText(QStringLiteral("↶"));
    action(QStringLiteral("redo"))->setIconText(QStringLiteral("↷"));
    action(QStringLiteral("undo"))
        ->setToolTip(tr("Rückgängig (Strg+Z)") + QStringLiteral(": ") + tr("in Entwickeln das aktive Bild") +
                     QStringLiteral(", ") + tr("auf dem Leuchttisch Aktionen auf mehrere Bilder"));
    action(QStringLiteral("redo"))
        ->setToolTip(tr("Wiederholen (Strg+Y): nimmt das letzte Rückgängig zurück"));
    if (auto* w = bar->widgetForAction(action(QStringLiteral("undo")))) {
        QFont f = w->font();
        f.setPointSizeF(f.pointSizeF() * 1.6);
        w->setFont(f);
        bar->widgetForAction(action(QStringLiteral("redo")))->setFont(f);
    }
    // Reiter „Zuschnitt und Retusche“ der Werkzeuge
    auto* imageLayout = new QVBoxLayout(imageTools_);
    for (const char* n : {"toolCrop", "rotateLeft", "rotateRight", "flipH"}) {
        auto* b = new QToolButton(imageTools_);
        b->setDefaultAction(action(QString::fromLatin1(n)));
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        imageLayout->addWidget(b);
    }
    imageLayout->addStretch(1);
    // Reiter Staubretusche
    auto* retouchLayout = new QVBoxLayout(retouchTab_);
    auto* retouchButton = new QToolButton(retouchTab_);
    retouchButton->setDefaultAction(action(QStringLiteral("toolRetouch")));
    retouchButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    retouchButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    retouchLayout->addWidget(retouchButton);
    auto* retouchHint =
        new QLabel(tr("Eingeschaltet setzt jeder Klick auf ein Staubkorn eine Retuschestelle.") +
                       QLatin1Char(' ') + tr("Alle Stellen eines Bilds sind eine Bearbeitungsoption."),
                   retouchTab_);
    retouchHint->setWordWrap(true);
    retouchLayout->addWidget(retouchHint);
    retouchLayout->addStretch(1);

    // Leuchttisch / Entwickeln: Reiter am linken Rand, schließbar, hier wieder zu öffnen (K-M10-08)
    QMenu* views = menuBar()->addMenu(tr("&Fenster"));
    auto* overviewAction = add(views, QStringLiteral("viewOverview"), tr("Leuchttisch"),
                               QKeySequence(Qt::Key_G), [this] { showView(View::Overview); });
    auto* imageAction = add(views, QStringLiteral("viewImage"), tr("Entwickeln"), QKeySequence(Qt::Key_E),
                            [this] { showView(View::Image); });
    overviewAction->setCheckable(true);
    imageAction->setCheckable(true);
    overviewAction->setToolTip(tr("Alle Bilder der Rolle: auswählen, bewerten, exportieren (G)"));
    imageAction->setToolTip(tr("Ein Bild bearbeiten (E)"));
    auto* group = new QActionGroup(this);
    group->addAction(overviewAction);
    group->addAction(imageAction);
    views->addSeparator();
    add(views, QStringLiteral("closeView"), tr("Ansicht schließen"), QKeySequence(Qt::CTRL | Qt::Key_W),
        [this] { closeView(view_); });
    views->addSeparator();
    // jedes Fenster einzeln ein- und ausblenden
    for (QDockWidget* d : editorDocks_) {
        QAction* t = d->toggleViewAction();
        t->setObjectName(QStringLiteral("show_") + d->objectName());
        views->addAction(t);
    }
    add(views, QStringLiteral("arrangeWindows"), tr("Automatisch anordnen"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] { arrangeWindows(); });
    for (auto& [name, a] : actions_) {
        a->setProperty("standardShortcut", a->shortcut());
    }
    applyShortcuts();
    {
        const auto v = s_.settings.get("filmProfile");
        const std::string* k = v ? v->asString() : nullptr;
        chooseFilmProfile(k != nullptr ? QString::fromStdString(*k) : FilmProfilePanel::kNeutral, false);
    }
    if (const auto v = s_.settings.get("panButton"); v && v->number()) {
        canvas_->setPanButton(static_cast<Qt::MouseButton>(static_cast<int>(*v->number())));
    }
    showView(View::Start);
}

void MainWindow::applyShortcuts()
{
    const auto saved = s_.settings.get("shortcuts");
    for (auto& [name, a] : actions_) {
        QKeySequence key = a->property("standardShortcut").value<QKeySequence>();
        if (const model::JsonValue* v = saved ? saved->find(name.toStdString()) : nullptr;
            v != nullptr && v->asString() != nullptr) {
            key = QKeySequence(QString::fromStdString(*v->asString()), QKeySequence::PortableText);
        }
        a->setShortcut(key);
    }
}

void MainWindow::stepImage(int delta)
{
    const auto it = std::ranges::find(frames_, s_.session.current, &model::Frame::id);
    if (it == frames_.end()) {
        return;
    }
    const auto i = std::clamp<std::ptrdiff_t>(it - frames_.begin() + delta, 0,
                                              static_cast<std::ptrdiff_t>(frames_.size()) - 1);
    s_.session.selection.clear();
    selectFrame(frames_[static_cast<std::size_t>(i)].id);
}

void MainWindow::showView(View v)
{
    // Ohne Rolle gibt es nur den Start; während des Imports keine Übersicht und keine Bildansicht
    if (!roll_ && v != View::Start) {
        v = View::Start;
    }
    view_ = v;
    // Reiter am linken Rand: geschlossene Ansicht wieder öffnen, die aktive markieren
    if (v == View::Overview || v == View::Image) {
        int tab = -1;
        for (int i = 0; i < viewTabs_->count(); ++i) {
            if (viewTabs_->tabData(i).toInt() == static_cast<int>(v)) {
                tab = i;
            }
        }
        const QSignalBlocker block(viewTabs_);
        if (tab < 0) {
            // feste Reihenfolge: Leuchttisch oben, Entwickeln darunter
            tab = viewTabs_->insertTab(v == View::Overview ? 0 : viewTabs_->count(),
                                       v == View::Overview ? tr("Leuchttisch") : tr("Entwickeln"));
            viewTabs_->setTabData(tab, static_cast<int>(v));
            viewTabs_->setTabToolTip(tab, v == View::Overview ? tr("Alle Bilder der Rolle (G)")
                                                              : tr("Ein Bild bearbeiten (E)"));
            // Eigener Schließknopf mit Abstand nach oben: der Stil setzt ihn sonst auf die
            // Oberkante des Reiters, wo der Rand ihn abschneidet
            auto* holder = new QWidget(viewTabs_);
            auto* holderLayout = new QVBoxLayout(holder);
            holderLayout->setContentsMargins(0, 8, 0, 2);
            auto* close = new QToolButton(holder);
            close->setText(QStringLiteral("✕"));
            close->setAutoRaise(true);
            close->setFixedSize(16, 16);
            close->setToolTip(tr("Ansicht schließen"));
            holderLayout->addWidget(close);
            const QTabBar::ButtonPosition side = static_cast<QTabBar::ButtonPosition>(
                viewTabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, viewTabs_));
            viewTabs_->setTabButton(tab, side, holder);
            connect(close, &QToolButton::clicked, this, [this, v] { closeView(v); });
        }
        viewTabs_->setCurrentIndex(tab);
    }
    QWidget* page = v == View::Start      ? startPage_
                    : v == View::Import   ? static_cast<QWidget*>(import_)
                    : v == View::Overview ? overviewPage_
                    : v == View::None     ? emptyPage_
                                          : static_cast<QWidget*>(canvas_);
    stack_->setCurrentWidget(page);
    for (QDockWidget* d : editorDocks_) {
        d->setVisible(v == View::Image);
    }
    const bool navigable = roll_ && v != View::Import;
    action(QStringLiteral("viewOverview"))->setEnabled(navigable);
    action(QStringLiteral("viewImage"))->setEnabled(navigable);
    action(QStringLiteral("viewOverview"))->setChecked(v == View::Overview);
    action(QStringLiteral("viewImage"))->setChecked(v == View::Image);
    viewTabs_->setVisible(navigable);
    // Leuchttisch: Rückgängig nur für Aktionen auf mehrere Bilder; Entwickeln: nur das aktive Bild
    ui_->setBatchScope(v == View::Overview);
    action(QStringLiteral("openFolder"))->setVisible(v != View::Image);
    action(QStringLiteral("contactSheet"))->setVisible(v == View::Overview);
    // Bildwerkzeuge nur in der Bildansicht; Befehle für die Rolle nicht während des Imports
    if (zoomBar_ != nullptr) {
        zoomBar_->setVisible(v == View::Image);
    }
    for (const char* n : {"toolCrop", "toolRetouch", "compare", "zoomIn", "zoomOut", "zoomFit", "zoom100",
                          "zoom200", "rotateLeft",
                          "rotateRight", "flipH"}) {
        if (QAction* a = action(QString::fromLatin1(n))) {
            a->setEnabled(v == View::Image);
        }
    }
    for (const char* n :
         {"export", "contactSheet", "undo", "redo", "copy", "paste", "resetAll", "saveEditPreset", "sync",
          "rateKeep", "rateReject", "rateNone", "applyToSelection", "applyToAll", "closeView"}) {
        if (QAction* a = action(QString::fromLatin1(n))) {
            a->setEnabled(navigable);
        }
    }
    if (v == View::Overview && roll_) {
        overviewTitle_->setText(tr("%1 – %n Bild(er)", nullptr, static_cast<int>(frames_.size()))
                                    .arg(QString::fromStdString(roll_->name)));
        rollFilm_->setCurrentIndex(std::max(0, rollFilm_->findData(static_cast<int>(roll_->filmType))));
        updateExportPanel();
    }
    if (v == View::Image) {
        refreshCurrent();
        requestPreview(ui_->currentList());
    }
}

void MainWindow::closeView(View v)
{
    if (v != View::Overview && v != View::Image) {
        return;
    }
    for (int i = 0; i < viewTabs_->count(); ++i) {
        if (viewTabs_->tabData(i).toInt() == static_cast<int>(v)) {
            const QSignalBlocker block(viewTabs_);
            viewTabs_->removeTab(i);
            break;
        }
    }
    if (view_ == v) {
        showView(viewTabs_->count() > 0
                     ? static_cast<View>(viewTabs_->tabData(viewTabs_->currentIndex()).toInt())
                     : View::None);
    }
}

void MainWindow::setRollFilm(model::FilmType film)
{
    if (!roll_) {
        return;
    }
    model::Roll r = *roll_;
    r.filmType = film;
    if (!readOnly_) {
        if (auto ok = s_.rolls.save(r); !ok) {
            report(tr("Rollendatei nicht gespeichert: %1").arg(QString::fromStdString(ok.error().message)));
        }
    }
    roll_ = std::make_shared<const model::Roll>(std::move(r));
    s_.session.roll = roll_;
    refreshAllThumbnails();
    refreshCurrent();
    requestPreview(ui_->currentList());
}

void MainWindow::refreshAllThumbnails()
{
    for (const model::Frame& f : frames_) {
        requestThumbnail(f);
    }
}

void MainWindow::closeRoll()
{
    if (roll_) {
        for (const model::Frame& f : frames_) {
            s_.bus.close(f.id);
        }
        s_.rolls.close(roll_->folder);
    }
    roll_.reset();
    s_.session.roll.reset();
    s_.session.current = {};
    s_.session.selection.clear();
    frames_.clear();
    sources_.clear();
    stripModel_->setFrames({});
    canvas_->setImage({}, {});
    setWindowTitle(QStringLiteral("AutoNeg"));
    showView(View::Start);
}

void MainWindow::setTool(int which)
{
    if (which != 4 && showArea_) {
        showArea_ = false;
        conversion_->setShowArea(false);
    }
    action(QStringLiteral("toolCrop"))->setChecked(which == 1);
    action(QStringLiteral("toolRetouch"))->setChecked(which == 3);
    switch (which) {
    case 1: {
        const model::EditList l = ui_->currentList();
        model::NormRect r;
        double angle = 0.0;
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::crop)))) {
            r = e->params.get<model::NormRect>(key::rect).value_or(model::NormRect{});
        }
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::rotation)))) {
            angle = e->params.get<double>(key::angle).value_or(0.0);
        }
        cropTool_->setState(r, angle);
        if (const auto it = sources_.find(s_.session.current); it != sources_.end()) {
            cropTool_->setSourceSize(QSizeF(it->second.width, it->second.height));
        }
        canvas_->setTool(cropTool_.get());
        break;
    }
    case 2:
        canvas_->setTool(baseTool_.get());
        break;
    case 4: {
        // Messbereich der Basis auf dem ganzen, ungeschnittenen Bild
        std::optional<model::NormRect> area;
        if (const model::EditEntry* e = ui_->currentList().get(model::StepId(std::string(id::filmbase)))) {
            if (auto a = e->params.get<model::NormRect>(key::area)) {
                area = *a;
            }
        }
        if (!area || area->w <= 0.0 || (area->w >= 1.0 && area->h >= 1.0)) {
            report(tr("Kein Messbereich gespeichert: zuerst Auto-Basis oder Basis messen."));
            setTool(0); // Häkchen zurück, Ansicht bleibt beim zugeschnittenen Bild
            return;
        }
        areaTool_->setRect(area);
        canvas_->setTool(areaTool_.get());
        break;
    }
    case 3: {
        model::SpotList spots;
        if (const model::EditEntry* e = ui_->currentList().get(model::StepId(std::string(id::retouch)))) {
            spots = e->params.get<model::SpotList>(key::spots).value_or(model::SpotList{});
        }
        retouchTool_->setSpots(spots);
        canvas_->setTool(retouchTool_.get());
        break;
    }
    default:
        canvas_->setTool(nullptr);
    }
    requestPreview(ui_->currentList()); // beim Zuschneiden das ungeschnittene Bild zeigen
}

void MainWindow::report(const QString& msg)
{
    messages_.push_back(msg);
    status_->setText(msg);
    if (s_.logger != nullptr) {
        s_.logger->log(ports::LogLevel::Info, msg.toStdString());
    }
}

void MainWindow::openFolder(const std::filesystem::path& folder)
{
    report(tr("Öffne %1 …").arg(qs(folder)));
    QPointer<MainWindow> self(this);
    QtSessionBridge* bridge = &s_.bridge;
    trackJob(s_.rolls.openFolder(
                 folder,
                 [self, bridge](base::Expected<application::OpenedRoll> r) {
                     // Job-Thread: Ergebnis über die Brücke in den UI-Thread
                     auto shared = std::make_shared<base::Expected<application::OpenedRoll>>(std::move(r));
                     bridge->post([self, shared] {
                         if (!self) {
                             return;
                         }
                         if (!*shared) {
                             self->report(tr("Ordner kann nicht geöffnet werden: %1")
                                              .arg(QString::fromStdString(shared->error().message)));
                             return;
                         }
                         self->onRollOpened(std::move(**shared));
                     });
                 }),
             tr("Rolle öffnen"));
}

void MainWindow::onRollOpened(application::OpenedRoll r)
{
    if (roll_) {
        for (const model::Frame& f : frames_) {
            s_.bus.close(f.id);
        }
        s_.rolls.close(roll_->folder);
    }
    roll_ = std::make_shared<const model::Roll>(r.roll);
    s_.session.roll = roll_;
    exportSettings_.folder = roll_->folder / "export";
    s_.session.current = {};
    s_.session.selection.clear();
    readOnly_ = r.readOnly;
    frames_.clear();
    sources_.clear();
    for (application::OpenedFrame& f : r.frames) {
        if (f.problem) {
            report(tr("%1: %2").arg(qs(f.frame.path.filename()), QString::fromStdString(f.problem->message)));
        }
        s_.bus.open(f.frame, f.data);
        frames_.push_back(f.frame);
    }
    for (const ports::SidecarRef& o : r.orphans) {
        report(tr("Begleitdatei ohne Scan: %1").arg(qs(o.path.filename())));
    }
    if (readOnly_) {
        report(tr("Die Rolle ist in einem anderen AutoNeg geöffnet und wird nur lesend angezeigt."));
    }
    setWindowTitle(QStringLiteral("AutoNeg – %1").arg(QString::fromStdString(roll_->name)));
    stripModel_->setFrames(frames_);
    for (const model::Frame& f : frames_) {
        requestThumbnail(f);
    }
    if (!frames_.empty()) {
        filmstrip_->selectFrame(frames_.front().id);
    }
    report(tr("%n Bild(er) geöffnet", nullptr, static_cast<int>(frames_.size())));
    refreshFilmProfiles();
    // beide Ansichten als Reiter; Entwickeln ist danach aktiv
    showView(View::Overview);
    // Neue Rolle: erst der Import-Bildschirm (Filmtyp festlegen), sonst gleich die Bildansicht
    if (roll_->filmType == model::FilmType::Unconverted && !readOnly_) {
        import_->setRoll(qs(roll_->folder), QString::fromStdString(roll_->name),
                         static_cast<int>(frames_.size()));
        showView(View::Import);
    } else {
        showView(View::Image);
    }
    Q_EMIT rollOpened();
}

const model::Frame* MainWindow::frame(const model::FrameId& f) const
{
    const auto it = std::ranges::find(frames_, f, &model::Frame::id);
    return it != frames_.end() ? &*it : nullptr;
}

std::vector<model::FrameId> MainWindow::targets() const
{
    std::vector<model::FrameId> t = filmstrip_->selectedFrames();
    if (t.empty() && s_.session.current != model::FrameId{}) {
        t.push_back(s_.session.current);
    }
    return t;
}

void MainWindow::requestThumbnail(const model::Frame& f)
{
    if (!roll_ || !s_.bus.isOpen(f.id)) {
        return;
    }
    QtSessionBridge* bridge = &s_.bridge;
    const model::FrameId fid = f.id;
    s_.thumbnails.request(f, s_.bus.history(f.id).current(), roll_->filmType, thumbnailSize,
                          [bridge, fid](const application::ThumbnailService::Result& r) {
                              if (r) {
                                  bridge->deliverThumbnail(
                                      fid, QByteArray(reinterpret_cast<const char*>(r->data()),
                                                      static_cast<qsizetype>(r->size())));
                              } else {
                                  bridge->deliverThumbnail(fid, {});
                              }
                          });
}

void MainWindow::selectFrame(const model::FrameId& f)
{
    const model::Frame* fr = frame(f);
    if (fr == nullptr || f == s_.session.current) {
        return;
    }
    s_.session.current = f;
    stripModel_->setActive(f);
    if (s_.currentChanged) {
        s_.currentChanged(f);
    }
    if (s_.session.selection.empty()) {
        s_.session.selection = {f};
    }
    filmstrip_->selectFrame(f);
    lastSeq_ = 0;
    canvas_->setImage({}, {});
    // Kopfdaten (Größe für 100 %) im Hintergrund holen
    if (!sources_.contains(f) && s_.sourceInfo) {
        const model::Frame copy = *fr;
        QPointer<MainWindow> self(this);
        QtSessionBridge* bridge = &s_.bridge;
        auto sourceInfo = s_.sourceInfo;
        auto job = s_.jobs.submit(
            std::make_unique<FunctionJob>("Kopfdaten",
                                          [self, bridge, sourceInfo, copy]() -> base::Expected<void> {
                                              auto i = sourceInfo(copy);
                                              if (!i) {
                                                  return std::unexpected(i.error());
                                              }
                                              bridge->post([self, id = copy.id, v = *i] {
                                                  if (!self) {
                                                      return;
                                                  }
                                                  self->sources_[id] = v;
                                                  if (id == self->s_.session.current) {
                                                      self->refreshCurrent();
                                                  }
                                              });
                                              return {};
                                          }),
            ports::Priority::Interactive);
        running_.push_back(job);
    }
    refreshCurrent();
}

model::FilmType MainWindow::filmOf(const model::FrameId& f) const
{
    const model::FilmType rollFilm = roll_ ? roll_->filmType : model::FilmType::Unconverted;
    if (f == model::FrameId{} || !s_.bus.isOpen(f)) {
        return rollFilm;
    }
    return steps::FilmModeStep::effective(s_.bus.history(f).current(), rollFilm);
}

void MainWindow::chooseFilm(const std::string& type)
{
    // Filmtyp für die markierten Bilder festlegen bzw. entfernen (K-M10-08)
    if (!roll_) {
        return;
    }
    const auto t = targets();
    if (type == "raw") {
        s_.bus.execute(application::RemoveEntryCommand(model::StepId(std::string(id::filmmode))), t, *roll_);
    } else {
        model::EditEntry e;
        e.step = model::StepId(std::string(id::filmmode));
        e.params = model::ParamSet{}.with(key::type, type);
        s_.bus.execute(application::SetEntryCommand(e), t, *roll_);
    }
    refreshCurrent();
}

MainWindow::Automatic MainWindow::automatic(application::JobHandle (application::AnalysisService::*a)(
    const model::Frame&, std::shared_ptr<const model::Roll>, model::FilmType, model::EditList))
{
    return [this, a](const model::Frame& f, model::FilmType film, model::EditList current) {
        return (s_.analysis.*a)(f, roll_, film, std::move(current));
    };
}

void MainWindow::redetect(const std::vector<model::FrameId>& frames, const std::set<model::StepId>& steps)
{
    detect(frames, tr("Automatik neu berechnen"),
           [this, steps](const model::Frame& f, model::FilmType film, model::EditList current) {
               return s_.analysis.redetect(f, roll_, film, std::move(current), steps);
           });
}

void MainWindow::detect(const std::vector<model::FrameId>& frames, const QString& what, const Automatic& run,
                        const QString& button)
{
    // Erkennung braucht den Filmtyp; Bilder ohne Filmtyp werden übersprungen und gemeldet
    int skipped = 0;
    for (const model::FrameId& f : frames) {
        const model::Frame* fr = frame(f);
        if (fr == nullptr) {
            continue;
        }
        const model::FilmType film = filmOf(f);
        if (film == model::FilmType::Unconverted) {
            ++skipped;
            continue;
        }
        application::JobHandle h = run(*fr, film, s_.bus.history(f).current());
        if (!button.isEmpty()) {
            busy_[button].push_back(h);
            conversion_->setBusy(button, true);
        }
        trackJob(h, what);
    }
    if (skipped > 0) {
        report(tr("%1: %n Bild(er) ohne Filmtyp übersprungen – zuerst den Filmtyp wählen.", nullptr, skipped)
                   .arg(what));
    }
}

void MainWindow::refreshCurrent()
{
    const model::EditList l = ui_->currentList();
    updateRatingButtons();
    editModel_->setList(l);
    conversion_->bindList(l);
    gradation_->bindList(l);
    adjust_->bindList(l);
    filmProfile_->bindList(l);
    // Bedienelemente nach Filmtyp: Gradation (Ilford Multigrade) nur bei S/W-Negativ
    const model::FilmType film = filmOf(s_.session.current);
    conversion_->setFilm(film);
    conversion_->setAutoMode(autoConverted(s_.session.current));
    {
        const auto it = frameReports_.find(s_.session.current);
        conversion_->setQuality(it != frameReports_.end() ? it->second : QString());
    }
    gradation_->setFilm(film);
    adjust_->setFilm(film);
    filmProfile_->setFilm(film);
    tools_->setTabVisible(tools_->indexOf(gradation_), film == model::FilmType::BwNegative);
    // Größe des Ergebnisses in Quellpixeln: Zuschnitt und Vierteldrehung berücksichtigt
    if (const auto it = sources_.find(s_.session.current); it != sources_.end() && it->second.width > 0) {
        double w = it->second.width;
        double h = it->second.height;
        if (canvas_->tool() != cropTool_.get()) {
            if (const model::EditEntry* e = l.get(model::StepId(std::string(id::crop))); e && e->enabled) {
                const auto r = e->params.get<model::NormRect>(key::rect).value_or(model::NormRect{});
                w *= r.w;
                h *= r.h;
            }
        }
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::orientation))); e && e->enabled) {
            if (e->params.get<int>(key::quarterTurns).value_or(0) % 2 == 1) {
                std::swap(w, h);
            }
        }
        canvas_->setSourceSize(QSize(static_cast<int>(w), static_cast<int>(h)));
    }
    requestPreview(l);
}

void MainWindow::requestPreview(const model::EditList& l)
{
    const model::Frame* fr = frame(s_.session.current);
    if (fr == nullptr || !roll_) {
        return;
    }
    pipeline::RenderRequest r;
    r.frame = fr->id;
    r.path = fr->path;
    model::EditList list = l;
    if (canvas_->tool() == cropTool_.get() || canvas_->tool() == baseTool_.get() ||
        canvas_->tool() == areaTool_.get()) {
        // Rahmen bzw. Messrechteck auf dem ganzen, ungeschnittenen Bild
        list = list.without(model::StepId(std::string(id::crop)))
                   .without(model::StepId(std::string(id::rotation)));
    }
    if (compare_) {
        r.stopAfter = model::StepId(std::string(id::retouch)); // Vorher: das Negativ (LF-KON-08)
    }
    r.list = std::make_shared<const model::EditList>(std::move(list));
    r.film = roll_->filmType;
    r.profile.mode = pipeline::RenderMode::Preview;
    r.profile.useCache = true;
    r.profile.priority = ports::Priority::Interactive;
    r.profile.output.transform = display_;
    const double dpr = canvas_->devicePixelRatioF();
    const double scale = canvas_->scale();
    if (canvas_->zoom() == ImageCanvas::Zoom::Fit || scale < 1.0) {
        // Die Vorschau verkleinert den ganzen Scan auf das Ziel und schneidet danach zu: mit
        // Zuschnitt entsprechend größer anfordern, damit der Ausschnitt das Fenster füllt bzw.
        // (unter 100 %) die Größe des gezoomten Bilds hat
        double fw = 1.0;
        double fh = 1.0;
        if (const model::EditEntry* e = r.list->get(model::StepId(std::string(id::crop))); e && e->enabled) {
            const auto c = e->params.get<model::NormRect>(key::rect).value_or(model::NormRect{0, 0, 1, 1});
            fw = std::clamp(c.w, 0.05, 1.0);
            fh = std::clamp(c.h, 0.05, 1.0);
        }
        const QSizeF want = canvas_->zoom() == ImageCanvas::Zoom::Fit
                                ? QSizeF(canvas_->width() * dpr, canvas_->height() * dpr)
                                : QSizeF(canvas_->sourceSize()) * scale;
        r.profile.target = {std::max(1, static_cast<int>(want.width() / fw)),
                            std::max(1, static_cast<int>(want.height() / fh))};
        detail_.reset();
    } else {
        // ab 100 %: volle Auflösung, nur der sichtbare Ausschnitt; vergrößert wird in der Brücke
        s_.bridge.setDetailScale(scale);
        const QSize full = canvas_->sourceSize();
        const QRectF vis = canvas_->visibleNorm();
        const int x0 =
            std::clamp(static_cast<int>(std::floor(vis.left() * full.width())), 0, full.width() - 1);
        const int y0 =
            std::clamp(static_cast<int>(std::floor(vis.top() * full.height())), 0, full.height() - 1);
        const int x1 =
            std::clamp(static_cast<int>(std::ceil(vis.right() * full.width())), x0 + 1, full.width());
        const int y1 =
            std::clamp(static_cast<int>(std::ceil(vis.bottom() * full.height())), y0 + 1, full.height());
        r.profile.mode = pipeline::RenderMode::Detail;
        r.profile.roi = image::Roi{x0, y0, x1 - x0, y1 - y0};
        detail_ =
            QRectF(static_cast<double>(x0) / full.width(), static_cast<double>(y0) / full.height(),
                   static_cast<double>(x1 - x0) / full.width(), static_cast<double>(y1 - y0) / full.height());
    }
    lastSeq_ = s_.render.request(std::move(r));
}

void MainWindow::onPreview(const PreviewFrame& p)
{
    if (p.frame != s_.session.current || p.seq < lastSeq_) {
        return; // veraltet: die neueste Anfrage gewinnt
    }
    if (!sources_.contains(p.frame) && canvas_->zoom() == ImageCanvas::Zoom::Fit) {
        canvas_->setSourceSize(p.image.size()); // bis die Kopfdaten da sind
    }
    // Seitenverhältnis der Vorschau gilt (Feindrehung mit Füllen, Vierteldrehung): sonst wird das
    // Bild verschoben bzw. verzerrt gezeichnet
    if (!p.image.isNull() && p.image.height() > 0) {
        const QSize src = canvas_->sourceSize();
        const double aspect = static_cast<double>(p.image.width()) / p.image.height();
        if (src.height() > 0 && std::abs(static_cast<double>(src.width()) / src.height() - aspect) > 0.005) {
            canvas_->setSourceSize(
                QSize(src.width(), std::max(1, static_cast<int>(std::lround(src.width() / aspect)))));
        }
    }
    // Bild mittig im Feld (falls die Vorschau kleiner ist, etwa am Rand der Quellauflösung);
    // Detailansicht: an der Stelle des angeforderten Ausschnitts
    QRectF place = canvas_->mapping().imageRect;
    QImage image = p.image;
    if (detail_ && canvas_->zoom() != ImageCanvas::Zoom::Fit) {
        place = QRectF(place.x() + detail_->x() * place.width(), place.y() + detail_->y() * place.height(),
                       detail_->width() * place.width(), detail_->height() * place.height());
    } else if (canvas_->zoom() == ImageCanvas::Zoom::Fit && !p.image.isNull()) {
        // Einpassen: das Feld füllen, auch wenn die Vorschau kleiner ist (kleine Quelle, starker
        // Zuschnitt); sonst passen Rahmen und Messrechteck der Werkzeuge nicht zum Bild
        const QSizeF fitted = QSizeF(p.image.size()).scaled(place.size(), Qt::KeepAspectRatio);
        place = QRectF(place.center().x() - fitted.width() / 2.0, place.center().y() - fitted.height() / 2.0,
                       fitted.width(), fitted.height());
        const double dpr = canvas_->devicePixelRatioF();
        const QSize pixels(static_cast<int>(std::lround(fitted.width() * dpr)),
                           static_cast<int>(std::lround(fitted.height() * dpr)));
        if (p.image.width() < pixels.width()) {
            image = p.image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    } else {
        const double dpr = canvas_->devicePixelRatioF();
        const QSizeF logical(p.image.width() / dpr, p.image.height() / dpr);
        place = QRectF(place.center().x() - logical.width() / 2.0,
                       place.center().y() - logical.height() / 2.0, logical.width(), logical.height());
    }
    canvas_->setImage(image, place);
    shown_ = image;
    canvas_->setClipping(p.clipping, false);
    histogram_->setHistogram(p.histogram);
    ++previewsShown_;
    Q_EMIT previewShown(p.seq);
}

bool MainWindow::applyEditPreset(const QString& name)
{
    if (!roll_ || !s_.decodeEdits) {
        return false;
    }
    const auto j = s_.presets.load({ports::PresetKind::Edit, name.toStdString()});
    if (!j) {
        report(QString::fromStdString(j.error().message));
        return false;
    }
    const application::ApplyPresetCommand cmd(name.toStdString(), s_.decodeEdits(*j));
    s_.bus.execute(cmd, targets(), *roll_);
    refreshCurrent();
    return true;
}

void MainWindow::rate(model::Rating r)
{
    for (const model::FrameId& f : targets()) {
        if (s_.bus.setRating(f, r)) {
            stripModel_->setRating(f, r);
            for (model::Frame& fr : frames_) {
                if (fr.id == f) {
                    fr.rating = r;
                }
            }
        }
    }
    updateRatingButtons();
}

void MainWindow::setStars(int stars)
{
    for (const model::FrameId& f : targets()) {
        if (s_.bus.setStars(f, stars)) {
            stripModel_->setStars(f, stars);
            for (model::Frame& fr : frames_) {
                if (fr.id == f) {
                    fr.stars = stars;
                }
            }
        }
    }
    updateRatingButtons();
}

void MainWindow::updateRatingButtons()
{
    const model::Frame* f = frame(s_.session.current);
    const int stars = f != nullptr ? f->stars : 0;
    for (std::size_t i = 0; i < starButtons_.size(); ++i) {
        starButtons_[i]->setChecked(static_cast<int>(i) < stars);
    }
    rejectButton_->setChecked(f != nullptr && f->rating == model::Rating::Reject);
}

void MainWindow::copyOptions()
{
    if (s_.session.current == model::FrameId{}) {
        return;
    }
    const model::EditList cur = ui_->currentList();
    model::EditList chosen;
    for (const QModelIndex& i : editView_->selectionModel()->selectedRows()) {
        if (const model::EditEntry* e = editModel_->entryAt(i.row())) {
            chosen = chosen.with(*e);
        }
    }
    const bool all = chosen.empty();
    s_.clipboard.copy(all ? cur : chosen, s_.session.current);
    copiedFrom_ = s_.session.current;
    report(all ? tr("Alle Bearbeitungsoptionen kopiert")
               : tr("%n Bearbeitungsoption(en) kopiert", nullptr, static_cast<int>(chosen.size())));
}

void MainWindow::applyCopiedTo(std::vector<model::FrameId> frames, bool batch)
{
    if (!roll_) {
        return;
    }
    if (s_.clipboard.empty()) {
        report(tr("Zuerst Bearbeitungsoptionen kopieren (Liste: „Ausgewählte kopieren“ oder Strg+C)."));
        return;
    }
    if (copiedFrom_) {
        std::erase(frames, *copiedFrom_);
    }
    if (frames.empty()) {
        report(tr("Keine anderen Bilder ausgewählt."));
        return;
    }
    // Automatische Optionen gehören zum einzelnen Bild: dort neu berechnen statt kopieren
    std::set<model::StepId> values;
    std::set<model::StepId> automatics;
    for (const model::EditEntry& e : s_.clipboard.source().entries()) {
        const std::string& step = e.step.value();
        if (e.origin == model::Origin::Automatic &&
            (step == id::crop || step == id::rotation || step == id::filmbase || step == id::densityrange)) {
            automatics.insert(e.step);
        } else {
            values.insert(e.step);
        }
    }
    if (!values.empty()) {
        s_.bus.execute(s_.clipboard.paste(values, application::PasteMode::Values), frames, *roll_, batch);
    }
    if (!automatics.empty()) {
        redetect(frames, automatics);
    }
    refreshCurrent();
    report(tr("Kopierte Bearbeitungsoptionen auf %n Bild(er) übernommen", nullptr,
              static_cast<int>(frames.size())));
}

bool MainWindow::autoConverted(const model::FrameId& f) const
{
    if (f == model::FrameId{} || !s_.bus.isOpen(f) || manualConversion_.contains(f)) {
        return false;
    }
    const model::EditList& l = s_.bus.history(f).current();
    // Nur die Autokonvertierung markiert ihren Eintrag; dieselben Schritte von Hand bleiben manuell
    const model::EditEntry* e = l.get(model::StepId(std::string(id::convert)));
    return e != nullptr && e->origin == model::Origin::Automatic &&
           e->params.get<bool>(key::automatic).value_or(false);
}

void MainWindow::setAutoConvert(bool on)
{
    const auto t = targets();
    for (const model::FrameId& f : t) {
        if (on) {
            manualConversion_.erase(f);
        } else {
            manualConversion_.insert(f);
        }
    }
    if (on) {
        detect(t, tr("Autokonvertierung"), automatic(&application::AnalysisService::autoConvert));
    }
}

void MainWindow::removeSteps(std::initializer_list<std::string_view> steps)
{
    class RemoveSteps final : public application::ICommand
    {
    public:
        explicit RemoveSteps(std::vector<model::StepId> s)
            : steps_(std::move(s))
        {}
        [[nodiscard]] std::string label() const override { return "Entfernen"; }
        [[nodiscard]] model::EditList apply(const model::EditList& l,
                                            const application::CommandContext& /*c*/) const override
        {
            model::EditList out = l;
            for (const model::StepId& st : steps_) {
                out = out.without(st);
            }
            return out;
        }

    private:
        std::vector<model::StepId> steps_;
    };
    std::vector<model::StepId> ids;
    for (const std::string_view st : steps) {
        ids.emplace_back(std::string(st));
    }
    if (roll_) {
        s_.bus.execute(RemoveSteps(std::move(ids)), targets(), *roll_);
    }
    refreshCurrent();
}

void MainWindow::arrangeWindows()
{
    // Standardanordnung: alle Fenster angedockt und sichtbar (in Entwickeln)
    restoreState(defaultLayout_);
    for (QDockWidget* d : editorDocks_) {
        d->setFloating(false);
        d->setVisible(view_ == View::Image);
    }
}

void MainWindow::pickWhite(QPointF norm)
{
    if (shown_.isNull() || !roll_) {
        return;
    }
    // Detailansicht: die Vorschau zeigt nur den Ausschnitt
    if (detail_ && canvas_->zoom() != ImageCanvas::Zoom::Fit) {
        norm = QPointF((norm.x() - detail_->x()) / detail_->width(),
                       (norm.y() - detail_->y()) / detail_->height());
    }
    // Mittel über 5 × 5 Pixel der Vorschau, sRGB zurück nach linear
    const int cx = std::clamp(static_cast<int>(norm.x() * shown_.width()), 2, shown_.width() - 3);
    const int cy = std::clamp(static_cast<int>(norm.y() * shown_.height()), 2, shown_.height() - 3);
    const auto linear = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    base::Vec3 sum{0, 0, 0};
    for (int y = cy - 2; y <= cy + 2; ++y) {
        for (int x = cx - 2; x <= cx + 2; ++x) {
            const QColor c = shown_.pixelColor(x, y);
            sum = {sum.x + linear(c.red()), sum.y + linear(c.green()), sum.z + linear(c.blue())};
        }
    }
    // Die Vorschau zeigt das Bild mit dem jetzigen Weißabgleich: ihn herausrechnen, dann für die
    // Farbe davor neu bestimmen (ein zweiter Klick an derselben Stelle ändert nichts mehr)
    const model::EditList cur = ui_->currentList();
    double t = 0.0;
    double u = 0.0;
    if (const model::EditEntry* old = cur.get(model::StepId(std::string(id::whitebalance)))) {
        t = old->params.get<double>(key::temperature).value_or(0.0) / 100.0;
        u = old->params.get<double>(key::tint).value_or(0.0) / 100.0;
    }
    // Vorschau in sRGB, gerechnet wird in Rec. 2020 (linear)
    const base::Vec3 w{0.6274 * sum.x + 0.3293 * sum.y + 0.0433 * sum.z,
                       0.0691 * sum.x + 0.9195 * sum.y + 0.0114 * sum.z,
                       0.0164 * sum.x + 0.0880 * sum.y + 0.8956 * sum.z};
    const base::Vec3 before{w.x / std::exp2(t), w.y / std::exp2(-u), w.z / std::exp2(-t)};
    const auto [nt, nu] = steps::WhiteBalanceStep::fromNeutral(before);
    model::EditEntry e = entryWith(cur, id::whitebalance, key::temperature, std::clamp(nt, -300.0, 300.0));
    e.params = e.params.with(key::tint, std::clamp(nu, -300.0, 300.0));
    ui_->apply(std::make_unique<application::SetEntryCommand>(e));
    refreshCurrent();
}

void MainWindow::applyFrameHint()
{
    analysis::FrameHint h;
    const auto text = [this](const char* key) {
        const auto v = s_.settings.get(key);
        return v && v->asString() != nullptr ? *v->asString() : std::string();
    };
    // Format: Länge entlang des Films / Breite quer
    static const std::map<std::string, std::pair<double, double>> formats{
        {"24x36", {36.0 / 24.0, 24}}, {"18x24", {18.0 / 24.0, 24}}, {"24x65", {65.0 / 24.0, 24}},
        {"6x4.5", {41.5 / 56.0, 56}}, {"6x6", {1.0, 56}},           {"6x7", {70.0 / 56.0, 56}},
        {"6x9", {84.0 / 56.0, 56}}};
    if (const auto it = formats.find(text("frameFormat")); it != formats.end()) {
        h.aspect = it->second.first;
        h.acrossMm = it->second.second;
    }
    const std::string o = text("frameOrientation");
    h.orientation = o == "landscape"  ? analysis::FrameHint::Orientation::Landscape
                    : o == "portrait" ? analysis::FrameHint::Orientation::Portrait
                                      : analysis::FrameHint::Orientation::Any;
    if (const auto v = s_.settings.get("maxSkewDeg"); v && v->number()) {
        h.maxSkewDeg = *v->number();
    }
    s_.analysis.setFrameHint(h);
}

void MainWindow::doFrameOptions()
{
    QDialog d(this);
    d.setWindowTitle(tr("Optionen der Bildfelderkennung"));
    auto* layout = new QVBoxLayout(&d);
    layout->addWidget(
        new QLabel(tr("Hinweise für die Erkennung.") + QLatin1Char(' ') +
                       tr("Weicht das Gefundene stark ab, sucht AutoNeg mit diesem Format weiter."),
                   &d));
    auto* format = new QComboBox(&d);
    format->setObjectName(QStringLiteral("frameFormat"));
    for (const auto& [key, name] :
         std::vector<std::pair<QString, QString>>{{QString(), tr("Automatisch")},
                                                  {QStringLiteral("24x36"), tr("Kleinbild 24×36")},
                                                  {QStringLiteral("18x24"), tr("Halbformat 18×24")},
                                                  {QStringLiteral("24x65"), tr("Panorama 24×65")},
                                                  {QStringLiteral("6x4.5"), tr("Mittelformat 6×4,5")},
                                                  {QStringLiteral("6x6"), tr("Mittelformat 6×6")},
                                                  {QStringLiteral("6x7"), tr("Mittelformat 6×7")},
                                                  {QStringLiteral("6x9"), tr("Mittelformat 6×9")}}) {
        format->addItem(name, key);
    }
    auto* orientation = new QComboBox(&d);
    orientation->setObjectName(QStringLiteral("frameOrientation"));
    orientation->addItem(tr("Lage egal"), QString());
    orientation->addItem(tr("Bild quer im Scan"), QStringLiteral("landscape"));
    orientation->addItem(tr("Bild hochkant im Scan"), QStringLiteral("portrait"));
    const auto current = [this](const char* key) {
        const auto v = s_.settings.get(key);
        return v && v->asString() != nullptr ? QString::fromStdString(*v->asString()) : QString();
    };
    format->setCurrentIndex(std::max(0, format->findData(current("frameFormat"))));
    orientation->setCurrentIndex(std::max(0, orientation->findData(current("frameOrientation"))));
    layout->addWidget(new QLabel(tr("Bevorzugtes Bildformat"), &d));
    layout->addWidget(format);
    layout->addWidget(new QLabel(tr("Lage"), &d));
    layout->addWidget(orientation);
    auto* skew = new QDoubleSpinBox(&d);
    skew->setObjectName(QStringLiteral("maxSkew"));
    skew->setRange(1.0, 30.0);
    skew->setDecimals(1);
    skew->setSuffix(QStringLiteral(" °"));
    skew->setToolTip(tr("So schief darf der Film unter der Kamera liegen; Standard 10°"));
    {
        const auto v = s_.settings.get("maxSkewDeg");
        skew->setValue(v && v->number() ? *v->number() : 10.0);
    }
    layout->addWidget(new QLabel(tr("Maximale Schräglage"), &d));
    layout->addWidget(skew);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    s_.settings.set("frameFormat", model::JsonValue(format->currentData().toString().toStdString()));
    s_.settings.set("frameOrientation",
                    model::JsonValue(orientation->currentData().toString().toStdString()));
    s_.settings.set("maxSkewDeg", model::JsonValue(skew->value()));
    applyFrameHint();
}

void MainWindow::refreshFilmProfiles()
{
    QStringList names;
    for (const ports::PresetInfo& p : s_.presets.list(ports::PresetKind::Base)) {
        names << QString::fromStdString(p.id.name);
    }
    conversion_->setFilmProfiles(names, filmProfileKey_);
    filmProfile_->setFilmProfiles(names, filmProfileKey_);
}

void MainWindow::chooseFilmProfile(const QString& key, bool apply)
{
    using Profile = application::AnalysisService::FilmProfile;
    Profile p;
    if (key == FilmProfilePanel::kUnprocessed) {
        p.kind = Profile::Kind::Unprocessed;
        p.name = tr("Unbearbeitet").toStdString();
    } else if (key == FilmProfilePanel::kNeutral || !s_.decodeEdits) {
        p.kind = Profile::Kind::Neutral;
        p.name = tr("Neutral").toStdString();
    } else {
        const auto j = s_.presets.load({ports::PresetKind::Base, key.toStdString()});
        const model::EditList l = j ? s_.decodeEdits(*j) : model::EditList{};
        const model::EditEntry* e = l.get(model::StepId(std::string(id::densityrange)));
        if (e == nullptr) {
            report(tr("Film-Farbprofil „%1“ nicht lesbar, Neutral wird verwendet.").arg(key));
            chooseFilmProfile(FilmProfilePanel::kNeutral, apply);
            return;
        }
        p = Profile::fromEntry(*e, key.toStdString());
    }
    filmProfileKey_ = key;
    s_.analysis.setFilmProfile(p);
    s_.settings.set("filmProfile", model::JsonValue(key.toStdString()));
    refreshFilmProfiles();
    if (!apply || !roll_) {
        return;
    }
    // Tonwerte der markierten Bilder mit dem Profil neu setzen (auch von Hand geänderte)
    removeSteps({id::densityrange});
    detect(targets(), tr("Film-Farbprofil"), automatic(&application::AnalysisService::autoLevels));
}

void MainWindow::saveFilmProfile()
{
    const model::EditEntry* e = ui_->currentList().get(model::StepId(std::string(id::densityrange)));
    if (e == nullptr || !s_.encodeEdits) {
        report(tr("Das Bild ist noch nicht konvertiert: es gibt keine Werte zum Speichern."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Film-Farbprofil speichern"), tr("Name, z. B. der Film (Portra 160)"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (name.startsWith(QLatin1Char(':'))) {
        report(tr("Der Name darf nicht mit einem Doppelpunkt beginnen."));
        return;
    }
    model::EditEntry copy = *e;
    copy.params = copy.params.with(key::profile, name.toStdString());
    if (auto r = s_.presets.save(ports::PresetKind::Base, name.toStdString(),
                                 s_.encodeEdits(model::EditList{}.with(copy)));
        !r) {
        report(QString::fromStdString(r.error().message));
        return;
    }
    ui_->apply(std::make_unique<application::SetEntryCommand>(copy)); // Bild trägt jetzt den Profilnamen
    chooseFilmProfile(name, false);
    report(tr("Film-Farbprofil „%1“ gespeichert").arg(name));
}

void MainWindow::excludeFrames(const std::vector<model::FrameId>& frames)
{
    if (!roll_ || frames.empty()) {
        return;
    }
    model::Roll r = *roll_;
    for (const model::FrameId& id : frames) {
        const auto it = std::ranges::find(frames_, id, &model::Frame::id);
        if (it == frames_.end()) {
            continue;
        }
        r.excluded.push_back(it->path.filename().string());
        std::erase(r.frames, id);
        s_.bus.close(id);
        frames_.erase(it);
    }
    roll_ = std::make_shared<const model::Roll>(std::move(r)); // gespeichert mit dem Filmtyp
    s_.session.roll = roll_;
    s_.session.current = {};
    s_.session.selection.clear();
    stripModel_->setFrames(frames_);
    if (!frames_.empty()) {
        selectFrame(frames_.front().id);
    }
    report(tr("%n Bild(er) nicht importiert", nullptr, static_cast<int>(frames.size())));
}

void MainWindow::updateExportPanel()
{
    if (!roll_) {
        return;
    }
    if (exportSettings_.folder.empty()) {
        exportSettings_.folder = roll_->folder / "export";
    }
    const QString folder = qs(exportSettings_.folder);
    const QFontMetrics fm = exportFolder_->fontMetrics();
    const int width =
        std::max(exportFolder_->width(), 200) - fm.horizontalAdvance(tr("Ziel: %1").arg(QString()));
    exportFolder_->setText(tr("Ziel: %1").arg(fm.elidedText(folder, Qt::ElideLeft, width)));
    exportFolder_->setToolTip(folder);
    const int format = exportSettings_.write.format == ports::FileFormat::Jpeg  ? 0
                       : exportSettings_.write.format == ports::FileFormat::Png ? 2
                                                                                : 1;
    exportFormat_->setCurrentIndex(format);
    selectionInfo_->setText(
        tr("%1 von %2 Bildern ausgewählt").arg(filmstrip_->selectedFrames().size()).arg(frames_.size()));
}

void MainWindow::trackJob(application::JobHandle h, const QString& what)
{
    running_.push_back(h);
    progress_->show();
    cancel_->show();
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, h, what] {
        progress_->setValue(static_cast<int>(h.progress() * 1000.0));
        progress_->setFormat(what + QStringLiteral(" %p %"));
        // so breit wie der Text bei 100 % (plus Rand), nie abgeschnitten
        progress_->setMinimumWidth(progress_->fontMetrics().horizontalAdvance(what + QStringLiteral(" 100 %")) +
                                   32);
        if (!h.done()) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        for (auto& [button, jobs] : busy_) {
            std::erase_if(jobs, [](const application::JobHandle& j) { return j.done(); });
            conversion_->setBusy(button, !jobs.empty());
        }
        refreshCurrent(); // Schalter zeigen, was tatsächlich eingetragen ist
        std::erase_if(running_, [](const application::JobHandle& j) { return j.done(); });
        if (std::ranges::none_of(running_, [](const application::JobHandle& j) { return !j.done(); })) {
            progress_->hide();
            cancel_->hide();
        }
        const auto r = h.wait();
        if (!r && r.error().code != base::ErrorCode::Cancelled) {
            report(what + QStringLiteral(": ") + QString::fromStdString(r.error().message));
        } else if (!r) {
            report(what + tr(": abgebrochen"));
        }
        Q_EMIT jobDone(r.has_value());
    });
    timer->start();
}

void MainWindow::doExport()
{
    if (!roll_) {
        return;
    }
    ExportDialog d(this);
    d.setPresets(&s_.presets);
    if (exportSettings_.folder.empty()) {
        exportSettings_.folder = roll_->folder / "export";
    }
    d.setSettings(exportSettings_);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    exportSettings_ = d.settings();
    exportFrames(targets());
}

void MainWindow::exportFrames(const std::vector<model::FrameId>& frames)
{
    if (!roll_ || frames.empty()) {
        report(tr("Keine Bilder zum Exportieren ausgewählt."));
        return;
    }
    if (exportSettings_.folder.empty()) {
        exportSettings_.folder = roll_->folder / "export";
    }
    std::error_code ec;
    std::filesystem::create_directories(exportSettings_.folder, ec);
    std::vector<application::ExportService::Item> items;
    for (const model::FrameId& f : frames) {
        if (const model::Frame* fr = frame(f)) {
            items.push_back({*fr, s_.bus.history(f).current()});
        }
    }
    auto h = s_.exports.exportFrames(items, roll_, exportSettings_);
    trackJob(h, tr("Export"));
}

std::vector<application::ContactSheetService::Item> MainWindow::sheetItems() const
{
    std::vector<application::ContactSheetService::Item> out;
    for (const model::Frame& f : frames_) {
        application::ContactSheetService::Item i;
        i.source = f;
        i.edits = s_.bus.history(f.id).current();
        double aspect = 1.5;
        if (const auto it = sources_.find(f.id); it != sources_.end() && it->second.height > 0) {
            aspect = static_cast<double>(it->second.width) / it->second.height;
        }
        i.frame = {f.id, f.frameNo, aspect};
        out.push_back(std::move(i));
    }
    return out;
}

void MainWindow::contactSheetPdf()
{
    if (!roll_) {
        return;
    }
    const QString p = QFileDialog::getSaveFileName(this, tr("Kontaktabzug speichern"),
                                                   qs(roll_->folder / "kontaktabzug.pdf"), tr("PDF (*.pdf)"));
    if (p.isEmpty()) {
        return;
    }
    sheetSpec_.header.rollNo = sheetSpec_.header.rollNo.empty() ? roll_->name : sheetSpec_.header.rollNo;
    sheetSpec_.header.film = roll_->film;
    const layout::FilmFormat* fmt = layout::FilmFormat::find(sheetFormat_);
    trackJob(s_.contactSheet.exportPdf(sheetItems(), roll_, sheetSpec_,
                                       fmt ? *fmt : layout::FilmFormat::catalog()[0],
                                       std::filesystem::path(p.toStdU16String())),
             tr("Kontaktabzug"));
}

void MainWindow::doContactSheet()
{
    if (!roll_) {
        return;
    }
    ContactSheetDialog d(this);
    d.setPresets(&s_.presets);
    sheetSpec_.header.rollNo = sheetSpec_.header.rollNo.empty() ? roll_->name : sheetSpec_.header.rollNo;
    sheetSpec_.header.film = roll_->film;
    d.setSpec(sheetSpec_, sheetFormat_);
    const auto items = [this] { return sheetItems(); };
    connect(&d, &ContactSheetDialog::exportPdfRequested, this, [this, &d, items] {
        sheetSpec_ = d.spec();
        sheetFormat_ = d.format();
        const QString p = QFileDialog::getSaveFileName(
            this, tr("Kontaktabzug speichern"), qs(roll_->folder / "kontaktabzug.pdf"), tr("PDF (*.pdf)"));
        if (p.isEmpty()) {
            return;
        }
        const layout::FilmFormat* fmt = layout::FilmFormat::find(sheetFormat_);
        trackJob(s_.contactSheet.exportPdf(items(), roll_, sheetSpec_,
                                           fmt ? *fmt : layout::FilmFormat::catalog()[0],
                                           std::filesystem::path(p.toStdU16String())),
                 tr("Kontaktabzug"));
    });
    connect(&d, &ContactSheetDialog::printRequested, this, [this, &d, items] {
        sheetSpec_ = d.spec();
        sheetFormat_ = d.format();
        const layout::FilmFormat* fmt = layout::FilmFormat::find(sheetFormat_);
        trackJob(s_.contactSheet.print(items(), roll_, sheetSpec_,
                                       fmt ? *fmt : layout::FilmFormat::catalog()[0], {}),
                 tr("Drucken"));
    });
    d.exec();
    sheetSpec_ = d.spec();
    sheetFormat_ = d.format();
}

void MainWindow::doSettings()
{
    // Einstellungen bleiben über Neustarts erhalten (LF-DAT-04); Threads wirken sofort,
    // die Sprache nach einem Neustart
    const auto num = [this](const char* key, int fallback) {
        const auto v = s_.settings.get(key);
        const auto n = v ? v->number() : std::nullopt;
        return n ? static_cast<int>(*n) : fallback;
    };
    SettingsDialog::Values before;
    if (const auto v = s_.settings.get("language"); v && v->asString() != nullptr) {
        before.language = *v->asString();
    }
    before.threads = num("threads", 0);
    before.memoryMb = num("memoryBudgetMb", 0); // Standard: unbegrenzt
    before.ramMb = static_cast<int>(s_.physicalMemory >> 20U);
    before.panButton = num("panButton", static_cast<int>(Qt::LeftButton));
    for (const QAction* menu : menuBar()->actions()) {
        for (const QAction* a : menu->menu()->actions()) {
            if (actions_.contains(a->objectName())) {
                before.shortcuts.push_back({a->objectName(), a->text(), a->shortcut(),
                                            a->property("standardShortcut").value<QKeySequence>()});
            }
        }
    }
    SettingsDialog d(this);
    d.setValues(before);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    const SettingsDialog::Values v = d.values();
    s_.settings.set("language", model::JsonValue(v.language));
    s_.settings.set("threads", model::JsonValue(std::int64_t{v.threads}));
    s_.settings.set("memoryBudgetMb", model::JsonValue(std::int64_t{v.memoryMb}));
    s_.settings.set("panButton", model::JsonValue(std::int64_t{v.panButton}));
    canvas_->setPanButton(static_cast<Qt::MouseButton>(v.panButton));
    // nur Abweichungen vom Standard speichern; leer = ohne Kürzel
    model::JsonValue keys = model::JsonValue::object({});
    for (const SettingsDialog::Shortcut& sc : v.shortcuts) {
        if (sc.key != sc.standard) {
            keys.set(sc.action.toStdString(),
                     model::JsonValue(sc.key.toString(QKeySequence::PortableText).toStdString()));
        }
    }
    s_.settings.set("shortcuts", keys);
    applyShortcuts();
    application::ThreadConfig c;
    c.workers = v.threads;
    c.memoryBudget =
        v.memoryMb > 0
            ? static_cast<std::size_t>(v.memoryMb) << 20U
            : static_cast<std::size_t>(s_.physicalMemory > 0 ? s_.physicalMemory : std::uint64_t{8} << 30U);
    s_.jobs.configure(c);
    if (v.language != before.language) {
        report(tr("Die Sprache wird nach einem Neustart umgestellt."));
    }
}

void MainWindow::doPaste()
{
    if (!roll_ || s_.clipboard.empty()) {
        return;
    }
    const model::EditList source = s_.clipboard.source();
    PasteDialog d(source, s_.clipboard.defaultSelection(), s_.registry, this);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    const application::PasteCommand cmd = s_.clipboard.paste(d.selection(), d.mode());
    const auto t = targets();
    s_.bus.execute(cmd, t, *roll_, view_ == View::Overview); // Leuchttisch: dort rückgängig
    if (d.mode() == application::PasteMode::Redetect && !cmd.redetect().empty()) {
        redetect(t, cmd.redetect());
    }
    refreshCurrent();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    // Ordner oder Datei (dann ihr Ordner) öffnen (LF-IMP-08)
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) {
            continue;
        }
        std::filesystem::path p(u.toLocalFile().toStdU16String());
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) {
            p = p.parent_path();
        }
        openFolder(p);
        e->acceptProposedAction();
        return;
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    for (const application::JobHandle& h : running_) {
        h.cancel();
    }
    QMainWindow::closeEvent(e);
}

} // namespace autoneg::ui
