// E2E-Tests (E-…): die echte Anwendung (Kompositionswurzel apps/autoneg) mit echten Adaptern,
// gesteuert über die Oberfläche mit Qt Test, ohne Bildschirm (Plattform offscreen; die
// Bildfläche läuft dann im QRhi-Null-Backend, ihr Bild ist trotzdem abfragbar).
// Rollen sind künstliche Negative als 16-bit-TIFF in einem temporären Ordner (schnell, mit
// bekanntem Bildfeld); Testing/Data wird nicht benutzt, damit dort nichts geschrieben wird.

#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"
#include "autoneg/app/composition.hpp"
#include "autoneg/app/gui_services.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"
#include "autoneg/ui/canvas_tools.hpp"
#include "autoneg/ui/dialogs.hpp"
#include "autoneg/ui/edit_list_model.hpp"
#include "autoneg/ui/gui_application.hpp"
#include "autoneg/ui/image_canvas.hpp"
#include "autoneg/ui/import_page.hpp"
#include "autoneg/ui/main_window.hpp"
#include "autoneg/ui/panels.hpp"
#include "autoneg/ui/translations.hpp"
#include "autoneg/ui/views.hpp"

#include <QtCore/QElapsedTimer>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QAction>
#include <QtGui/QDropEvent>
#include <QtTest/QTest>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableView>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>

namespace autoneg {
namespace {

namespace fs = std::filesystem;
using ui::MainWindow;

void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value); // plattformabhängig (PROGRESS.md)
#else
    setenv(name, value, 1);
#endif
}

/// Hält die Qt-Instanz für alle Tests und baut sie nach dem letzten Test ab (sonst meldet
/// LeakSanitizer Qts Schriftcaches als Lecks).
template <class T>
class QtInstance final : public ::testing::Environment
{
public:
    static std::unique_ptr<T>& slot()
    {
        static std::unique_ptr<T> instance;
        return instance;
    }
    void TearDown() override { slot().reset(); }
};

/// Eine Qt-Anwendung für alle Tests des Programms.
ui::GuiApplication& gui()
{
    auto& g = QtInstance<ui::GuiApplication>::slot();
    if (!g) {
        setEnv("QT_QPA_PLATFORM", "offscreen");
#if defined(AUTONEG_QT_PLUGIN_DIR)
        setEnv("QT_PLUGIN_PATH", AUTONEG_QT_PLUGIN_DIR);
#endif
        static int argc = 1;
        static char arg0[] = "autoneg_test_e2e";
        static char* argv[] = {arg0, nullptr};
        g = std::make_unique<ui::GuiApplication>(argc, argv);
    }
    return *g;
}

// Abbau nach dem letzten Test; gtest übernimmt das Objekt
[[maybe_unused]] ::testing::Environment* const qtTeardown =
    ::testing::AddGlobalTestEnvironment(new QtInstance<ui::GuiApplication>);

/// Wartet unter Abarbeitung der Ereignisse, bis cond gilt (höchstens ms).
bool waitFor(const std::function<bool()>& cond, int ms = 20000)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return true;
}

/// Künstliches Negativ Nr. i (Farbnegativ, Bildfeld und Basis bekannt).
testing::NegativeSpec specFor(int i, model::FilmType film)
{
    testing::NegativeSpec spec;
    spec.width = 480;
    spec.height = 320;
    spec.film = film;
    spec.seed = static_cast<std::uint64_t>(i);
    // jedes Bild anders (Staub, Lage des Bildfelds): sonst gleiche Prüfsumme = gleiche FrameId
    spec.dust = 1 + i % 4;
    spec.frame.x = 0.1 + 0.002 * i;
    return spec;
}

/// Schreibt n künstliche Negative als TIFF.
void writeRoll(const fs::path& dir, int n, model::FilmType film = model::FilmType::ColorNegative)
{
    fs::create_directories(dir);
    adapter::oiio::OiioImageWriter writer;
    for (int i = 1; i <= n; ++i) {
        const auto neg = testing::makeNegative(specFor(i, film));
        ports::WriteOptions o;
        o.format = ports::FileFormat::Tiff;
        o.bitDepth = 16;
        char name[32];
        std::snprintf(name, sizeof name, "SCAN%04d.tif", i);
        ASSERT_TRUE(writer.write(dir / name, neg.scan, o).has_value());
    }
}

/// Anwendung mit eigenen Benutzerverzeichnissen und einer Rolle.
class E2E : public ::testing::Test
{
protected:
    void start(int frames = 6, model::FilmType film = model::FilmType::ColorNegative)
    {
        rollDir = tmp.path() / "rolle";
        writeRoll(rollDir, frames, film);
        launch();
        window->openFolder(rollDir);
        ASSERT_TRUE(waitFor([this, frames] {
            return window->rollOpen() && window->filmstrip()->model()->rowCount() == frames;
        }));
        importAs(film);
        ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; })) << "keine Vorschau";
    }
    /// Import-Bildschirm einer neuen Rolle: Filmtyp wählen und importieren (K-M10-08)
    void importAs(model::FilmType film)
    {
        ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
        window->importPage()->choose(film);
        ASSERT_TRUE(window->importPage()->importButton()->isEnabled());
        window->importPage()->importButton()->click();
        ASSERT_EQ(window->view(), ui::MainWindow::View::Image);
    }
    void launch()
    {
        comp =
            std::make_unique<app::Composition>(app::Directories{tmp.path() / "config", tmp.path() / "cache"},
                                               gui().previewSink(), gui().dispatcher(), document);
        gui().setLanguage(comp->language()); // wie das Programm beim Start
        window = &gui().createWindow(app::guiServices(*comp));
        window->resize(1200, 800);
        window->show();
    }
    void restart()
    {
        gui().destroyWindow();
        window = nullptr;
        comp.reset();
        launch();
    }
    void TearDown() override
    {
        gui().destroyWindow();
        comp.reset();
        gui().setLanguage("de");
    }

    [[nodiscard]] model::EditList list() const { return window->controller().currentList(); }
    [[nodiscard]] const model::EditEntry* entry(const char* step) const
    {
        static model::EditList keep;
        keep = list();
        return keep.get(model::StepId(step));
    }
    QAction* action(const char* name) const { return window->action(QString::fromLatin1(name)); }
    /// Nächster modaler Dialog: f(dialog) ausführen.
    static void onNextDialog(std::function<void(QDialog*)> f) { onNext<QDialog>(std::move(f)); }
    /// Nächster modaler Dialog vom Typ T (auch verschachtelt): f(dialog) ausführen.
    template <class T>
    static void onNext(std::function<void(T*)> f)
    {
        QTimer::singleShot(50, [f = std::move(f)] {
            for (int i = 0; i < 1000; ++i) { // bis 20 s: erster Aufbau der Schriftdatenbank kann dauern
                if (auto* d = qobject_cast<T*>(QApplication::activeModalWidget())) {
                    f(d);
                    return;
                }
                QTest::qWait(20);
            }
        });
    }
    /// Namensabfrage (QInputDialog) mit text beantworten.
    static void answerName(const QString& text)
    {
        onNext<QInputDialog>([text](QInputDialog* d) {
            d->setTextValue(text);
            d->accept();
        });
    }
    [[nodiscard]] static int countFiles(const fs::path& dir, const std::string& ext)
    {
        int n = 0;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            n += e.path().extension() == ext ? 1 : 0;
        }
        return n;
    }

    testing::TempDir tmp;
    fs::path rollDir;
    adapter::qtprint::QtDocumentRenderer document;
    std::unique_ptr<app::Composition> comp;
    MainWindow* window = nullptr;
};

