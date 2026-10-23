#include "autoneg/ui/gui_application.hpp"

#include "autoneg/ui/main_window.hpp"
#include "autoneg/ui/qt_session_bridge.hpp"
#include "autoneg/ui/translations.hpp"

#include <QtCore/QMetaObject>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QApplication>

namespace autoneg::ui {

struct GuiApplication::Impl
{
    Impl(int& argc, char** argv)
        : app(argc, argv)
    {}
    QApplication app;
    QtSessionBridge bridge;
    std::unique_ptr<TableTranslator> translator;
    std::unique_ptr<MainWindow> window;
};

GuiApplication::GuiApplication(int& argc, char** argv)
{
    QApplication::setApplicationName(QStringLiteral("AutoNeg"));
    QApplication::setOrganizationName(QStringLiteral("AutoNeg"));
    impl_ = std::make_unique<Impl>(argc, argv);
}

GuiApplication::~GuiApplication()
{
    impl_->window.reset(); // Fenster vor Brücke und QApplication
}

void GuiApplication::setLanguage(const std::string& language)
{
    if (impl_->translator) {
        QCoreApplication::removeTranslator(impl_->translator.get());
        impl_->translator.reset();
    }
    if (language == "en") {
        impl_->translator = std::make_unique<TableTranslator>();
        QCoreApplication::installTranslator(impl_->translator.get());
    }
}

application::RenderService::ReadyHandler GuiApplication::previewSink()
{
    QtSessionBridge* bridge = &impl_->bridge;
    return [bridge](std::uint64_t seq, const pipeline::RenderRequest& r, image::ImageBuffer img) {
        bridge->deliverPreview(seq, r.frame, img, r.profile.mode == pipeline::RenderMode::Detail);
    };
}

application::UiDispatcher GuiApplication::dispatcher()
{
    QApplication* app = &impl_->app;
    return [app](std::function<void()> f) {
        QMetaObject::invokeMethod(app, std::move(f), Qt::QueuedConnection);
    };
}

std::filesystem::path GuiApplication::configDir()
{
    // %APPDATA%\AutoNeg bzw. ~/.config/autoneg (plattformabhängig über QStandardPaths)
    return std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdU16String());
}

std::filesystem::path GuiApplication::cacheDir()
{
    return std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdU16String());
}

MainWindow& GuiApplication::createWindow(const GuiServices& s)
{
    impl_->window = std::make_unique<MainWindow>(AppServices{
        s.registry, s.jobs,        s.bus,        s.session,        s.sync,         s.clipboard,  s.render,
        s.rolls,    s.thumbnails,  s.analysis,   s.exports,        s.contactSheet, s.color,      s.presets,
        s.settings, impl_->bridge, s.sourceInfo, s.currentChanged, s.encodeEdits,  s.decodeEdits});
    return *impl_->window;
}

void GuiApplication::destroyWindow()
{
    if (impl_->window) {
        impl_->window->close();
        impl_->window.reset();
        QCoreApplication::processEvents(); // ausstehende Signale an das alte Fenster verwerfen
    }
}

int GuiApplication::run(const GuiServices& s, const std::optional<std::filesystem::path>& folder)
{
    MainWindow& w = createWindow(s);
    w.show();
    if (folder) {
        w.openFolder(*folder);
    }
    return QApplication::exec();
}

} // namespace autoneg::ui
