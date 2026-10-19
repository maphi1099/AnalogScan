#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace autoneg::model {

/// JSON-Wertebaum des Kerns (A-03). Ersetzt nlohmann/json in den Schnittstellen; der
/// Adapter autoneg_adapter_json übersetzt. Objekte behalten die Reihenfolge ihrer
/// Schlüssel, damit unbekannte Felder unverändert zurückgeschrieben werden (NF-ARC-09).
class JsonValue
{
public:
    struct Member;
    using Array = std::vector<JsonValue>;
    using Object = std::vector<Member>;

    // Konstruktoren außerhalb definiert: Member ist innerhalb der Klasse noch unvollständig.
    JsonValue() noexcept = default; ///< null
    JsonValue(std::nullptr_t) noexcept;
    JsonValue(bool v) noexcept;
    JsonValue(int v) noexcept;
    JsonValue(std::int64_t v) noexcept;
    JsonValue(double v) noexcept;
    JsonValue(const char* v);
    JsonValue(std::string v) noexcept;
    JsonValue(Array v) noexcept;
    JsonValue(Object v) noexcept;

    [[nodiscard]] static JsonValue array(std::initializer_list<JsonValue> items);
    [[nodiscard]] static JsonValue object(std::initializer_list<Member> members);

    [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<std::monostate>(value_); }
    [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(value_); }
    [[nodiscard]] bool isInt() const noexcept { return std::holds_alternative<std::int64_t>(value_); }
    [[nodiscard]] bool isDouble() const noexcept { return std::holds_alternative<double>(value_); }
    [[nodiscard]] bool isNumber() const noexcept { return isInt() || isDouble(); }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(value_); }
    [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<Array>(value_); }
    [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<Object>(value_); }

    /// Zugriff auf den Inhalt; nullptr bei anderem Typ.
    [[nodiscard]] const bool* asBool() const noexcept { return std::get_if<bool>(&value_); }
    [[nodiscard]] const std::int64_t* asInt() const noexcept { return std::get_if<std::int64_t>(&value_); }
    [[nodiscard]] const double* asDouble() const noexcept { return std::get_if<double>(&value_); }
    [[nodiscard]] const std::string* asString() const noexcept { return std::get_if<std::string>(&value_); }
    [[nodiscard]] const Array* asArray() const noexcept { return std::get_if<Array>(&value_); }
    [[nodiscard]] const Object* asObject() const noexcept { return std::get_if<Object>(&value_); }
    [[nodiscard]] Array* asArray() noexcept { return std::get_if<Array>(&value_); }
    [[nodiscard]] Object* asObject() noexcept { return std::get_if<Object>(&value_); }

    /// Zahl als double (auch ganzzahlige Werte); leer bei anderem Typ.
    [[nodiscard]] std::optional<double> number() const noexcept;

    /// Objektfeld; nullptr, wenn kein Objekt oder Schlüssel fehlt.
    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
    /// Setzt bzw. ersetzt ein Objektfeld; macht aus null ein Objekt. Reihenfolge bleibt erhalten.
    void set(std::string_view key, JsonValue value);

    friend bool operator==(const JsonValue& a, const JsonValue& b) noexcept;

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> value_;
};

struct JsonValue::Member
{
    std::string key;
    JsonValue value;

    friend bool operator==(const Member&, const Member&) = default;
};

} // namespace autoneg::model