// E-01: LF-IMP-07, LF-UI-01, NF-PERF-06
TEST_F(E2E, E_01_OpenFolderShowsFilmstripAndPanels)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 36);
    launch();
    QElapsedTimer t;
    t.start();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->filmstrip()->model()->rowCount() == 36; }));
    importAs(model::FilmType::ColorNegative);
    ASSERT_TRUE(waitFor([this] { return window->filmstrip()->stripModel()->thumbnailsLoaded() >= 1; }));
    EXPECT_LE(t.elapsed(), 2000 * 5) << "erste Miniatur zu spät (Debug-Bau großzügig)";
    ASSERT_TRUE(
        waitFor([this] { return window->filmstrip()->stripModel()->thumbnailsLoaded() == 36; }, 60000));
    // Hauptfenster: Filmstreifen, Arbeitsfläche, Werkzeuge, Bearbeitungsliste, Histogramm
    EXPECT_TRUE(window->filmstrip()->isVisible());
    EXPECT_TRUE(window->canvas()->isVisible());
    EXPECT_TRUE(window->conversionPanel()->isVisibleTo(window));
    EXPECT_TRUE(window->editListView()->isVisible());
    EXPECT_TRUE(window->histogram()->isVisible());
    ASSERT_TRUE(waitFor([this] { return !window->canvas()->image().isNull(); }));
}

// E-02: LF-IMP-08
TEST_F(E2E, E_02_DragAndDropFolder)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 3);
    launch();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdU16String(rollDir.u16string()))});
    // wie beim echten Ziehen: erst Betreten, dann Ablegen
    QDragEnterEvent enter(QPoint(100, 100), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &enter);
    ASSERT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &drop);
    EXPECT_TRUE(waitFor([this] { return window->filmstrip()->model()->rowCount() == 3; }))
        << window->messages().join(QStringLiteral(" | ")).toStdString();
}

// E-10: LF-KON-01
TEST_F(E2E, E_10_SwitchFilmType)
{
    start(3);
    QComboBox* type = window->conversionPanel()->filmType();
    for (const char* t : {"bw", "slide", "color"}) {
        const int i = type->findData(QString::fromLatin1(t));
        type->setCurrentIndex(i);
        Q_EMIT type->activated(i);
        ASSERT_NE(entry("autoneg.filmmode"), nullptr);
        EXPECT_EQ(entry("autoneg.filmmode")->params.get<std::string>("type").value(), t);
        const auto before = window->previewsShown();
        EXPECT_TRUE(waitFor([&] { return window->previewsShown() > before; })) << t;
    }
}

// E-12: LF-KON-09 … -12
TEST_F(E2E, E_12_MultigradeAndSplitGrade)
{
    start(2, model::FilmType::BwNegative);
    ui::GradationPanel* g = window->gradationPanel();
    for (const auto& [button, grade] :
         std::vector<std::pair<int, double>>{{0, -1.0}, {5, 2.0}, {8, 3.5}, {11, 5.0}}) {
        g->button(button)->click();
        EXPECT_DOUBLE_EQ(entry("autoneg.gradation")->params.get<double>("grade").value(), grade);
    }
    g->setSplit(0.6, 0.4);
    EXPECT_DOUBLE_EQ(entry("autoneg.gradation")->params.get<double>("splitSoft").value(), 0.6);
    EXPECT_DOUBLE_EQ(entry("autoneg.gradation")->params.get<double>("splitHard").value(), 0.4);
}

// E-16, E-17: LF-EDT-04, LF-LST-01, LF-LST-02, LF-LST-04
TEST_F(E2E, E_17_SameChangeFiveTimesIsOneEntry)
{
    start(2);
    ui::ParamSlider* ev = window->adjustPanel()->slider("autoneg.exposure", "ev");
    ASSERT_NE(ev, nullptr);
    for (int i = 1; i <= 5; ++i) {
        ev->setValue(0.2 * i);
        Q_EMIT ev->committed(0.2 * i);
    }
    const model::EditList l = list();
    EXPECT_EQ(
        std::ranges::count_if(l.entries(),
                              [](const model::EditEntry& e) { return e.step.value() == "autoneg.exposure"; }),
        1);
    EXPECT_NEAR(entry("autoneg.exposure")->params.get<double>("ev").value(), 1.0, 1e-9);
    // Tabelle zeigt den Eintrag mit Herkunft manuell
    ui::EditListModel* m = window->editListModel();
    ASSERT_TRUE(waitFor([m] { return m->rowCount() >= 1; }));
    bool found = false;
    for (int r = 0; r < m->rowCount(); ++r) {
        if (m->entryAt(r)->step.value() == "autoneg.exposure") {
            found = true;
            EXPECT_EQ(m->data(m->index(r, ui::EditListModel::Origin)).toString(), QStringLiteral("manuell"));
        }
    }
    EXPECT_TRUE(found);
}

