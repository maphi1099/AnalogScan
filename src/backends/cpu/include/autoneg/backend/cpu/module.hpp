#pragma once

#include <string_view>

namespace autoneg::backend::cpu {

/// Name des Moduls. Platzhalter des Projektgerüsts (M0), wird mit den echten Klassen ersetzt.
[[nodiscard]] std::string_view moduleName() noexcept;

} // namespace autoneg::backend::cpu
