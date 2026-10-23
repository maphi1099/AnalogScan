// Kompositionswurzel der grafischen Anwendung: Qt-Anwendung, Adapter und Dienste anlegen,
// Hauptfenster zeigen.
//
// Aufruf: autoneg [--config <ordner>] [--cache <ordner>] [<scan-ordner>]
//   --config/--cache ersetzen die Benutzerverzeichnisse (Tests, Probeläufe).

#include "autoneg/app/composition.hpp"

#include <optional>
#include <span>
#include <string_view>

int main(int argc, char** argv)
{
    autoneg::ui::GuiApplication gui(argc, argv);
    autoneg::app::Directories dirs{autoneg::ui::GuiApplication::configDir(),
                                   autoneg::ui::GuiApplication::cacheDir()};
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
    autoneg::app::Composition app(gui, dirs);
    gui.setLanguage(app.language());
    return gui.run(app.services(), folder);
}
