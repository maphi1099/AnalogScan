// E2E-Tests (E-…): die echte Anwendung (Kompositionswurzel apps/autoneg) mit echten Adaptern,
// gesteuert über die Oberfläche mit Qt Test, ohne Bildschirm (Plattform offscreen; die
// Bildfläche läuft dann im QRhi-Null-Backend, ihr Bild ist trotzdem abfragbar).
// Rollen sind künstliche Negative als 16-bit-TIFF in einem temporären Ordner (schnell, mit
// bekanntem Bildfeld); Testing/Data wird nicht benutzt, damit dort nichts geschrieben wird.

#include "adapters/qtprint/qt_document_renderer.hpp"
#include "autoneg/composition.hpp"
#include "autoneg/gui_services.hpp"
#include "e2e_fixture.hpp"
#include "support/synthetic.hpp"
#include "support/temp_dir.hpp"
#include "ui/canvas_tools.hpp"
#include "ui/dialogs.hpp"
#include "ui/edit_list_model.hpp"
#include "ui/gui_application.hpp"
#include "ui/image_canvas.hpp"
#include "ui/import_page.hpp"
#include "ui/main_window.hpp"
#include "ui/panels.hpp"
#include "ui/translations.hpp"
#include "ui/views.hpp"

#include <QtCore/QElapsedTimer>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QAction>
#include <QtGui/QDropEvent>
#include <QtGui/QWheelEvent>
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
#include <QtWidgets/QKeySequenceEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListView>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolButton>
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
    // Die Beschnittwarnung im Bild entfällt auf Wunsch des Nutzers (K-M10-12); der Beschnitt
    // bleibt im Histogramm sichtbar
    EXPECT_EQ(action("clipping"), nullptr);
}

// E-25: LF-BAT-02, LF-BAT-03, LF-BAT-06, LF-LST-07
TEST_F(E2E, E_25_CopyAndPasteToRollThenUndo)
{
    start(4);
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.8);
    Q_EMIT window->adjustPanel()->slider("autoneg.sharpen", "amount")->committed(1.2);
    window->gradationPanel()->button(8)->click();
    action("copy")->trigger();
    // Einfügen auf alle vom Leuchttisch: dort nimmt ein Rückgängig die ganze Aktion zurück
    // (K-M10-13; in Entwickeln wirkt Rückgängig nur auf das aktive Bild)
    action("viewOverview")->trigger();
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
    action("viewOverview")->trigger();
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

// Reflexion: die Detailansicht rechnete das ganze Bild (aus der halben Auflösung). Bei 100 % und
// 200 % rechnet die Pipeline nur den sichtbaren Ausschnitt in voller Auflösung, die Arbeitsfläche
// zeigt ihn an seiner Stelle (200 %: die Brücke vergrößert auf das Doppelte).
TEST_F(E2E, M10_DetailRendersOnlyVisiblePart)
{
    start(1);
    action("zoom200")->trigger(); // Quelle 480 × 320 → 960 × 640, breiter als die Arbeitsfläche
    const auto before = window->previewsShown();
    window->canvas()->setCenter({0.25, 0.5});
    ASSERT_TRUE(waitFor([&] { return window->previewsShown() > before; }));
    const auto matches = [this] {
        const QRectF vis = window->canvas()->visibleNorm();
        return vis.width() < 1.0 &&
               std::abs(window->canvas()->image().width() - 2.0 * vis.width() * 480.0) <= 4.0;
    };
    ASSERT_TRUE(waitFor(matches)) << window->canvas()->image().width() << " "
                                  << window->canvas()->visibleNorm().width();
    EXPECT_LT(window->canvas()->image().width(), 960);
}

