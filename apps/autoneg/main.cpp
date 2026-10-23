// Kompositionswurzel der grafischen Anwendung: Qt-Anwendung, gemeinsame Komposition,
// Druck-Adapter, Hauptfenster.
//
// Aufruf: autoneg [--config <ordner>] [--cache <ordner>] [<scan-ordner>]
//   --config/--cache ersetzen die Benutzerverzeichnisse (Tests, Probeläufe).

#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"
#include "autoneg/app/composition.hpp"
#include "autoneg/app/gui_services.hpp"

#include <optional>
#include <span>
#include <string_view>

int main(int argc, char** argv)
{
    autoneg::ui::GuiApplication gui(argc, argv);
    autoneg::app::Directories dirs = autoneg::app::userDirectories();
    std::optional<std::filesystem::path> folder;
    const std::span args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "--config" && i + 1 < args.size()) {
            dirs.config = args[++i];
        } else if (a == "--cache" && i + 1 < args.size()) {
            dirs.cache = args[++i];
        } else {
            folder = std::filesystem::path(a);
        }
    }
    autoneg::adapter::qtprint::QtDocumentRenderer document;
    autoneg::app::Composition app(dirs, gui.previewSink(), gui.dispatcher(), document);
    gui.setLanguage(app.language());
    return gui.run(autoneg::app::guiServices(app), folder);
}
