#include "autoneg/ui/main_window.hpp"

#include "autoneg/application/presets.hpp"
#include "autoneg/steps/step_ids.hpp"
#include "autoneg/ui/canvas_tools.hpp"
#include "autoneg/ui/dialogs.hpp"
#include "autoneg/ui/edit_list_model.hpp"
#include "autoneg/ui/image_canvas.hpp"
#include "autoneg/ui/panels.hpp"
#include "autoneg/ui/views.hpp"

#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QAction>
#include <QtGui/QCloseEvent>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace autoneg::ui {

namespace id = steps::id;

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

model::EditEntry entryWith(const model::EditList& l, std::string_view step, const std::string& name,
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
{
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
    buildUi();
    buildActions();
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

    cropTool_->setCommit([this](const model::NormRect& r, double angle) {
        const model::EditList cur = ui_->currentList();
        ui_->apply(std::make_unique<SetEntriesCommand>(std::vector<model::EditEntry>{
            entryWith(cur, id::crop, "rect", r), entryWith(cur, id::rotation, "angle", angle)}));
    });
    retouchTool_->setSpot([this](const model::Spot& spot) {
        const model::EditList cur = ui_->currentList();
        model::SpotList spots;
        if (const model::EditEntry* e = cur.get(model::StepId(std::string(id::retouch)))) {
            spots = e->params.get<model::SpotList>("spots").value_or(model::SpotList{});
        }
        spots.push_back(spot);
        ui_->apply(
            std::make_unique<application::SetEntryCommand>(entryWith(cur, id::retouch, "spots", spots)));
    });
    baseTool_->setPicked([this](const model::NormRect& r) {
        // Messrechteck im Filmrand (LF-BAS-04); die Fläche zeigt das ungeschnittene Bild
        if (const model::Frame* fr = frame(s_.session.current)) {
            trackJob(s_.analysis.measureBase(*fr, roll_, r), tr("Basis messen"));
        }
        setTool(0);
    });
}

MainWindow::~MainWindow()
{
    for (const application::JobHandle& h : running_) {
        h.cancel();
    }
    if (roll_) {
        s_.rolls.close(roll_->folder);
    }
}

void MainWindow::buildUi()
{
    canvas_ = new ImageCanvas(this);
    canvas_->setObjectName(QStringLiteral("canvas"));
    setCentralWidget(canvas_);

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
    stripLayout->addWidget(filter_);
    stripLayout->addWidget(filmstrip_);
    stripDock->setWidget(stripBox);
    addDockWidget(Qt::BottomDockWidgetArea, stripDock);
    connect(filmstrip_, &FilmstripView::currentFrameChanged, this, &MainWindow::selectFrame);
    connect(filmstrip_, &FilmstripView::selectionChangedFrames, this,
            [this] { s_.session.selection = filmstrip_->selectedFrames(); });
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
    adjustScroll->setWidget(adjust_);
    adjustScroll->setWidgetResizable(true);
    tools_->addTab(adjustScroll, tr("Anpassen"));
    auto* toolDock = new QDockWidget(tr("Werkzeuge"), this);
    toolDock->setObjectName(QStringLiteral("toolsDock"));
    toolDock->setWidget(tools_);
    addDockWidget(Qt::RightDockWidgetArea, toolDock);

    histogram_ = new HistogramWidget(this);
    histogram_->setObjectName(QStringLiteral("histogram"));
    auto* histDock = new QDockWidget(tr("Histogramm"), this);
    histDock->setObjectName(QStringLiteral("histogramDock"));
    histDock->setWidget(histogram_);
    addDockWidget(Qt::RightDockWidgetArea, histDock);

    editModel_ = new EditListModel(*ui_, this);
    editView_ = new QTableView(this);
    editView_->setObjectName(QStringLiteral("editList"));
    editView_->setModel(editModel_);
    editView_->horizontalHeader()->setStretchLastSection(true);
    editView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    editView_->verticalHeader()->hide();
    auto* listBox = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listBox);
    listLayout->setContentsMargins(2, 2, 2, 2);
    listLayout->addWidget(editView_);
    auto* remove = new QPushButton(tr("Eintrag entfernen"), listBox);
    remove->setObjectName(QStringLiteral("removeEntry"));
    remove->setToolTip(tr("Ausgewählte Änderung mit einem Klick entfernen (LF-LST-03)"));
    listLayout->addWidget(remove);
    auto* listDock = new QDockWidget(tr("Bearbeitungsliste"), this);
    listDock->setObjectName(QStringLiteral("editListDock"));
    listDock->setWidget(listBox);
    addDockWidget(Qt::RightDockWidgetArea, listDock);
    connect(remove, &QPushButton::clicked, this, [this] {
        const QModelIndex i = editView_->currentIndex();
        if (i.isValid()) {
            editModel_->removeEntry(i.row());
        }
    });

    connect(conversion_, &ConversionPanel::autoFrameRequested, this, [this] {
        for (const model::FrameId& f : targets()) {
            if (const model::Frame* fr = frame(f)) {
                trackJob(s_.analysis.detectFrame(*fr, roll_), tr("Bildfeld erkennen"));
            }
        }
    });
    connect(conversion_, &ConversionPanel::autoBaseRequested, this, [this] {
        for (const model::FrameId& f : targets()) {
            if (const model::Frame* fr = frame(f)) {
                trackJob(s_.analysis.detectFrame(*fr, roll_), tr("Basis messen"));
            }
        }
    });
    connect(conversion_, &ConversionPanel::autoLevelsRequested, this,
            [this] { report(tr("Auto-Tonwerte: die Tonwertgrenzen setzt die Konvertierung selbst.")); });
    connect(conversion_, &ConversionPanel::pickBaseRequested, this, [this] { setTool(2); });

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("jobProgress"));
    progress_->setRange(0, 1000);
    progress_->setMaximumWidth(220);
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
        const QString d = QFileDialog::getExistingDirectory(this, tr("Scan-Ordner öffnen"));
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
    add(edit, QStringLiteral("undo"), tr("Rückgängig"), QKeySequence(Qt::CTRL | Qt::Key_Z),
        [this] { ui_->undo(); });
    add(edit, QStringLiteral("redo"), tr("Wiederholen"), QKeySequence(Qt::CTRL | Qt::Key_Y),
        [this] { ui_->redo(); });
    edit->addSeparator();
    add(edit, QStringLiteral("copy"), tr("Bearbeitung kopieren"), QKeySequence::Copy, [this] {
        if (s_.session.current != model::FrameId{}) {
            s_.clipboard.copy(ui_->currentList(), s_.session.current);
            report(tr("Bearbeitung kopiert"));
        }
    });
    add(edit, QStringLiteral("paste"), tr("Einfügen …"), QKeySequence::Paste, [this] { doPaste(); });
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

    QMenu* view = menuBar()->addMenu(tr("&Ansicht"));
    add(view, QStringLiteral("zoomFit"), tr("Einpassen"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] {
        s_.bridge.setUpscale(1);
        canvas_->setZoom(ImageCanvas::Zoom::Fit);
    });
    add(view, QStringLiteral("zoom100"), tr("100 %"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] {
        s_.bridge.setUpscale(1);
        canvas_->setZoom(ImageCanvas::Zoom::Actual);
    });
    add(view, QStringLiteral("zoom200"), tr("200 %"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] {
        s_.bridge.setUpscale(2);
        canvas_->setZoom(ImageCanvas::Zoom::Double);
    });
    auto* compare = add(view, QStringLiteral("compare"), tr("Vorher/Nachher"),
                        QKeySequence(Qt::Key_Backslash), [this](bool on) {
                            compare_ = on;
                            requestPreview(ui_->currentList());
                        });
    compare->setCheckable(true);
    auto* clip = add(view, QStringLiteral("clipping"), tr("Beschnittwarnung"), QKeySequence(Qt::Key_J),
                     [this](bool on) {
                         clipping_ = on;
                         canvas_->setClipping({}, false);
                         requestPreview(ui_->currentList());
                     });
    clip->setCheckable(true);
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
            q = e->params.get<int>("quarterTurns").value_or(0);
            fh = e->params.get<bool>("flipH").value_or(false);
            fv = e->params.get<bool>("flipV").value_or(false);
        }
        q = ((q + turns) % 4 + 4) % 4;
        fh = flipH ? !fh : fh;
        model::EditEntry e = entryWith(cur, id::orientation, "quarterTurns", q);
        e.params = e.params.with("flipH", fh).with("flipV", fv);
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

    for (QAction* a : {action(QStringLiteral("openFolder")), action(QStringLiteral("undo")),
                       action(QStringLiteral("redo")), action(QStringLiteral("toolCrop")),
                       action(QStringLiteral("compare")), action(QStringLiteral("export"))}) {
        a->setToolTip(a->text() + QStringLiteral(" (") + a->shortcut().toString(QKeySequence::NativeText) +
                      QStringLiteral(")"));
    }
    QToolBar* bar = addToolBar(tr("Werkzeuge"));
    bar->setObjectName(QStringLiteral("toolbar"));
    for (const char* n : {"openFolder", "undo", "redo", "toolCrop", "toolRetouch", "compare", "clipping",
                          "export", "contactSheet"}) {
        bar->addAction(action(QString::fromLatin1(n)));
    }
}