// E-18, E-19: LF-LST-03, LF-LST-06
TEST_F(E2E, E_18_E_19_RemoveAndToggleEntry)
{
    start(2);
    ui::ParamSlider* contrast = window->adjustPanel()->slider("autoneg.contrast", "amount");
    ui::ParamSlider* sharpen = window->adjustPanel()->slider("autoneg.sharpen", "amount");
    Q_EMIT contrast->committed(0.3);
    Q_EMIT sharpen->committed(1.0);
    ui::EditListModel* m = window->editListModel();
    const auto rowOf = [m](const char* step) {
        for (int r = 0; r < m->rowCount(); ++r) {
            if (m->entryAt(r)->step.value() == step) {
                return r;
            }
        }
        return -1;
    };
    // Schärfen aus- und wieder einschalten: Wert bleibt
    int r = rowOf("autoneg.sharpen");
    ASSERT_GE(r, 0);
    m->setData(m->index(r, ui::EditListModel::Active), Qt::Unchecked, Qt::CheckStateRole);
    EXPECT_FALSE(entry("autoneg.sharpen")->enabled);
    r = rowOf("autoneg.sharpen");
    m->setData(m->index(r, ui::EditListModel::Active), Qt::Checked, Qt::CheckStateRole);
    EXPECT_TRUE(entry("autoneg.sharpen")->enabled);
    EXPECT_DOUBLE_EQ(entry("autoneg.sharpen")->params.get<double>("amount").value(), 1.0);
    // Kontrast mit einem Klick entfernen
    window->editListView()->setCurrentIndex(m->index(rowOf("autoneg.contrast"), 0));
    auto* remove = window->findChild<QPushButton*>(QStringLiteral("removeEntry"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    EXPECT_EQ(entry("autoneg.contrast"), nullptr);
    EXPECT_NE(entry("autoneg.sharpen"), nullptr);
}

// E-21, E-45: LF-EDT-15, LF-UI-04
TEST_F(E2E, E_21_UndoRedoWithShortcuts)
{
    start(2);
    ui::ParamSlider* ev = window->adjustPanel()->slider("autoneg.exposure", "ev");
    Q_EMIT ev->committed(0.5);
    Q_EMIT ev->committed(1.0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_NEAR(entry("autoneg.exposure")->params.get<double>("ev").value(), 0.5, 1e-9);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(entry("autoneg.exposure"), nullptr);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    EXPECT_NEAR(entry("autoneg.exposure")->params.get<double>("ev").value(), 1.0, 1e-9);
}

// E-22: LF-EDT-16
TEST_F(E2E, E_22_ResetSliderAndImage)
{
    start(2);
    ui::ParamSlider* ev = window->adjustPanel()->slider("autoneg.exposure", "ev");
    ui::ParamSlider* sat = window->adjustPanel()->slider("autoneg.saturation", "saturation");
    Q_EMIT ev->committed(0.7);
    Q_EMIT sat->committed(0.3);
    QTest::mouseDClick(ev->slider(), Qt::LeftButton);
    EXPECT_EQ(entry("autoneg.exposure"), nullptr);
    EXPECT_NE(entry("autoneg.saturation"), nullptr);
    action("resetAll")->trigger();
    EXPECT_TRUE(list().empty());
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier); // Zurücksetzen ist eine Stufe
    EXPECT_NE(entry("autoneg.saturation"), nullptr);
}

// E-23: LF-EDT-13, LF-EDT-14
TEST_F(E2E, E_23_HistogramAndClippingWarning)
{
    start(2);
    ASSERT_TRUE(waitFor([this] { return !window->histogram()->histogram().bins[0].empty(); }));
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(5.0);
    ASSERT_TRUE(waitFor([this] {
        const auto& h = window->histogram()->histogram();
        return h.clipHigh.x > 0.01 || h.clipHigh.y > 0.01 || h.clipHigh.z > 0.01;
    })) << "starke Belichtung muss Lichter beschneiden";
    action("clipping")->trigger();
    EXPECT_TRUE(action("clipping")->isChecked());
}

// E-25: LF-BAT-02, LF-BAT-03, LF-BAT-06, LF-LST-07
TEST_F(E2E, E_25_CopyAndPasteToRollThenUndo)
{
    start(4);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.8);
    Q_EMIT window->adjustPanel()->slider("autoneg.sharpen", "amount")->committed(1.2);
    window->gradationPanel()->button(8)->click();
    action("copy")->trigger();
    window->filmstrip()->selectAll();
    onNextDialog([](QDialog* d) {
        auto* box = d->findChild<QDialogButtonBox*>();
        box->button(QDialogButtonBox::Ok)->click();
    });
    action("paste")->trigger();
    // alle Bilder haben die drei Einträge, Herkunft kopiert
    for (int r = 1; r < 4; ++r) {
        const auto* item = window->filmstrip()->stripModel()->itemAt(r);
        window->selectFrame(item->frame.id);
        ASSERT_NE(entry("autoneg.exposure"), nullptr) << r;
        EXPECT_EQ(entry("autoneg.exposure")->origin, model::Origin::Copied);
        EXPECT_NE(entry("autoneg.sharpen"), nullptr);
        EXPECT_NE(entry("autoneg.gradation"), nullptr);
    }
    // ein Strg+Z nimmt das Einfügen auf allen Bildern zurück (nach dem modalen Dialog ist das
    // Hauptfenster wieder das aktive Fenster)
    window->activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(window));
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    for (int r = 1; r < 4; ++r) {
        window->selectFrame(window->filmstrip()->stripModel()->itemAt(r)->frame.id);
        EXPECT_EQ(entry("autoneg.exposure"), nullptr) << r;
    }
}

// E-31: LF-BAT-10
TEST_F(E2E, E_31_RatingAndFilter)
{
    start(4);
    window->filmstrip()->selectFrame(window->filmstrip()->stripModel()->itemAt(0)->frame.id);
    QTest::keyClick(window, Qt::Key_P);
    window->filmstrip()->selectFrame(window->filmstrip()->stripModel()->itemAt(1)->frame.id);
    QTest::keyClick(window, Qt::Key_X);
    auto* filter = window->findChild<QComboBox*>(QStringLiteral("ratingFilter"));
    filter->setCurrentIndex(1);
    Q_EMIT filter->activated(1);
    EXPECT_EQ(window->filmstrip()->model()->rowCount(), 1);
    filter->setCurrentIndex(2);
    Q_EMIT filter->activated(2);
    EXPECT_EQ(window->filmstrip()->model()->rowCount(), 1);
    filter->setCurrentIndex(0);
    Q_EMIT filter->activated(0);
    EXPECT_EQ(window->filmstrip()->model()->rowCount(), 4);
}

// E-38: LF-DAT-01, LF-DAT-03 (Begleitdatei steht sofort nach dem Loslassen auf der Platte)
TEST_F(E2E, E_38_ImmediateSave)
{
    start(2);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.6);
    const fs::path sidecar = rollDir / "SCAN0001.tif.autoneg.json";
    ASSERT_TRUE(waitFor([&] { return fs::exists(sidecar); }, 2000)) << "keine Begleitdatei nach 2 s";
    std::ifstream in(sidecar);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("autoneg.exposure"), std::string::npos);
    // keine temporären Reste
    for (const auto& e : fs::directory_iterator(rollDir)) {
        EXPECT_NE(e.path().extension(), ".tmp") << e.path();
    }
}

