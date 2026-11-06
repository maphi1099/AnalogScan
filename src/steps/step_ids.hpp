#pragma once

#include "model/ids.hpp"
#include "model/param_set.hpp"

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
inline constexpr std::string_view convert = "autoneg.convert";
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

/// Parameternamen der Schritte (gespeichert in den Begleitdateien: nie ändern).
namespace key {
inline constexpr std::string_view amount = "amount";
inline constexpr std::string_view angle = "angle";
inline constexpr std::string_view area = "area";
inline constexpr std::string_view aspect = "aspect";
inline constexpr std::string_view autoFill = "autoFill";
inline constexpr std::string_view automatic = "auto";
inline constexpr std::string_view base = "base";
inline constexpr std::string_view black = "black";
inline constexpr std::string_view clip = "clip";
inline constexpr std::string_view compensate = "compensate";
inline constexpr std::string_view curveB = "b";
inline constexpr std::string_view curveG = "g";
inline constexpr std::string_view curveMaster = "master";
inline constexpr std::string_view curveR = "r";
inline constexpr std::string_view dMax = "dMax";
inline constexpr std::string_view dMin = "dMin";
inline constexpr std::string_view ev = "ev";
inline constexpr std::string_view flipH = "flipH";
inline constexpr std::string_view flipV = "flipV";
inline constexpr std::string_view gamma = "gamma";
inline constexpr std::string_view grade = "grade";
inline constexpr std::string_view highlights = "highlights";
inline constexpr std::string_view inset = "inset";
inline constexpr std::string_view level = "level";
inline constexpr std::string_view on = "on";
inline constexpr std::string_view pivot = "pivot";
inline constexpr std::string_view profile = "profile";
inline constexpr std::string_view quarterTurns = "quarterTurns";
inline constexpr std::string_view radius = "radius";
inline constexpr std::string_view rect = "rect";
inline constexpr std::string_view saturation = "saturation";
inline constexpr std::string_view shadows = "shadows";
inline constexpr std::string_view source = "source";
inline constexpr std::string_view splitHard = "splitHard";
inline constexpr std::string_view splitSoft = "splitSoft";
inline constexpr std::string_view spots = "spots";
inline constexpr std::string_view strength = "strength";
inline constexpr std::string_view temperature = "temperature";
inline constexpr std::string_view threshold = "threshold";
inline constexpr std::string_view tint = "tint";
inline constexpr std::string_view type = "type";
inline constexpr std::string_view vibrance = "vibrance";
inline constexpr std::string_view weights = "weights";
inline constexpr std::string_view white = "white";
} // namespace key

/// Ränge der festen Stufenfolge (Architektur 5.2). Lücken lassen Platz für spätere Stufen.
namespace rank {
inline constexpr int flatfield = 10;
inline constexpr int orientation = 20;
inline constexpr int rotation = 30;
inline constexpr int crop = 40;
inline constexpr int retouch = 50;
inline constexpr int filmmode = 60;
inline constexpr int convert = 65;
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
