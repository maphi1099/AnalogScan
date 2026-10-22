#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/model/json_value.hpp"

#include <functional>
#include <vector>

namespace autoneg::adapter::json {

/// Hebt ältere Schemaversionen beim Lesen auf die aktuelle (Architektur 8.8, NF-ARC-09).
/// Jede Migration führt von Version from nach from + 1. Dateien eines neueren Schemas bleiben
/// unverändert; der Aufrufer öffnet sie nur lesend.
class SchemaMigrator
{
public:
    static constexpr int currentSchema = 1;

    struct Migration
    {
        int from = 0;
        std::function<base::Expected<model::JsonValue>(model::JsonValue)> apply;
    };

    SchemaMigrator() = default;
    /// current: Zielversion (Standard: currentSchema; Tests können eine höhere angeben).
    SchemaMigrator(std::vector<Migration> steps, int current)
        : steps_(std::move(steps))
        , current_(current)
    {}

    [[nodiscard]] int current() const noexcept { return current_; }
    /// Schemaversion einer Datei; 0, wenn das Feld fehlt.
    [[nodiscard]] static int schemaOf(const model::JsonValue& j);
    /// Migriert bis zur aktuellen Version. Neuere Versionen werden unverändert zurückgegeben.
    /// Fehler Unsupported, wenn ein Migrationsschritt fehlt.
    [[nodiscard]] base::Expected<model::JsonValue> migrate(model::JsonValue j) const;

private:
    std::vector<Migration> steps_;
    int current_ = currentSchema;
};

} // namespace autoneg::adapter::json
