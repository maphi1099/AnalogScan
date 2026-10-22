#pragma once

#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/json_value.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace autoneg::model {

/// Ein Scan einer Rolle.
struct Frame
{
    FrameId id;
    RollId roll;
    std::filesystem::path path;
    int frameNo = 0;
    Rating rating = Rating::None;
    SourceInfo source;

    friend bool operator==(const Frame&, const Frame&) = default;
};

/// Eine Filmrolle = ein Scan-Ordner (Kapitel 8).
struct Roll
{
    RollId id;
    std::string name;
    std::filesystem::path folder;
    FilmType filmType = FilmType::ColorNegative;
    FilmInfo film;
    EditList defaults; ///< Rollen-Standard (LF-BAT-01)
    std::vector<FrameId> frames;
    /// Unbekannte Felder der Rollendatei (z. B. Kontaktabzug bis M7), unverändert erhalten
    /// (NF-ARC-09). Ergänzung K-M6-02.
    JsonValue extra;

    /// Position des Bildes in der Rolle, -1 wenn es nicht dazugehört.
    [[nodiscard]] int indexOf(const FrameId& f) const noexcept;

    friend bool operator==(const Roll&, const Roll&) = default;
};

} // namespace autoneg::model