// LF-UI-02: stufenloser Zoom mit Strg+Mausrad (um die Stelle unter der Maus), Strg+Plus/Minus,
// Regler und Prozentfeld in der Statusleiste; Ziehen verschiebt, Taste in den Einstellungen wählbar
TEST_F(E2E, M10_FreeZoomAndPan)
{
    start(1);
    ui::ImageCanvas* c = window->canvas();
    auto* overlay = c->findChild<QWidget*>();
    ASSERT_NE(overlay, nullptr);
    auto* percent = window->findChild<QSpinBox*>(QStringLiteral("zoomPercent"));
    auto* slider = window->findChild<QSlider*>(QStringLiteral("zoomSlider"));
    ASSERT_NE(percent, nullptr);
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(window->findChild<QWidget*>(QStringLiteral("zoomBar"))->isVisibleTo(window));

    // Strg+Mausrad: die Bildstelle unter der Maus bleibt stehen (Bild größer als die Fläche)
    c->setScale(3.0);
    const QPointF at(c->width() * 0.3, c->height() * 0.4);
    const QPointF normBefore = c->mapping().toNorm(at);
    QWheelEvent wheel(at, overlay->mapToGlobal(at), {}, {0, 120}, Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(overlay, &wheel);
    EXPECT_NEAR(c->scale(), 3.75, 1e-9);
    EXPECT_EQ(c->zoom(), ui::ImageCanvas::Zoom::Custom);
    const QPointF normAfter = c->mapping().toNorm(at);
    EXPECT_NEAR(normAfter.x(), normBefore.x(), 0.01);
    EXPECT_NEAR(normAfter.y(), normBefore.y(), 0.01);
    EXPECT_EQ(percent->value(), 375);

    // Rad ohne Strg zoomt nicht
    QWheelEvent plain(at, overlay->mapToGlobal(at), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(overlay, &plain);
    EXPECT_NEAR(c->scale(), 3.75, 1e-9);

    // Kürzel und Prozentfeld
    action("zoomIn")->trigger();
    EXPECT_NEAR(c->scale(), 4.6875, 1e-9);
    action("zoomOut")->trigger();
    EXPECT_NEAR(c->scale(), 3.75, 1e-9);
    percent->setValue(300);
    EXPECT_NEAR(c->scale(), 3.0, 1e-9);
    const int sliderAt300 = slider->value();
    slider->setValue(sliderAt300 - 100);
    EXPECT_LT(c->scale(), 3.0);
    EXPECT_EQ(percent->value(), static_cast<int>(std::lround(c->scale() * 100)));

    // Ziehen mit der linken Taste verschiebt den Ausschnitt
    percent->setValue(300);
    const QPointF center = c->center();
    const QPoint from(c->width() / 2, c->height() / 2);
    QTest::mousePress(overlay, Qt::LeftButton, {}, from);
    QTest::mouseMove(overlay, from + QPoint(60, 0));
    QTest::mouseRelease(overlay, Qt::LeftButton, {}, from + QPoint(60, 0));
    EXPECT_LT(c->center().x(), center.x());

    // Einstellung: rechte Taste zieht, linke nicht mehr
    c->setPanButton(Qt::RightButton);
    const QPointF c2 = c->center();
    QTest::mousePress(overlay, Qt::LeftButton, {}, from);
    QTest::mouseMove(overlay, from + QPoint(60, 0));
    QTest::mouseRelease(overlay, Qt::LeftButton, {}, from + QPoint(60, 0));
    EXPECT_EQ(c->center(), c2);
    QTest::mousePress(overlay, Qt::RightButton, {}, from);
    QTest::mouseMove(overlay, from + QPoint(60, 0));
    QTest::mouseRelease(overlay, Qt::RightButton, {}, from + QPoint(60, 0));
    EXPECT_LT(c->center().x(), c2.x());

    // Grenzen 2 … 800 %; Einpassen nur ausdrücklich
    c->setScale(0.0001);
    EXPECT_NEAR(c->scale(), ui::ImageCanvas::kMinScale, 1e-9);
    c->setScale(100.0);
    EXPECT_NEAR(c->scale(), ui::ImageCanvas::kMaxScale, 1e-9);
    action("zoomFit")->trigger();
    EXPECT_EQ(c->zoom(), ui::ImageCanvas::Zoom::Fit);
}

// Film-Farbprofil (Wunsch des Nutzers): Werte als Regler mit Zahl in „Licht und Farbe“, RGB/CMY,
// eigenes Profil speichern (dauerhaft, für alle Rollen) und in der Konvertierung wählen
TEST_F(E2E, M10_FilmProfileSlidersAndSave)
{
    start(1);
    model::EditEntry levels;
    levels.step = model::StepId("autoneg.densityrange");
    levels.params = model::ParamSet{}
                        .with("dMin", base::Vec3{0.2, 0.3, 0.4})
                        .with("dMax", base::Vec3{1.6, 1.8, 2.0});
    ASSERT_TRUE(window->controller().apply(std::make_unique<application::SetEntryCommand>(levels)));
    auto* panel = window->findChild<ui::FilmProfilePanel*>(QStringLiteral("filmProfilePanel"));
    ASSERT_NE(panel, nullptr);
    const auto slider = [&](const char* name) {
        auto* s = panel->findChild<ui::NumberSlider*>(QString::fromLatin1(name));
        EXPECT_NE(s, nullptr) << name;
        return s;
    };
    // Gesamt = Mittel der Kanäle; RGB: Rot hat die niedrigste Grenze = mehr Rot (+0,1)
    EXPECT_NEAR(slider("profileBlack")->value(), 0.3, 1e-9);
    EXPECT_NEAR(slider("profileWhite")->value(), 1.8, 1e-9);
    EXPECT_NEAR(slider("profileBlackR")->value(), 0.1, 1e-9);
    EXPECT_NEAR(slider("profileBlackB")->value(), -0.1, 1e-9);
    panel->setCmy(true); // Cyan ist der Rotkanal: weniger Cyan
    EXPECT_NEAR(slider("profileBlackR")->value(), -0.1, 1e-9);
    panel->setCmy(false);

    // Regler ändern: Eintrag folgt (mehr Rot in den Lichtern = niedrigere Weißgrenze Rot)
    slider("profileWhiteR")->spin()->setValue(0.3);
    Q_EMIT slider("profileWhiteR")->spin()->editingFinished();
    const auto hi = entry("autoneg.densityrange")->params.get<base::Vec3>("dMax").value();
    EXPECT_NEAR(hi.x, 1.5, 1e-9);
    EXPECT_NEAR(hi.y, 1.8, 1e-9);

    // speichern: erscheint in beiden Auswahlen und bleibt nach dem Neustart
    answerName(QStringLiteral("Portra 160"));
    panel->findChild<QPushButton*>(QStringLiteral("filmProfileSave"))->click();
    auto* conv = window->findChild<QComboBox*>(QStringLiteral("filmProfile"));
    ASSERT_NE(conv, nullptr);
    EXPECT_GE(conv->findText(QStringLiteral("Portra 160")), 0);
    EXPECT_EQ(conv->currentText(), QStringLiteral("Portra 160"));
    EXPECT_EQ(entry("autoneg.densityrange")->params.get<std::string>("profile").value_or(""), "Portra 160");
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; }));
    conv = window->findChild<QComboBox*>(QStringLiteral("filmProfile"));
    EXPECT_GE(conv->findText(QStringLiteral("Portra 160")), 0);
    EXPECT_EQ(conv->currentText(), QStringLiteral("Portra 160")); // zuletzt gewählt
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

