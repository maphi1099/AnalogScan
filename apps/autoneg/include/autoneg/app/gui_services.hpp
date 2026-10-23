#pragma once

// Dienste der gemeinsamen Komposition für die Oberfläche (GUI-Programm und E2E-Tests).

#include "autoneg/app/composition.hpp"
#include "autoneg/ui/gui_application.hpp"

namespace autoneg::app {

[[nodiscard]] inline ui::GuiServices guiServices(Composition& c)
{
    return ui::GuiServices{
        c.registry(),
        c.jobs(),
        c.bus(),
        c.session(),
        c.sync(),
        c.clipboard(),
        c.render(),
        c.rolls(),
        c.thumbnails(),
        c.analysis(),
        c.exports(),
        c.contactSheet(),
        c.color(),
        c.presets(),
        c.settings(),
        [&c](const model::Frame& f) { return c.sourceInfo(f); },
        [&c](const model::FrameId& f) { c.setCurrent(f); },
        [&c](const model::EditList& l) { return c.encodeEdits(l); },
        [&c](const model::JsonValue& j) { return c.decodeEdits(j); },
        &c.logger(),
    };
}

} // namespace autoneg::app