// E-39: LF-DAT-02
TEST_F(E2E, E_39_ResumeAfterRestart)
{
    start(3);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(-0.4);
    window->gradationPanel()->button(3)->click();
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen() && window->previewsShown() > 0; }));
    ASSERT_NE(entry("autoneg.exposure"), nullptr);
    EXPECT_NEAR(entry("autoneg.exposure")->params.get<double>("ev").value(), -0.4, 1e-9);
    EXPECT_NE(entry("autoneg.gradation"), nullptr);
}

// E-42: NF-REL-02, LF-IMP-03
TEST_F(E2E, E_42_BrokenFileDoesNotCrash)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 2);
    std::ofstream(rollDir / "SCAN0003.ARW", std::ios::binary) << "kein RAW";
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->filmstrip()->model()->rowCount() == 3; }));
    importAs(model::FilmType::ColorNegative);
    const auto* broken = window->filmstrip()->stripModel()->itemAt(2);
    window->selectFrame(broken->frame.id);
    ASSERT_TRUE(waitFor([this] { return window->filmstrip()->stripModel()->itemAt(2)->failed; }, 20000));
    EXPECT_TRUE(window->isVisible()); // läuft weiter
    window->selectFrame(window->filmstrip()->stripModel()->itemAt(0)->frame.id);
    const auto before = window->previewsShown();
    EXPECT_TRUE(waitFor([&] { return window->previewsShown() > before; }));
}

// E-43: LF-UI-02
TEST_F(E2E, E_43_ZoomLevels)
{
    start(1);
    const auto shown = [this] { return window->previewsShown(); };
    for (const char* a : {"zoom100", "zoom200", "zoomFit"}) {
        const auto before = shown();
        action(a)->trigger();
        EXPECT_TRUE(waitFor([&] { return shown() > before; })) << a;
    }
    action("zoom100")->trigger();
    EXPECT_EQ(window->canvas()->zoom(), ui::ImageCanvas::Zoom::Actual);
    ASSERT_TRUE(waitFor([this] { return !window->canvas()->image().isNull(); }));
    // 100 %: ein Quellpixel je Gerätepixel (Quelle 480 × 320)
    const double dpr = window->canvas()->devicePixelRatioF();
    EXPECT_NEAR(window->canvas()->mapping().imageRect.width() * dpr, 480.0, 1.0);
}

// E-45, E-11: LF-UI-04, LF-KON-08
TEST_F(E2E, E_45_ShortcutsTriggerFunctions)
{
    start(1);
    QTest::keyClick(window, Qt::Key_Backslash);
    EXPECT_TRUE(action("compare")->isChecked());
    QTest::keyClick(window, Qt::Key_Backslash);
    EXPECT_FALSE(action("compare")->isChecked());
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    EXPECT_EQ(window->canvas()->zoom(), ui::ImageCanvas::Zoom::Actual);
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    EXPECT_EQ(window->canvas()->zoom(), ui::ImageCanvas::Zoom::Fit);
    QTest::keyClick(window, Qt::Key_C);
    EXPECT_TRUE(action("toolCrop")->isChecked());
    EXPECT_NE(window->canvas()->tool(), nullptr);
    QTest::keyClick(window, Qt::Key_C);
    EXPECT_EQ(window->canvas()->tool(), nullptr);
}

// E-46: LF-UI-08
TEST_F(E2E, E_46_EverySliderHasTooltip)
{
    launch();
    const auto sliders = window->findChildren<QSlider*>();
    ASSERT_FALSE(sliders.isEmpty());
    for (const QSlider* s : sliders) {
        EXPECT_FALSE(s->toolTip().isEmpty()) << s->objectName().toStdString();
    }
    for (const auto* b : window->findChildren<QAbstractButton*>()) {
        if (b->isVisibleTo(window) && !b->objectName().isEmpty() &&
            !b->objectName().startsWith(QStringLiteral("qt_"))) {
            EXPECT_FALSE(b->toolTip().isEmpty()) << b->objectName().toStdString();
        }
    }
}

// E-13 (Teil): LF-EDT-01 Zuschnitt über das Werkzeug ist ein Eintrag mit Herkunft manuell
TEST_F(E2E, E_13_CropToolCommitsOneEntry)
{
    start(1);
    action("toolCrop")->trigger();
    auto* tool = dynamic_cast<ui::CropRotateTool*>(window->canvas()->tool());
    ASSERT_NE(tool, nullptr);
    tool->setAspect(4.0 / 3.0);
    // Anfasser unten rechts mit der Maus ziehen
    const ui::ViewMapping m = window->canvas()->mapping();
    const QRectF r = m.toWidget(tool->rect());
    QWidget* overlay = window->canvas()->childAt(r.bottomRight().toPoint() - QPoint(1, 1));
    ASSERT_NE(overlay, nullptr);
    const QPoint from = overlay->mapFrom(window->canvas(), r.bottomRight().toPoint() - QPoint(1, 1));
    QTest::mousePress(overlay, Qt::LeftButton, {}, from);
    QTest::mouseMove(overlay, from - QPoint(60, 40));
    QTest::mouseRelease(overlay, Qt::LeftButton, {}, from - QPoint(60, 40));
    ASSERT_NE(entry("autoneg.crop"), nullptr);
    EXPECT_EQ(entry("autoneg.crop")->origin, model::Origin::Manual);
    EXPECT_NEAR(tool->pixelAspect(), 4.0 / 3.0, 1e-6);
}

