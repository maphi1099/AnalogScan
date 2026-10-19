#pragma once

// Hilfen zum Aufbau von Bearbeitungslisten in Tests. Die Ränge folgen der Stufenfolge aus
// Architektur 5.2; die echten Werte liefert ab M2 die StepRegistry.

#include "autoneg/model/edit_entry.hpp"
#include "autoneg/model/param_set.hpp"

#include <string>
#include <utility>

namespace autoneg::testing {

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
inline constexpr int output = 210;
} // namespace rank

/// Eintrag mit einem Parameter.
inline model::EditEntry entry(const std::string& step, int stepRank, const std::string& param,
                              model::ParamValue value, model::Origin origin = model::Origin::Manual)
{
    model::EditEntry e;
    e.step = model::StepId("autoneg." + step);
    e.rank = stepRank;
    e.params = model::ParamSet{}.with(param, std::move(value));
    e.origin = origin;
    return e;
}

inline model::EditEntry exposure(double ev)
{
    return entry("exposure", rank::exposure, "ev", ev);
}
inline model::EditEntry contrast(double amount)
{
    return entry("contrast", rank::contrast, "amount", amount);
}
inline model::EditEntry sharpen(double amount)
{
    return entry("sharpen", rank::sharpen, "amount", amount);
}
inline model::EditEntry crop(model::NormRect r)
{
    return entry("crop", rank::crop, "rect", r);
}
inline model::EditEntry whitebalance(double temperature)
{
    return entry("whitebalance", rank::whitebalance, "temperature", temperature);
}

inline model::StepId id(const std::string& step)
{
    return model::StepId("autoneg." + step);
}

} // namespace autoneg::testing
