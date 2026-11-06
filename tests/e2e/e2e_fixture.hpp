#pragma once

// Gemeinsame Hilfen der E2E-Tests: eine Qt-Anwendung je Testprogramm (ohne Bildschirm, Plattform
// offscreen; mit AUTONEG_E2E_NATIVE die echte Plattform), künstliche Rollen und die Fixture E2E.

#include "adapters/oiio/oiio_image_io.hpp"
#include "adapters/qtprint/qt_document_renderer.hpp"
#include "autoneg/composition.hpp"
#include "autoneg/gui_services.hpp"
#include "support/synthetic.hpp"
#include "support/temp_dir.hpp"
#include "ui/gui_application.hpp"
#include "ui/import_page.hpp"
#include "ui/main_window.hpp"
#include "ui/views.hpp"

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QPushButton>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>

namespace autoneg {
namespace {

void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value); // plattformabhängig (PROGRESS.md)
#else
    setenv(name, value, 1);
#endif
}

namespace fs = std::filesystem;
using ui::MainWindow;

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
#if !defined(AUTONEG_E2E_NATIVE)
        setEnv("QT_QPA_PLATFORM", "offscreen");
#endif
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
testing::NegativeSpec specFor(int i, model::FilmType film, int width = 480, int height = 320)
{
    testing::NegativeSpec spec;
    spec.width = width;
    spec.height = height;
    spec.film = film;
    spec.seed = static_cast<std::uint64_t>(i);
    // jedes Bild anders (Staub, Lage des Bildfelds): sonst gleiche Prüfsumme = gleiche FrameId
    spec.dust = 1 + i % 4;
    spec.frame.x = 0.1 + 0.002 * i;
    return spec;
}

/// Schreibt n künstliche Negative als TIFF.
void writeRoll(const fs::path& dir, int n, model::FilmType film = model::FilmType::ColorNegative, int width = 480,
               int height = 320)
{
    fs::create_directories(dir);
    adapter::oiio::OiioImageWriter writer;
    for (int i = 1; i <= n; ++i) {
        const auto neg = testing::makeNegative(specFor(i, film, width, height));
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
    void start(int frames = 6, model::FilmType film = model::FilmType::ColorNegative, int width = 480,
               int height = 320)
    {
        rollDir = tmp.path() / "rolle";
        writeRoll(rollDir, frames, film, width, height);
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
        // ohne „Automatisch konvertieren“: die Tests setzen ihre Einträge selbst (T-22)
        window->importPage()->findChild<QCheckBox*>(QStringLiteral("detectNow"))->setChecked(false);
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

} // namespace
} // namespace autoneg