// E-03 (Teil): NF-USE-01, LF-GEO-01, LF-BAS-01, LF-EXP-01 – Auto-Bildfeld und Export
TEST_F(E2E, E_03_AutoFrameThenExport)
{
    start(2);
    window->findChild<QPushButton*>(QStringLiteral("autoFrame"))->click();
    ASSERT_TRUE(waitFor([this] { return entry("autoneg.crop") != nullptr; }, 30000))
        << "kein Bildfeld erkannt";
    EXPECT_EQ(entry("autoneg.crop")->origin, model::Origin::Automatic);
    const fs::path out = tmp.path() / "export";
    onNextDialog([&out](QDialog* d) {
        auto* folder = d->findChild<QLineEdit*>(QStringLiteral("folder"));
        folder->setText(QString::fromStdU16String(out.u16string()));
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("export")->trigger();
    int files = 0;
    const auto count = [&] {
        files = 0;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(out, ec)) {
            files += e.path().extension() == ".tif" ? 1 : 0;
        }
        return files >= 1;
    };
    ASSERT_TRUE(waitFor(count, 60000)) << "keine Exportdatei";
    EXPECT_EQ(files, 1); // aktives Bild
}

// E-05: LF-BAS-04 – Messrechteck in den Filmrand ziehen
TEST_F(E2E, E_05_MeasureBaseInRectangle)
{
    start(1);
    const auto truth = testing::makeNegative(specFor(1, model::FilmType::ColorNegative)).truth;
    window->findChild<QPushButton*>(QStringLiteral("pickBase"))->click();
    auto* tool = dynamic_cast<ui::RectPickerTool*>(window->canvas()->tool());
    ASSERT_NE(tool, nullptr);
    // Rechteck mittig im oberen Filmrand, mit Abstand zu den Kanten
    const model::NormRect rb = truth.rebateTop;
    const ui::ViewMapping m = window->canvas()->mapping();
    const QPoint a = m.toWidget(QPointF(rb.x + rb.w * 0.4, rb.y + rb.h * 0.3)).toPoint();
    const QPoint b = m.toWidget(QPointF(rb.x + rb.w * 0.6, rb.y + rb.h * 0.7)).toPoint();
    QWidget* overlay = window->canvas()->childAt(a);
    ASSERT_NE(overlay, nullptr);
    QTest::mousePress(overlay, Qt::LeftButton, {}, overlay->mapFrom(window->canvas(), a));
    QTest::mouseMove(overlay, overlay->mapFrom(window->canvas(), b));
    QTest::mouseRelease(overlay, Qt::LeftButton, {}, overlay->mapFrom(window->canvas(), b));
    ASSERT_TRUE(waitFor([this] { return entry("autoneg.filmbase") != nullptr; }));
    const model::EditEntry* e = entry("autoneg.filmbase");
    EXPECT_EQ(e->origin, model::Origin::Manual);
    EXPECT_EQ(e->params.get<std::string>("source").value_or(""), "manual");
    const base::Vec3 got = e->params.get<base::Vec3>("base").value();
    EXPECT_NEAR(got.x, truth.base.x, 0.03);
    EXPECT_NEAR(got.y, truth.base.y, 0.03);
    EXPECT_NEAR(got.z, truth.base.z, 0.03);
}

// E-41: LF-DAT-06 – Scan außerhalb umbenannt, Bearbeitung bleibt
TEST_F(E2E, E_41_RenamedScanKeepsEdits)
{
    start(2);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.9);
    ASSERT_TRUE(waitFor([this] { return fs::exists(rollDir / "SCAN0001.tif.autoneg.json"); }, 2000));
    gui().destroyWindow();
    window = nullptr;
    comp.reset();
    fs::rename(rollDir / "SCAN0001.tif", rollDir / "Urlaub_01.tif"); // Testtreiber benennt um
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen() && window->previewsShown() > 0; }));
    // erstes Bild nach Dateiname ist jetzt SCAN0002, das umbenannte steht am Ende
    const auto* item = window->filmstrip()->stripModel()->itemAt(1);
    ASSERT_EQ(item->frame.path.filename(), "Urlaub_01.tif");
    window->selectFrame(item->frame.id);
    ASSERT_NE(entry("autoneg.exposure"), nullptr);
    EXPECT_NEAR(entry("autoneg.exposure")->params.get<double>("ev").value(), 0.9, 1e-9);
    EXPECT_TRUE(fs::exists(rollDir / "Urlaub_01.tif.autoneg.json"));
}

// E-28: LF-BAT-07 – Bearbeitungsvorlage speichern, Neustart, auf andere Rolle anwenden
TEST_F(E2E, E_28_EditPresetAcrossRestart)
{
    start(2);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.4);
    Q_EMIT window->adjustPanel()->slider("autoneg.saturation", "saturation")->committed(0.25);
    answerName(QStringLiteral("Portra warm"));
    action("saveEditPreset")->trigger();
    restart();
    const fs::path other = tmp.path() / "andere";
    writeRoll(other, 2);
    window->openFolder(other);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen(); }));
    importAs(model::FilmType::ColorNegative);
    ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; }));
    auto* menu = window->findChild<QMenu*>(QStringLiteral("applyEditPreset"));
    ASSERT_NE(menu, nullptr);
    Q_EMIT menu->aboutToShow();
    ASSERT_EQ(menu->actions().size(), 1);
    EXPECT_EQ(menu->actions()[0]->text(), QStringLiteral("Portra warm"));
    menu->actions()[0]->trigger();
    ASSERT_NE(entry("autoneg.exposure"), nullptr);
    EXPECT_EQ(entry("autoneg.exposure")->origin, model::Origin::Preset);
    EXPECT_NEAR(entry("autoneg.saturation")->params.get<double>("saturation").value(), 0.25, 1e-9);
}

