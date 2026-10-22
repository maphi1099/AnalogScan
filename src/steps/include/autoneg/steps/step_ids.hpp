#pragma once

#include "autoneg/model/ids.hpp"
#include "autoneg/model/param_set.hpp"

#include <string>
#include <string_view>

namespace autoneg::steps {

/// Kennungen der Schritte (NF-ARC-09): einmal vergeben, nie ändern (U-STP-03).
namespace id {
inline constexpr std::string_view flatfield = "autoneg.flatfield";
inline constexpr std::string_view orientation = "autoneg.orientation";
inline constexpr std::string_view rotation = "autoneg.rotation";
inline constexpr std::string_view crop = "autoneg.crop";
inline constexpr std::string_view retouch = "autoneg.retouch";
inline constexpr std::string_view filmmode = "autoneg.filmmode";
inline constexpr std::string_view bwmix = "autoneg.bwmix";
inline constexpr std::string_view filmbase = "autoneg.filmbase";
inline constexpr std::string_view density = "autoneg.density";
inline constexpr std::string_view densityrange = "autoneg.densityrange";
inline constexpr std::string_view exposure = "autoneg.exposure";
inline constexpr std::string_view whitebalance = "autoneg.whitebalance";
inline constexpr std::string_view gradation = "autoneg.gradation";
inline constexpr std::string_view colorgrade = "autoneg.colorgrade";
inline constexpr std::string_view shadowshighlights = "autoneg.shadowshighlights";
inline constexpr std::string_view contrast = "autoneg.contrast";
inline constexpr std::string_view tonecurve = "autoneg.tonecurve";
inline constexpr std::string_view saturation = "autoneg.saturation";
inline constexpr std::string_view denoise = "autoneg.denoise";
inline constexpr std::string_view sharpen = "autoneg.sharpen";
} // namespace id

/// Ränge der festen Stufenfolge (Architektur 5.2). Lücken lassen Platz für spätere Stufen.
namespace rank {
inline constexpr int flatfield = 10;
inline constexpr int orientation = 20;
inline constexpr int rotation = 30;
inline constexpr int crop = 40;
inline constexpr int retouch = 50;
inline constexpr int filmmode = 60;
inline constexpr int bwmix = 70;
inline constexpr int filmbase = 80;
inline constexpr int density = 90;
inline constexpr int densityrange = 100;
inline constexpr int exposure = 110;
inline constexpr int whitebalance = 120;
inline constexpr int gradation = 130;
inline constexpr int colorgrade = 140;
inline constexpr int shadowshighlights = 150;
inline constexpr int contrast = 160;
inline constexpr int tonecurve = 170;
inline constexpr int saturation = 180;
inline constexpr int denoise = 190;
inline constexpr int sharpen = 200;
} // namespace rank

[[nodiscard]] inline model::StepId stepId(std::string_view id)
{
    return model::StepId(std::string(id));
}

/// Parameter oder Ersatzwert (fehlender Parameter = Neutral- bzw. Standardwert).
template <class T>
[[nodiscard]] T param(const model::ParamSet& p, std::string_view name, T fallback)
{
    return p.get<T>(name).value_or(std::move(fallback));
}

} // namespace autoneg::steps