// K-M10-12 (Wunsch des Nutzers): beim Import einzelne Scans abwählen; sie gehören danach nicht
// zur Rolle, auch nicht nach dem Wiederöffnen
TEST_F(E2E, M10_ImportWithoutDeselectedScans)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 4);
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
    QListView* preview = window->importPage()->preview();
    EXPECT_EQ(preview->selectionModel()->selectedIndexes().size(), 4); // anfangs alle gewählt
    preview->selectionModel()->select(preview->model()->index(1, 0), QItemSelectionModel::Deselect);
    importAs(model::FilmType::ColorNegative);
    EXPECT_EQ(window->filmstrip()->model()->rowCount(), 3);
    std::ifstream rollFile(rollDir / "roll.autoneg.json");
    const std::string rollJson((std::istreambuf_iterator<char>(rollFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(rollJson.find("SCAN0002.tif"), std::string::npos) << rollJson;
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->rollOpen(); }));
    EXPECT_EQ(window->filmstrip()->model()->rowCount(), 3);
    EXPECT_TRUE(fs::exists(rollDir / "SCAN0002.tif")); // der Scan selbst bleibt unberührt
}

// K-M10-12 (Wunsch des Nutzers): Reiter Leuchttisch/Entwickeln schließen und über Fenster wieder
// öffnen; Sterne und Verwerfen am Filmstreifen; Einstellungen mit einem Klick auf alle
// übernehmen (ohne Zuschnitt); Export aller Bilder vom Leuchttisch
TEST_F(E2E, M10_LightTableTabsRatingApplyAllAndExport)
{
    start(3);
    QTabBar* tabs = window->viewTabs();
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("Leuchttisch"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("Entwickeln"));
    window->closeView(ui::MainWindow::View::Image);
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(window->view(), ui::MainWindow::View::Overview);
    window->closeView(ui::MainWindow::View::Overview);
    EXPECT_EQ(window->view(), ui::MainWindow::View::None);
    action("viewImage")->trigger();
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(window->view(), ui::MainWindow::View::Image);
    action("viewOverview")->trigger();
    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("Leuchttisch"));
    action("viewImage")->trigger();

    // Sterne und Verwerfen für das aktive Bild
    const model::FrameId first = window->filmstrip()->stripModel()->itemAt(0)->frame.id;
    window->filmstrip()->selectFrame(first);
    window->findChild<QToolButton*>(QStringLiteral("stars3"))->click();
    EXPECT_EQ(window->filmstrip()->stripModel()->itemAt(0)->frame.stars, 3);
    window->findChild<QToolButton*>(QStringLiteral("rejectButton"))->click();
    EXPECT_EQ(window->filmstrip()->stripModel()->itemAt(0)->frame.rating, model::Rating::Reject);

    // Belichtung und Zuschnitt am ersten Bild, dann auf alle übernehmen
    Q_EMIT window->adjustPanel()->slider("autoneg.exposure", "ev")->committed(0.7);
    model::EditEntry crop;
    crop.step = model::StepId("autoneg.crop");
    crop.params = model::ParamSet{}.with("rect", model::NormRect{0.1, 0.1, 0.8, 0.8});
    window->controller().apply(std::make_unique<application::SetEntryCommand>(crop));
    action("copy")->trigger(); // ohne Markierung in der Liste: alle Bearbeitungsoptionen
    window->findChild<QPushButton*>(QStringLiteral("applyToAll"))->click();
    for (int r = 1; r < 3; ++r) {
        window->selectFrame(window->filmstrip()->stripModel()->itemAt(r)->frame.id);
        ASSERT_NE(entry("autoneg.exposure"), nullptr) << r;
        EXPECT_EQ(entry("autoneg.exposure")->origin, model::Origin::Copied);
        ASSERT_NE(entry("autoneg.crop"), nullptr) << "manueller Zuschnitt wird mitkopiert";
    }

    // Leuchttisch: alle exportieren, ohne Dialog
    action("viewOverview")->trigger();
    const fs::path target = rollDir / "export";
    window->findChild<QPushButton*>(QStringLiteral("exportAll"))->click();
    ASSERT_TRUE(waitFor(
        [&] {
            std::error_code ec;
            return fs::exists(target, ec) &&
                   std::distance(fs::directory_iterator(target), fs::directory_iterator()) == 3;
        },
        60000));
}