// E-32, E-33: LF-EXP-01 … -03, -05 … -07, -09, -10 – Exportdialog
TEST_F(E2E, E_32_E_33_ExportDialogFormatsAndTemplate)
{
    start(2, model::FilmType::BwNegative);
    const fs::path out = tmp.path() / "export";
    const auto exportWith = [&](const char* format, int depth, int profile, int longEdge, bool gray,
                                const QString& pattern, const QString& preset) {
        onNextDialog([=, &out](QDialog* d) {
            auto* f = d->findChild<QComboBox*>(QStringLiteral("format"));
            f->setCurrentIndex(f->findText(QString::fromLatin1(format)));
            auto* b = d->findChild<QComboBox*>(QStringLiteral("bitDepth"));
            b->setCurrentIndex(b->findData(depth));
            d->findChild<QSpinBox*>(QStringLiteral("quality"))->setValue(90);
            auto* p = d->findChild<QComboBox*>(QStringLiteral("profile"));
            p->setCurrentIndex(p->findData(profile));
            d->findChild<QSpinBox*>(QStringLiteral("longEdge"))->setValue(longEdge);
            d->findChild<QCheckBox*>(QStringLiteral("grayscale"))->setChecked(gray);
            d->findChild<QLineEdit*>(QStringLiteral("folder"))
                ->setText(QString::fromStdU16String(out.u16string()));
            d->findChild<QLineEdit*>(QStringLiteral("pattern"))->setText(pattern);
            if (!preset.isEmpty()) {
                answerName(preset);
                d->findChild<QPushButton*>(QStringLiteral("savePreset"))->click();
            }
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        action("export")->trigger();
    };
    exportWith("TIFF", 16, static_cast<int>(ports::ProfileId::AdobeRgb), 0, false,
               QStringLiteral("{roll}_{frame}"), QStringLiteral("Archiv"));
    ASSERT_TRUE(waitFor([&] { return fs::exists(out / "rolle_001.tif"); }, 60000));
    exportWith("JPEG", 8, static_cast<int>(ports::ProfileId::Srgb), 200, false, QStringLiteral("{name}_web"),
               {});
    ASSERT_TRUE(waitFor([&] { return fs::exists(out / "SCAN0001_web.jpg"); }, 60000));
    exportWith("PNG", 16, static_cast<int>(ports::ProfileId::Srgb), 0, true, QStringLiteral("{name}_sw"), {});
    ASSERT_TRUE(waitFor([&] { return fs::exists(out / "SCAN0001_sw.png"); }, 60000));
    EXPECT_EQ(countFiles(out, ".tif") + countFiles(out, ".jpg") + countFiles(out, ".png"), 3); // keine Reste
    EXPECT_TRUE(fs::exists(out / "rolle_001.tif"));
    EXPECT_TRUE(fs::exists(out / "SCAN0001_web.jpg"));
    EXPECT_TRUE(fs::exists(out / "SCAN0001_sw.png")) << [&out] {
        std::string names;
        for (const auto& e : fs::directory_iterator(out)) {
            names += e.path().filename().string() + " ";
        }
        return names;
    }();
    // Größe und Graustufen unabhängig über den Leseadapter prüfen
    adapter::oiio::OiioImageReader reader;
    const auto jpg = reader.probe(out / "SCAN0001_web.jpg");
    ASSERT_TRUE(jpg.has_value());
    EXPECT_EQ(std::max(jpg->width, jpg->height), 200);
    const auto png = reader.read(out / "SCAN0001_sw.png");
    ASSERT_TRUE(png.has_value());
    EXPECT_EQ(png->spec().channels, 1);
    // Vorlage gespeichert und im nächsten Dialog wählbar
    std::optional<int> presets;
    onNextDialog([&presets](QDialog* d) {
        presets = d->findChild<QComboBox*>(QStringLiteral("presetCombo"))->findText(QStringLiteral("Archiv"));
        d->reject();
    });
    action("export")->trigger();
    ASSERT_TRUE(presets.has_value());
    EXPECT_GT(*presets, 0);
}

// E-34, E-36: LF-KTA-01 … -03, -05 … -10 – Kontaktabzug über den Dialog, Vorlage
TEST_F(E2E, E_34_E_36_ContactSheetPdfAndTemplate)
{
    start(6);
    const fs::path pdf = tmp.path() / "kontakt.pdf";
    onNextDialog([&](QDialog* d) {
        d->findChild<QPlainTextEdit*>(QStringLiteral("notes"))->setPlainText(QStringLiteral("Toskana"));
        d->findChild<QComboBox*>(QStringLiteral("scale"))->setCurrentIndex(1); // 1:1
        answerName(QStringLiteral("KB 1:1"));
        d->findChild<QPushButton*>(QStringLiteral("saveTemplate"))->click();
        onNext<QFileDialog>([&pdf](QFileDialog* f) {
            // wie ein Anwender: Dateiname eintippen, Speichern drücken
            auto* name = f->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            ASSERT_NE(name, nullptr);
            name->setText(QString::fromStdU16String(pdf.u16string()));
            f->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        d->findChild<QPushButton*>(QStringLiteral("exportPdf"))->click();
        d->accept();
    });
    action("contactSheet")->trigger();
    ASSERT_TRUE(waitFor([&] { return fs::exists(pdf) && fs::file_size(pdf) > 1000; }, 60000))
        << "kein PDF: " << window->messages().join(QStringLiteral(" | ")).toStdString();
    // Vorlage nach Neustart vorhanden
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen(); }));
    std::optional<int> found;
    onNextDialog([&found](QDialog* d) {
        found = d->findChild<QComboBox*>(QStringLiteral("presetCombo"))->findText(QStringLiteral("KB 1:1"));
        d->accept();
    });
    action("contactSheet")->trigger();
    ASSERT_TRUE(found.has_value());
    EXPECT_GT(*found, 0);
}

// E-27: LF-BAT-05 – Synchronisieren
TEST_F(E2E, E_27_SyncAppliesToSelection)
{
    start(3);
    window->filmstrip()->selectAll();
    action("sync")->trigger();
    ASSERT_TRUE(action("sync")->isChecked());
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.3);
    for (int r = 0; r < 3; ++r) {
        window->selectFrame(window->filmstrip()->stripModel()->itemAt(r)->frame.id);
        ASSERT_NE(entry("autoneg.exposure"), nullptr) << r;
    }
}

// E-29: LF-BAT-08, LF-UI-07 – Stapelerkennung für die Auswahl
TEST_F(E2E, E_29_BatchDetection)
{
    start(4);
    window->filmstrip()->selectAll();
    window->findChild<QPushButton*>(QStringLiteral("autoFrame"))->click();
    for (int r = 0; r < 4; ++r) {
        const model::FrameId id = window->filmstrip()->stripModel()->itemAt(r)->frame.id;
        ASSERT_TRUE(waitFor(
            [&] {
                window->selectFrame(id);
                return entry("autoneg.crop") != nullptr;
            },
            30000))
            << r;
    }
}

// E-14 (Teil), E-15: LF-EDT-02, LF-EDT-03 – 90° drehen und spiegeln
TEST_F(E2E, E_15_RotateAndFlip)
{
    start(1);
    action("rotateRight")->trigger();
    ASSERT_NE(entry("autoneg.orientation"), nullptr);
    EXPECT_EQ(entry("autoneg.orientation")->params.get<int>("quarterTurns").value(), 1);
    action("flipH")->trigger();
    EXPECT_TRUE(entry("autoneg.orientation")->params.get<bool>("flipH").value());
    action("flipH")->trigger();
    action("rotateLeft")->trigger();
    EXPECT_EQ(entry("autoneg.orientation"), nullptr); // zurück im Neutralzustand: kein Eintrag
    // Hochformat nach Drehung: Vorschau wird höher als breit
    action("rotateRight")->trigger();
    ASSERT_TRUE(waitFor([this] {
        const QImage& img = window->canvas()->image();
        return !img.isNull() && img.height() > img.width();
    }));
}

// E-24: LF-EDT-12, LF-LST-02 – drei Stellen retuschieren: ein Eintrag mit drei Stellen
TEST_F(E2E, E_24_RetouchThreeSpots)
{
    start(1);
    action("toolRetouch")->trigger();
    ASSERT_NE(window->canvas()->tool(), nullptr);
    const ui::ViewMapping m = window->canvas()->mapping();
    for (const QPointF n : {QPointF(0.3, 0.4), QPointF(0.5, 0.5), QPointF(0.6, 0.45)}) {
        const QPoint w = m.toWidget(n).toPoint();
        QWidget* overlay = window->canvas()->childAt(w);
        ASSERT_NE(overlay, nullptr);
        QTest::mouseClick(overlay, Qt::LeftButton, {}, overlay->mapFrom(window->canvas(), w));
    }
    ASSERT_NE(entry("autoneg.retouch"), nullptr);
    EXPECT_EQ(entry("autoneg.retouch")->params.get<model::SpotList>("spots").value().size(), 3U);
    const model::EditList l = list();
    EXPECT_EQ(std::ranges::count_if(
                  l.entries(), [](const model::EditEntry& e) { return e.step.value() == "autoneg.retouch"; }),
              1);
}

