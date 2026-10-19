#include "autoneg/model/json_value.hpp"

#include <algorithm>

namespace autoneg::model {

JsonValue::JsonValue(std::nullptr_t) noexcept
{}

JsonValue::JsonValue(bool v) noexcept
    : value_(v)
{}

JsonValue::JsonValue(int v) noexcept
    : value_(static_cast<std::int64_t>(v))
{}

JsonValue::JsonValue(std::int64_t v) noexcept
    : value_(v)
{}

JsonValue::JsonValue(double v) noexcept
    : value_(v)
{}

JsonValue::JsonValue(const char* v)
    : value_(std::string(v))
{}

JsonValue::JsonValue(std::string v) noexcept
    : value_(std::move(v))
{}

JsonValue::JsonValue(Array v) noexcept
    : value_(std::move(v))
{}

JsonValue::JsonValue(Object v) noexcept
    : value_(std::move(v))
{}

JsonValue JsonValue::array(std::initializer_list<JsonValue> items)
{
    return JsonValue(Array(items));
}

JsonValue JsonValue::object(std::initializer_list<Member> members)
{
    JsonValue result{Object{}};
    for (const Member& m : members) {
        result.set(m.key, m.value);
    }
    return result;
}

std::optional<double> JsonValue::number() const noexcept
{
    if (const auto* i = asInt()) {
        return static_cast<double>(*i);
    }
    if (const auto* d = asDouble()) {
        return *d;
    }
    return std::nullopt;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept
{
    const Object* obj = asObject();
    if (obj == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find(*obj, key, &Member::key);
    return it == obj->end() ? nullptr : &it->value;
}

void JsonValue::set(std::string_view key, JsonValue value)
{
    if (isNull()) {
        value_ = Object{};
    }
    Object* obj = asObject();
    if (obj == nullptr) {
        return; // kein Objekt: unverändert lassen
    }
    const auto it = std::ranges::find(*obj, key, &Member::key);
    if (it != obj->end()) {
        it->value = std::move(value);
    } else {
        obj->push_back(Member{std::string(key), std::move(value)});
    }
}

bool operator==(const JsonValue& a, const JsonValue& b) noexcept
{
    return a.value_ == b.value_;
}

} // namespace autoneg::model