// K-M10-15 (Wunsch des Nutzers): Filmstreifen wie ein Explorer-Fenster – Klick wählt aus, ohne
// zu öffnen; das Kästchen oben rechts schaltet ein Bild dazu; erst der Doppelklick öffnet
TEST_F(E2E, M10_FilmstripSelectsLikeExplorer)
{
    start(3);
    ui::FilmstripView* strip = window->filmstrip();
    const model::FrameId opened = window->controller().session().current;
    const QModelIndex second = strip->model()->index(1, 0);
    const QModelIndex third = strip->model()->index(2, 0);
    QTest::mouseClick(strip->viewport(), Qt::LeftButton, {}, strip->visualRect(second).center());
    EXPECT_EQ(window->controller().session().current, opened) << "Klick öffnet nicht";
    EXPECT_EQ(strip->selectedFrames().size(), 1U);
    const QRect cell = strip->visualRect(third).adjusted(2, 2, -2, -2);
    QTest::mouseClick(strip->viewport(), Qt::LeftButton, {},
                      ui::ThumbnailDelegate::checkBoxRect(cell).center());
    EXPECT_EQ(strip->selectedFrames().size(), 2U) << "Kästchen wählt dazu";
    Q_EMIT strip->doubleClicked(third);
    EXPECT_EQ(window->controller().session().current, strip->stripModel()->itemAt(2)->frame.id);
}