// E-26: LF-BAT-04 – Einfügen mit "neu erkennen": Zuschnitt wird nicht kopiert, sondern erkannt
TEST_F(E2E, E_26_PasteWithRedetect)
{
    start(2);
    window->findChild<QPushButton*>(QStringLiteral("autoFrame"))->click();
    ASSERT_TRUE(waitFor([this] { return entry("autoneg.crop") != nullptr; }, 30000));
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.5);
    action("copy")->trigger();
    const model::FrameId second = window->filmstrip()->stripModel()->itemAt(1)->frame.id;
    window->filmstrip()->selectFrame(second);
    onNextDialog([](QDialog* d) {
        auto* sel = d->findChild<QListWidget*>(QStringLiteral("pasteSelection"));
        for (int i = 0; i < sel->count(); ++i) {
            sel->item(i)->setCheckState(Qt::Checked); // auch den Zuschnitt wählen
        }
        d->findChild<QRadioButton*>(QStringLiteral("redetect"))->setChecked(true);
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("paste")->trigger();
    ASSERT_NE(entry("autoneg.exposure"), nullptr);
    EXPECT_EQ(entry("autoneg.exposure")->origin, model::Origin::Copied);
    // Zuschnitt kommt aus der Erkennung des zweiten Bildes, nicht aus der Kopie
    ASSERT_TRUE(waitFor([this] { return entry("autoneg.crop") != nullptr; }, 30000));
    EXPECT_EQ(entry("autoneg.crop")->origin, model::Origin::Automatic);
    const auto truth = testing::makeNegative(specFor(2, model::FilmType::ColorNegative)).truth;
    EXPECT_NEAR(entry("autoneg.crop")->params.get<model::NormRect>("rect").value().x, truth.frame.x, 0.01);
}

// E-30: LF-BAT-09, NF-PERF-04, NF-PERF-10, LF-UI-07 – Stapelexport abbrechen, dabei bearbeiten
TEST_F(E2E, E_30_BatchExportCancelWhileEditing)
{
    start(36);
    // Mehrere Bilder parallel (NF-PERF-11), aber nicht so viele, dass die Rolle fertig ist,
    // bevor abgebrochen wird
    application::ThreadConfig tc;
    tc.workers = 4;
    tc.memoryBudget = comp->jobs().budget().total();
    comp->jobs().configure(tc);
    window->filmstrip()->selectAll();
    const fs::path out = tmp.path() / "export";
    onNextDialog([&out](QDialog* d) {
        d->findChild<QLineEdit*>(QStringLiteral("folder"))
            ->setText(QString::fromStdU16String(out.u16string()));
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("export")->trigger();
    auto* cancel = window->findChild<QPushButton*>(QStringLiteral("cancelJobs"));
    ASSERT_TRUE(waitFor([cancel] { return cancel->isVisible(); }, 5000));
    ASSERT_TRUE(waitFor([&] { return countFiles(out, ".tif") >= 2; }, 60000));
    // während des Exports: Bild 5 bearbeiten, Vorschau reagiert
    window->selectFrame(window->filmstrip()->stripModel()->itemAt(4)->frame.id);
    const auto before = window->previewsShown();
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.7);
    EXPECT_TRUE(waitFor([&] { return window->previewsShown() > before; }, 5000));
    cancel->click();
    ASSERT_TRUE(waitFor([cancel] { return !cancel->isVisible(); }, 30000));
    QTest::qWait(300);
    const int written = countFiles(out, ".tif");
    EXPECT_LT(written, 36);
    for (const auto& e : fs::directory_iterator(out)) {
        EXPECT_EQ(e.path().string().find(".tmp"), std::string::npos) << e.path(); // keine halben Dateien
    }
    EXPECT_TRUE(window->messages().last().contains(QStringLiteral("abgebrochen")));
}

// E-09: LF-GEO-05 – Erkennung schlägt fehl: Meldung, Zuschnitt bleibt unverändert
TEST_F(E2E, E_09_DetectionFailureIsReported)
{
    rollDir = tmp.path() / "rolle";
    fs::create_directories(rollDir);
    adapter::oiio::OiioImageWriter writer;
    ports::WriteOptions o;
    o.format = ports::FileFormat::Tiff;
    o.bitDepth = 16;
    // gleichmäßig graue Fläche: kein Bildfeld erkennbar
    ASSERT_TRUE(writer
                    .write(rollDir / "LEER.tif",
                           testing::imageFrom(300, 200, 3, [](int, int, int) { return 0.4F; }), o)
                    .has_value());
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen(); }));
    importAs(model::FilmType::ColorNegative);
    ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; }));
    const int before = static_cast<int>(window->messages().size());
    window->findChild<QPushButton*>(QStringLiteral("autoFrame"))->click();
    ASSERT_TRUE(waitFor([&] { return static_cast<int>(window->messages().size()) > before; }, 30000));
    EXPECT_TRUE(window->messages().last().startsWith(QStringLiteral("Bildfeld erkennen")))
        << window->messages().last().toStdString();
    EXPECT_EQ(entry("autoneg.crop"), nullptr);
}

