#pragma once

#include <string_view>

namespace autoneg::application {

/// Name des Moduls. Platzhalter des Projektgerüsts (M0), wird mit den echten Klassen ersetzt.
[[nodiscard]] std::string_view moduleName() noexcept;

} // namespace autoneg::application