// K-M10-15 (Fehler des Nutzers: Auto-Basis ging nicht, beide Basis-Schalter gleichzeitig an):
// Auto-Basis und Basis messen schließen sich aus; Auto-Basis ersetzt auch eine gemessene Basis
TEST_F(E2E, M10_AutoBaseAndPickBaseExclusive)
{
    start(2);
    auto* autoBase = window->findChild<QPushButton*>(QStringLiteral("autoBase"));
    auto* pickBase = window->findChild<QPushButton*>(QStringLiteral("pickBase"));
    // gemessene Basis wie nach "Basis messen"
    model::EditEntry manual;
    manual.step = model::StepId("autoneg.filmbase");
    manual.params =
        model::ParamSet{}.with("base", base::Vec3{0.5, 0.3, 0.2}).with("source", std::string("manual"));
    window->controller().apply(std::make_unique<application::SetEntryCommand>(manual));
    ASSERT_TRUE(pickBase->isChecked());
    EXPECT_FALSE(autoBase->isChecked());
    autoBase->click();
    ASSERT_TRUE(waitFor(
        [&] {
            const model::EditEntry* e = entry("autoneg.filmbase");
            return e != nullptr && e->origin == model::Origin::Automatic;
        },
        30000))
        << "Auto-Basis ersetzt die gemessene Basis";
    EXPECT_TRUE(autoBase->isChecked());
    EXPECT_FALSE(pickBase->isChecked());
    pickBase->click(); // Basis messen: automatische Basis weg, nur dieser Schalter an
    EXPECT_EQ(entry("autoneg.filmbase"), nullptr);
    EXPECT_TRUE(pickBase->isChecked());
    EXPECT_FALSE(autoBase->isChecked());
    pickBase->click(); // abwählen
    EXPECT_FALSE(pickBase->isChecked());
    EXPECT_FALSE(autoBase->isChecked());
}

// LF-EDT-06, U-STP-15 (Fehler: die Weißabgleich-Pipette war nicht angeschlossen): Knopf, dann
// Klick ins Bild setzt Farbtemperatur und Tönung; ein zweiter Klick an derselben Stelle ändert
// kaum noch etwas
TEST_F(E2E, M10_WhitePickerSetsWhiteBalance)
{
    start(1, model::FilmType::Slide);
    ASSERT_EQ(entry("autoneg.whitebalance"), nullptr);
    window->findChild<QPushButton*>(QStringLiteral("whitePicker"))->click();
    const auto before = window->previewsShown();
    Q_EMIT window->canvas()->picked(QPointF(0.5, 0.5));
    const model::EditEntry* wb = entry("autoneg.whitebalance");
    ASSERT_NE(wb, nullptr) << "Pipette setzt den Weißabgleich";
    const double t1 = wb->params.get<double>("temperature").value_or(0.0);
    const double u1 = wb->params.get<double>("tint").value_or(0.0);
    EXPECT_GT(std::abs(t1) + std::abs(u1), 1.0);
    ASSERT_TRUE(waitFor([&] { return window->previewsShown() > before; }));
    window->findChild<QPushButton*>(QStringLiteral("whitePicker"))->click();
    Q_EMIT window->canvas()->picked(QPointF(0.5, 0.5));
    wb = entry("autoneg.whitebalance");
    const double t2 = wb->params.get<double>("temperature").value_or(0.0);
    const double u2 = wb->params.get<double>("tint").value_or(0.0);
    EXPECT_LT(std::abs(t2 - t1) + std::abs(u2 - u1), 0.5 * (std::abs(t1) + std::abs(u1)))
        << t1 << " " << u1 << " -> " << t2 << " " << u2;
    // ohne Knopf bleibt ein Klick ins Bild wirkungslos
    Q_EMIT window->canvas()->picked(QPointF(0.2, 0.2));
    EXPECT_EQ(entry("autoneg.whitebalance")->params.get<double>("temperature").value_or(0.0), t2);
}