// K-M10-08 (Wunsch des Nutzers 25.10.2026): neue Rolle → Import-Bildschirm mit Filmtyp, Scans
// zunächst unverändert; danach Bildansicht, Übersicht umschaltbar, Filmtyp später änderbar,
// Bedienelemente passend zum Filmtyp (Gradation nur bei S/W-Negativ)
TEST_F(E2E, M10_ImportScreenThenViewsAndFilmTypes)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 4, model::FilmType::BwNegative);
    launch();
    EXPECT_EQ(window->view(), ui::MainWindow::View::Start);
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
    // ohne Filmtyp kein Import und keine Ansichten
    EXPECT_FALSE(window->importPage()->importButton()->isEnabled());
    EXPECT_FALSE(action("viewOverview")->isEnabled());
    EXPECT_FALSE(action("viewImage")->isEnabled());
    EXPECT_FALSE(window->filmstrip()->isVisible());
    ASSERT_TRUE(
        waitFor([this] { return window->filmstrip()->stripModel()->thumbnailsLoaded() == 4; }, 30000));
    window->importPage()->choose(model::FilmType::BwNegative);
    window->importPage()->importButton()->click();
    ASSERT_EQ(window->view(), ui::MainWindow::View::Image);
    EXPECT_TRUE(window->filmstrip()->isVisible());
    // Filmtyp der Rolle gespeichert
    std::ifstream rollFile(rollDir / "roll.autoneg.json");
    const std::string rollJson((std::istreambuf_iterator<char>(rollFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(rollJson.find("\"bw\""), std::string::npos) << rollJson;
    // S/W: Gradation sichtbar, Weißabgleich gesperrt
    auto* tabs = window->findChild<QTabWidget*>(QStringLiteral("tools"));
    ASSERT_NE(tabs, nullptr);
    EXPECT_TRUE(tabs->isTabVisible(tabs->indexOf(window->gradationPanel())));
    EXPECT_FALSE(window->adjustPanel()->slider("autoneg.whitebalance", "temperature")->isEnabled());
    EXPECT_TRUE(window->adjustPanel()->slider("autoneg.exposure", "ev")->isEnabled());
    // Übersicht: alle Bilder, Doppelklick öffnet die Bildansicht
    action("viewOverview")->trigger();
    ASSERT_EQ(window->view(), ui::MainWindow::View::Overview);
    EXPECT_TRUE(window->overview()->isVisible());
    EXPECT_FALSE(window->filmstrip()->isVisible());
    EXPECT_EQ(window->overview()->model()->rowCount(), 4);
    EXPECT_EQ(window->rollFilmCombo()->currentData().toInt(), static_cast<int>(model::FilmType::BwNegative));
    // Filmtyp der Rolle ändern: Dia → keine Gradation
    const int slide = window->rollFilmCombo()->findData(static_cast<int>(model::FilmType::Slide));
    window->rollFilmCombo()->setCurrentIndex(slide);
    Q_EMIT window->rollFilmCombo()->activated(slide);
    Q_EMIT window->overview()->doubleClicked(window->overview()->model()->index(2, 0));
    ASSERT_EQ(window->view(), ui::MainWindow::View::Image);
    EXPECT_EQ(window->filmstrip()->currentIndex().row(), 2);
    EXPECT_FALSE(tabs->isTabVisible(tabs->indexOf(window->gradationPanel())));
    EXPECT_TRUE(window->adjustPanel()->slider("autoneg.whitebalance", "temperature")->isEnabled());
    // Filmtyp wieder offen: nur der Scan, Regler und Automatiken gesperrt
    action("viewOverview")->trigger();
    const int none = window->rollFilmCombo()->findData(static_cast<int>(model::FilmType::Unconverted));
    window->rollFilmCombo()->setCurrentIndex(none);
    Q_EMIT window->rollFilmCombo()->activated(none);
    action("viewImage")->trigger();
    EXPECT_FALSE(window->adjustPanel()->slider("autoneg.exposure", "ev")->isEnabled());
    EXPECT_FALSE(window->findChild<QPushButton*>(QStringLiteral("autoFrame"))->isEnabled());
    // Wiederöffnen: bekannte Rolle mit Filmtyp geht direkt in die Bildansicht
    action("viewOverview")->trigger();
    window->rollFilmCombo()->setCurrentIndex(slide);
    Q_EMIT window->rollFilmCombo()->activated(slide);
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen(); }));
    EXPECT_EQ(window->view(), ui::MainWindow::View::Image);
}

// K-M10-08: Abbrechen auf dem Import-Bildschirm schließt die Rolle wieder
TEST_F(E2E, M10_ImportCancelClosesRoll)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 2);
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
    window->importPage()->findChild<QPushButton*>(QStringLiteral("importCancel"))->click();
    EXPECT_EQ(window->view(), ui::MainWindow::View::Start);
    EXPECT_FALSE(window->rollOpen());
    EXPECT_FALSE(fs::exists(rollDir / "roll.autoneg.json"));
}

// E-48: LF-DAT-04, NF-PERF-12 – Einstellungen bleiben nach Neustart erhalten
TEST_F(E2E, E_48_SettingsPersist)
{
    launch();
    onNextDialog([](QDialog* d) {
        d->findChild<QSpinBox*>(QStringLiteral("threads"))->setValue(3);
        d->findChild<QSpinBox*>(QStringLiteral("memoryMb"))->setValue(2048);
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("settings")->trigger();
    restart();
    std::optional<std::pair<int, int>> seen;
    onNextDialog([&seen](QDialog* d) {
        seen = {d->findChild<QSpinBox*>(QStringLiteral("threads"))->value(),
                d->findChild<QSpinBox*>(QStringLiteral("memoryMb"))->value()};
        d->reject();
    });
    action("settings")->trigger();
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(seen->first, 3);
    EXPECT_EQ(seen->second, 2048);
}

// E-44: LF-UI-03 – Sprache Englisch: keine unübersetzten Texte
TEST_F(E2E, E_44_EnglishUserInterface)
{
    launch();
    onNextDialog([](QDialog* d) {
        auto* lang = d->findChild<QComboBox*>(QStringLiteral("language"));
        lang->setCurrentIndex(lang->findData(QStringLiteral("en")));
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("settings")->trigger();
    EXPECT_TRUE(window->messages().last().contains(QStringLiteral("Neustart")));
    restart(); // übernimmt die gespeicherte Sprache
    EXPECT_EQ(action("openFolder")->text(), QStringLiteral("Open folder …"));
    // Alle sichtbaren Texte: keiner darf ein deutscher Text mit vorhandener Übersetzung sein
    ui::TableTranslator table;
    QStringList texts;
    for (const QAction* a : window->findChildren<QAction*>()) {
        texts << a->text() << a->toolTip();
    }
    for (const QAbstractButton* b : window->findChildren<QAbstractButton*>()) {
        texts << b->text() << b->toolTip();
    }
    for (const QLabel* l : window->findChildren<QLabel*>()) {
        texts << l->text();
    }
    for (const QDockWidget* dock : window->findChildren<QDockWidget*>()) {
        texts << dock->windowTitle();
    }
    for (const QSlider* s : window->findChildren<QSlider*>()) {
        texts << s->toolTip();
    }
    int checked = 0;
    for (const QString& t : texts) {
        if (t.isEmpty()) {
            continue;
        }
        const QString en = table.translate("", t.toUtf8().constData());
        EXPECT_TRUE(en.isEmpty() || en == t) << "unübersetzt: " << t.toStdString();
        ++checked;
    }
    EXPECT_GT(checked, 50);
}

} // namespace
} // namespace autoneg
