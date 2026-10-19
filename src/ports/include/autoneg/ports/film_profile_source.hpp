#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/json_value.hpp"

#include <string>
#include <vector>

namespace autoneg::ports {

/// Kurzangaben eines Filmprofils (LF-PRO-01, Version 1.1).
struct FilmProfileInfo
{
    std::string id;
    std::string manufacturer;
    std::string name;
    int iso = 0;
    model::FilmType type = model::FilmType::ColorNegative;

    friend bool operator==(const FilmProfileInfo&, const FilmProfileInfo&) = default;
};

/// Quelle für Filmprofile (NF-ARC-15). Version 1.0: nur die Schnittstelle; Profile,
/// Import und Export folgen mit Version 1.1 (LF-PRO).
class IFilmProfileSource
{
public:
    virtual ~IFilmProfileSource() = default;

    [[nodiscard]] virtual std::vector<FilmProfileInfo> list() = 0;
    /// Profilinhalt im dokumentierten Dateiformat (LF-PRO-03).
    [[nodiscard]] virtual base::Expected<model::JsonValue> load(const std::string& id) = 0;
};

} // namespace autoneg::ports
