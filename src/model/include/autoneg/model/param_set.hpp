#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/model/values.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace autoneg::model {

/// Wert eines Schrittparameters.
using ParamValue = std::variant<double, int, bool, std::string, base::Vec3, NormRect, Curve, SpotList>;

/// Unveränderliche Parametermenge eines Eintrags, nach Namen sortiert.
///
/// Abweichung von Architektur 9.1: statt std::flat_map ein sortierter Vektor (D-01);
/// Schnittstelle unverändert.
class ParamSet
{
public:
    using Entry = std::pair<std::string, ParamValue>;

    ParamSet() = default;

    /// Neue Menge mit gesetztem bzw. ersetztem Wert; die eigene bleibt unverändert.
    [[nodiscard]] ParamSet with(std::string_view name, ParamValue value) const;
    /// Neue Menge ohne den Wert.
    [[nodiscard]] ParamSet without(std::string_view name) const;

    /// Wert mit Typprüfung. Fehler: NotFound bei unbekanntem Namen, TypeMismatch bei
    /// anderem Typ (U-MOD-12). Es wird nicht umgewandelt (auch nicht int -> double).
    template <class T>
    [[nodiscard]] base::Expected<T> get(std::string_view name) const;

    /// Wert ohne Typprüfung; nullptr, wenn nicht vorhanden.
    [[nodiscard]] const ParamValue* find(std::string_view name) const noexcept;
    [[nodiscard]] bool contains(std::string_view name) const noexcept { return find(name) != nullptr; }

    [[nodiscard]] std::span<const Entry> entries() const noexcept { return values_; }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

    /// Hängt nur vom Inhalt ab, nicht von der Reihenfolge des Aufbaus.
    [[nodiscard]] std::uint64_t hash() const noexcept;

    friend bool operator==(const ParamSet&, const ParamSet&) = default;

private:
    std::vector<Entry> values_;
};

template <class T>
base::Expected<T> ParamSet::get(std::string_view name) const
{
    static_assert(std::is_constructible_v<ParamValue, T>, "T muss ein Typ von ParamValue sein");
    const ParamValue* value = find(name);
    if (value == nullptr) {
        return base::makeError(base::ErrorCode::NotFound, "Parameter '" + std::string(name) + "' fehlt");
    }
    if (const T* typed = std::get_if<T>(value)) {
        return *typed;
    }
    return base::makeError(base::ErrorCode::TypeMismatch,
                           "Parameter '" + std::string(name) + "' hat einen anderen Typ");
}

} // namespace autoneg::model