// Fehler: nach Auto-Bildfeld, Auto-Basis und Konvertieren von Hand sprang die Konvertierung auf
// "Autokonvertierung" um und versteckte die Schalter (auch nach erneutem Öffnen der Rolle)
TEST_F(E2E, M10_ManualStepsKeepManualMode)
{
    start(1);
    auto* autoConvert = window->findChild<QCheckBox*>(QStringLiteral("autoConvert"));
    const auto click = [this](const char* name) {
        window->findChild<QPushButton*>(QString::fromLatin1(name))->click();
    };
    click("autoFrame");
    ASSERT_TRUE(waitFor([&] { return entry("autoneg.crop") != nullptr; }, 30000));
    click("autoBase");
    ASSERT_TRUE(waitFor([&] { return entry("autoneg.filmbase") != nullptr; }, 30000));
    click("convert");
    ASSERT_TRUE(waitFor([&] { return entry("autoneg.convert") != nullptr; }, 30000));
    EXPECT_FALSE(autoConvert->isChecked());
    EXPECT_TRUE(window->findChild<QPushButton*>(QStringLiteral("convert"))->isVisibleTo(window));
    restart();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; }));
    EXPECT_FALSE(window->findChild<QCheckBox*>(QStringLiteral("autoConvert"))->isChecked());
}

// Fehler: nach Öffnen und Importieren ohne weitere Eingabe lief die Autokonvertierung über alle Bilder,
// weil „Automatisch konvertieren“ vorab angehakt war (NF-USE: nichts ändern, was der Nutzer nicht auslöst)
TEST_F(E2E, M10_ImportChangesNothingByDefault)
{
    rollDir = tmp.path() / "rolle";
    writeRoll(rollDir, 2);
    launch();
    window->openFolder(rollDir);
    ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
    window->importPage()->choose(model::FilmType::ColorNegative);
    EXPECT_FALSE(window->importPage()->findChild<QCheckBox*>(QStringLiteral("detectNow"))->isChecked());
    window->importPage()->importButton()->click();
    ASSERT_TRUE(waitFor([this] { return window->previewsShown() > 0; }));
    QTest::qWait(1500); // eine Automatik hätte ihre Einträge inzwischen gesetzt
    EXPECT_TRUE(list().entries().empty());
    EXPECT_FALSE(window->findChild<QCheckBox*>(QStringLiteral("autoConvert"))->isChecked());
}

// LF-UI-01: eine laufende Automatik zeigt am Schalter ein drehendes Symbol und sperrt ihn bis zum Ende
TEST_F(E2E, M10_AutomaticShowsBusyButton)
{
    start(1);
    auto* button = window->findChild<QPushButton*>(QStringLiteral("autoBase"));
    button->click();
    EXPECT_TRUE(window->conversionPanel()->busy(QStringLiteral("autoBase")));
    EXPECT_FALSE(button->isEnabled());
    // sofort, nicht erst beim ersten Takt: schnelle Automatiken zeigten sonst nie ein Symbol (Linux Release)
    EXPECT_FALSE(button->icon().isNull());
    ASSERT_TRUE(waitFor([&] { return !window->conversionPanel()->busy(QStringLiteral("autoBase")); }, 30000));
    EXPECT_TRUE(button->isEnabled());
    EXPECT_TRUE(button->icon().isNull());
    EXPECT_NE(entry("autoneg.filmbase"), nullptr);
}