void MainWindow::setTool(int which)
{
    action(QStringLiteral("toolCrop"))->setChecked(which == 1);
    action(QStringLiteral("toolRetouch"))->setChecked(which == 3);
    switch (which) {
    case 1: {
        const model::EditList l = ui_->currentList();
        model::NormRect r;
        double angle = 0.0;
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::crop)))) {
            r = e->params.get<model::NormRect>("rect").value_or(model::NormRect{});
        }
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::rotation)))) {
            angle = e->params.get<double>("angle").value_or(0.0);
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
    case 3: {
        model::SpotList spots;
        if (const model::EditEntry* e = ui_->currentList().get(model::StepId(std::string(id::retouch)))) {
            spots = e->params.get<model::SpotList>("spots").value_or(model::SpotList{});
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

void MainWindow::refreshCurrent()
{
    const model::EditList l = ui_->currentList();
    editModel_->setList(l);
    conversion_->bindList(l);
    gradation_->bindList(l);
    adjust_->bindList(l);
    // Größe des Ergebnisses in Quellpixeln: Zuschnitt und Vierteldrehung berücksichtigt
    if (const auto it = sources_.find(s_.session.current); it != sources_.end() && it->second.width > 0) {
        double w = it->second.width;
        double h = it->second.height;
        if (canvas_->tool() != cropTool_.get()) {
            if (const model::EditEntry* e = l.get(model::StepId(std::string(id::crop))); e && e->enabled) {
                const auto r = e->params.get<model::NormRect>("rect").value_or(model::NormRect{});
                w *= r.w;
                h *= r.h;
            }
        }
        if (const model::EditEntry* e = l.get(model::StepId(std::string(id::orientation))); e && e->enabled) {
            if (e->params.get<int>("quarterTurns").value_or(0) % 2 == 1) {
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
    if (canvas_->tool() == cropTool_.get() || canvas_->tool() == baseTool_.get()) {
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
    const QRectF ir = canvas_->mapping().imageRect;
    if (canvas_->zoom() == ImageCanvas::Zoom::Fit) {
        r.profile.target = {std::max(1, static_cast<int>(canvas_->width() * dpr)),
                            std::max(1, static_cast<int>(canvas_->height() * dpr))};
    } else {
        // 100 %: so viele Pixel wie das Ergebnis hat (200 % vergrößert die Brücke)
        const int scale = canvas_->zoom() == ImageCanvas::Zoom::Double ? 2 : 1;
        r.profile.target = {std::max(1, static_cast<int>(ir.width() * dpr / scale)),
                            std::max(1, static_cast<int>(ir.height() * dpr / scale))};
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
    canvas_->setImage(p.image, canvas_->mapping().imageRect);
    canvas_->setClipping(p.clipping, clipping_);
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
        if (!h.done()) {
            return;
        }
        timer->stop();
        timer->deleteLater();
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
    std::error_code ec;
    std::filesystem::create_directories(exportSettings_.folder, ec);
    std::vector<application::ExportService::Item> items;
    for (const model::FrameId& f : targets()) {
        if (const model::Frame* fr = frame(f)) {
            items.push_back({*fr, s_.bus.history(f).current()});
        }
    }
    auto h = s_.exports.exportFrames(items, roll_, exportSettings_);
    trackJob(h, tr("Export"));
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
    const auto items = [this] {
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
    };
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
    before.memoryMb = num("memoryBudgetMb", 4096);
    SettingsDialog d(this);
    d.setValues(before);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    const SettingsDialog::Values v = d.values();
    s_.settings.set("language", model::JsonValue(v.language));
    s_.settings.set("threads", model::JsonValue(std::int64_t{v.threads}));
    s_.settings.set("memoryBudgetMb", model::JsonValue(std::int64_t{v.memoryMb}));
    application::ThreadConfig c;
    c.workers = v.threads;
    c.memoryBudget = static_cast<std::size_t>(v.memoryMb) << 20U;
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
    s_.bus.execute(cmd, t, *roll_);
    if (d.mode() == application::PasteMode::Redetect && !cmd.redetect().empty()) {
        for (const model::FrameId& f : t) {
            if (const model::Frame* fr = frame(f)) {
                trackJob(s_.analysis.detectFrame(*fr, roll_), tr("Erkennung"));
            }
        }
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