// Fehler: "Messbereich anzeigen" ohne gemessene Basis blieb angehakt und zeigte den ganzen Scan
TEST_F(E2E, M10_ShowAreaWithoutBaseStaysOff)
{
    start(1);
    auto* show = window->findChild<QCheckBox*>(QStringLiteral("showBaseArea"));
    ASSERT_NE(show, nullptr);
    show->click();
    EXPECT_FALSE(show->isChecked());
    EXPECT_EQ(window->canvas()->tool(), nullptr);
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
        // K-M10-15: Standard unbegrenzt; Haken raus stellt die Hälfte des Arbeitsspeichers ein
        auto* unlimited = d->findChild<QCheckBox*>(QStringLiteral("memoryUnlimited"));
        auto* memory = d->findChild<QSpinBox*>(QStringLiteral("memoryMb"));
        EXPECT_TRUE(unlimited->isChecked());
        EXPECT_FALSE(memory->isEnabled());
        unlimited->click();
        EXPECT_TRUE(memory->isEnabled());
        memory->setValue(2048);
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

// LF-UI-07: Strg+Pfeil wechselt das Bild; Tastenkürzel sind in den Einstellungen frei belegbar,
// doppelte Belegung sperrt OK, die Belegung bleibt über einen Neustart erhalten
TEST_F(E2E, M10_ImageNavigationAndCustomShortcuts)
{
    start(3);
    const model::FrameId first = window->controller().session().current;
    EXPECT_EQ(action("nextImage")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Right));
    action("prevImage")->trigger(); // am Anfang: bleibt
    EXPECT_EQ(window->controller().session().current, first);
    action("nextImage")->trigger();
    action("nextImage")->trigger();
    action("nextImage")->trigger(); // am Ende: bleibt beim dritten
    const model::FrameId third = window->controller().session().current;
    EXPECT_NE(third, first);
    action("prevImage")->trigger();
    action("prevImage")->trigger();
    EXPECT_EQ(window->controller().session().current, first);

    onNextDialog([](QDialog* d) {
        auto* next = d->findChild<QKeySequenceEdit*>(QStringLiteral("key_nextImage"));
        auto* undo = d->findChild<QKeySequenceEdit*>(QStringLiteral("key_undo"));
        ASSERT_NE(next, nullptr);
        auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        next->setKeySequence(undo->keySequence()); // doppelt: OK gesperrt
        EXPECT_FALSE(ok->isEnabled());
        EXPECT_FALSE(d->findChild<QLabel*>(QStringLiteral("shortcutConflict"))->text().isEmpty());
        next->setKeySequence(QKeySequence(Qt::Key_N));
        EXPECT_TRUE(ok->isEnabled());
        ok->click();
    });
    action("settings")->trigger();
    EXPECT_EQ(action("nextImage")->shortcut(), QKeySequence(Qt::Key_N));
    restart();
    EXPECT_EQ(action("nextImage")->shortcut(), QKeySequence(Qt::Key_N));
    EXPECT_EQ(action("prevImage")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Left));
    onNextDialog([](QDialog* d) {
        d->findChild<QPushButton*>(QStringLiteral("shortcutsReset"))->click();
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action("settings")->trigger();
    EXPECT_EQ(action("nextImage")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Right));
}

// LF-EDT-01: die Drehung eines Bilds lässt sich auf die ganze Rolle übernehmen
TEST_F(E2E, M10_ApplyRotationToAll)
{
    start(3);
    action("rotateRight")->trigger();
    ASSERT_NE(entry("autoneg.orientation"), nullptr);
    action("orientAll")->trigger();
    for (int r = 0; r < 3; ++r) {
        window->selectFrame(window->filmstrip()->stripModel()->itemAt(r)->frame.id);
        ASSERT_NE(entry("autoneg.orientation"), nullptr) << r;
        EXPECT_EQ(entry("autoneg.orientation")->params.get<int>("quarterTurns"), 1) << r;
    }
    action("rotateLeft")->trigger(); // zurück auf neutral: übernehmen entfernt die Drehung überall
    action("orientAll")->trigger();
    for (int r = 0; r < 3; ++r) {
        window->selectFrame(window->filmstrip()->stripModel()->itemAt(r)->frame.id);
        EXPECT_EQ(entry("autoneg.orientation"), nullptr) << r;
    }
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

// LF-EXP-01: Zielordner ist standardmäßig <Importordner>/export, auch nach Wechsel der Rolle;
// die Anzeige kürzt lange Pfade, der Tooltip zeigt den vollen Pfad
TEST_F(E2E, M10_ExportFolderFollowsImportFolder)
{
    start(2);
    action("viewOverview")->trigger();
    auto* label = window->findChild<QLabel*>(QStringLiteral("exportFolder"));
    ASSERT_NE(label, nullptr);
    const auto shown = [label] { return fs::path(label->toolTip().toStdU16String()); };
    EXPECT_EQ(shown(), rollDir / "export");

    const fs::path second = tmp.path() / "zweite Rolle mit einem recht langen Ordnernamen";
    writeRoll(second, 2, model::FilmType::ColorNegative);
    window->openFolder(second);
    ASSERT_TRUE(waitFor([this] { return window->view() == ui::MainWindow::View::Import; }));
    importAs(model::FilmType::ColorNegative);
    action("viewOverview")->trigger();
    EXPECT_EQ(shown(), second / "export");
    EXPECT_TRUE(label->text().contains(QStringLiteral("export")));
}

} // namespace
} // namespace autoneg
