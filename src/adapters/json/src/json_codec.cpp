#include "autoneg/adapter/json/json_codec.hpp"

#include "autoneg/adapter/json/schema_migrator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace autoneg::adapter::json {

namespace {

using model::JsonValue;
using Ordered = nlohmann::ordered_json;

JsonValue fromNlohmann(const Ordered& j)
{
    switch (j.type()) {
    case Ordered::value_t::null:
    case Ordered::value_t::discarded:
    case Ordered::value_t::binary:
        return {};
    case Ordered::value_t::boolean:
        return JsonValue(j.get<bool>());
    case Ordered::value_t::number_integer:
        return JsonValue(j.get<std::int64_t>());
    case Ordered::value_t::number_unsigned:
        return JsonValue(static_cast<std::int64_t>(j.get<std::uint64_t>()));
    case Ordered::value_t::number_float:
        return JsonValue(j.get<double>());
    case Ordered::value_t::string:
        return JsonValue(j.get<std::string>());
    case Ordered::value_t::array: {
        JsonValue::Array a;
        for (const Ordered& e : j) {
            a.push_back(fromNlohmann(e));
        }
        return JsonValue(std::move(a));
    }
    case Ordered::value_t::object: {
        JsonValue::Object o;
        for (const auto& [k, v] : j.items()) {
            o.push_back({k, fromNlohmann(v)});
        }
        return JsonValue(std::move(o));
    }
    }
    return {};
}

Ordered toNlohmann(const JsonValue& j)
{
    if (const bool* b = j.asBool()) {
        return *b;
    }
    if (const std::int64_t* i = j.asInt()) {
        return *i;
    }
    if (const double* d = j.asDouble()) {
        return *d;
    }
    if (const std::string* s = j.asString()) {
        return *s;
    }
    if (const auto* a = j.asArray()) {
        Ordered out = Ordered::array();
        for (const JsonValue& e : *a) {
            out.push_back(toNlohmann(e));
        }
        return out;
    }
    if (const auto* o = j.asObject()) {
        Ordered out = Ordered::object();
        for (const auto& m : *o) {
            out[m.key] = toNlohmann(m.value);
        }
        return out;
    }
    return nullptr;
}

// --- Aufzählungen ------------------------------------------------------------------------

const char* toString(model::Origin o)
{
    switch (o) {
    case model::Origin::Manual:
        return "manual";
    case model::Origin::Automatic:
        return "automatic";
    case model::Origin::Copied:
        return "copied";
    case model::Origin::RollDefault:
        return "roll";
    case model::Origin::Preset:
        return "preset";
    }
    return "manual";
}

std::optional<model::Origin> originOf(std::string_view s)
{
    for (const model::Origin o : {model::Origin::Manual, model::Origin::Automatic, model::Origin::Copied,
                                  model::Origin::RollDefault, model::Origin::Preset}) {
        if (s == toString(o)) {
            return o;
        }
    }
    return std::nullopt;
}

const char* toString(model::Rating r)
{
    switch (r) {
    case model::Rating::None:
        return "none";
    case model::Rating::Keep:
        return "keep";
    case model::Rating::Reject:
        return "reject";
    }
    return "none";
}

model::Rating ratingOf(std::string_view s)
{
    return s == "keep" ? model::Rating::Keep : s == "reject" ? model::Rating::Reject : model::Rating::None;
}

const char* toString(model::FilmType f)
{
    switch (f) {
    case model::FilmType::ColorNegative:
        return "color";
    case model::FilmType::BwNegative:
        return "bw";
    case model::FilmType::Slide:
        return "slide";
    case model::FilmType::Unconverted:
        return "raw";
    }
    return "raw";
}

/// Unbekannt oder fehlend: noch nicht festgelegt (K-M10-08)
model::FilmType filmTypeOf(std::string_view s)
{
    return s == "color"   ? model::FilmType::ColorNegative
           : s == "bw"    ? model::FilmType::BwNegative
           : s == "slide" ? model::FilmType::Slide
                          : model::FilmType::Unconverted;
}

// --- Parameterwerte ----------------------------------------------------------------------

JsonValue numbers(std::initializer_list<double> v)
{
    JsonValue::Array a;
    for (const double x : v) {
        a.emplace_back(x);
    }
    return JsonValue(std::move(a));
}

JsonValue encodeValue(const model::ParamValue& v)
{
    return std::visit(
        [](const auto& x) -> JsonValue {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, double> || std::is_same_v<T, bool> ||
                          std::is_same_v<T, std::string>) {
                return JsonValue(x);
            } else if constexpr (std::is_same_v<T, int>) {
                return JsonValue(static_cast<std::int64_t>(x));
            } else if constexpr (std::is_same_v<T, base::Vec3>) {
                return numbers({x.x, x.y, x.z});
            } else if constexpr (std::is_same_v<T, model::NormRect>) {
                return numbers({x.x, x.y, x.w, x.h});
            } else if constexpr (std::is_same_v<T, model::Curve>) {
                JsonValue::Array a;
                for (const model::NormPoint& p : x.points) {
                    a.push_back(numbers({p.x, p.y}));
                }
                return JsonValue(std::move(a));
            } else {
                static_assert(std::is_same_v<T, model::SpotList>);
                JsonValue::Array a;
                for (const model::Spot& s : x) {
                    a.push_back(JsonValue::object({{"c", numbers({s.center.x, s.center.y})},
                                                   {"o", numbers({s.offset.x, s.offset.y})},
                                                   {"r", JsonValue(s.radius)}}));
                }
                return JsonValue(std::move(a));
            }
        },
        v);
}

std::optional<std::vector<double>> numberArray(const JsonValue& j, std::size_t n)
{
    const auto* a = j.asArray();
    if (a == nullptr || a->size() != n) {
        return std::nullopt;
    }
    std::vector<double> out;
    for (const JsonValue& e : *a) {
        const auto x = e.number();
        if (!x) {
            return std::nullopt;
        }
        out.push_back(*x);
    }
    return out;
}

std::optional<model::ParamValue> decodeValue(const JsonValue& j, ports::ParamKind k)
{
    switch (k) {
    case ports::ParamKind::Double:
        if (const auto x = j.number()) {
            return *x;
        }
        break;
    case ports::ParamKind::Int:
        if (const auto* i = j.asInt()) {
            return static_cast<int>(*i);
        }
        if (const auto* d = j.asDouble(); d != nullptr && std::floor(*d) == *d) {
            return static_cast<int>(*d);
        }
        break;
    case ports::ParamKind::Bool:
        if (const auto* b = j.asBool()) {
            return *b;
        }
        break;
    case ports::ParamKind::String:
        if (const auto* s = j.asString()) {
            return *s;
        }
        break;
    case ports::ParamKind::Vec3:
        if (const auto v = numberArray(j, 3)) {
            return base::Vec3{(*v)[0], (*v)[1], (*v)[2]};
        }
        break;
    case ports::ParamKind::Rect:
        if (const auto v = numberArray(j, 4)) {
            return model::NormRect{(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
        }
        break;
    case ports::ParamKind::Curve:
        if (const auto* a = j.asArray()) {
            model::Curve c;
            for (const JsonValue& e : *a) {
                const auto p = numberArray(e, 2);
                if (!p) {
                    return std::nullopt;
                }
                c.points.push_back({(*p)[0], (*p)[1]});
            }
            return c;
        }
        break;
    case ports::ParamKind::Spots:
        if (const auto* a = j.asArray()) {
            model::SpotList spots;
            for (const JsonValue& e : *a) {
                const JsonValue* c = e.find("c");
                const JsonValue* o = e.find("o");
                const JsonValue* r = e.find("r");
                const auto cv = c != nullptr ? numberArray(*c, 2) : std::nullopt;
                const auto ov = o != nullptr ? numberArray(*o, 2) : std::nullopt;
                const auto rv = r != nullptr ? r->number() : std::nullopt;
                if (!cv || !ov || !rv) {
                    return std::nullopt;
                }
                spots.push_back({{(*cv)[0], (*cv)[1]}, {(*ov)[0], (*ov)[1]}, *rv});
            }
            return spots;
        }
        break;
    }
    return std::nullopt;
}

/// Typ eines unbekannten Parameters aus der JSON-Form (Ersatz, wenn der Katalog ihn nicht kennt).
std::optional<model::ParamValue> guessValue(const JsonValue& j)
{
    if (j.isBool()) {
        return decodeValue(j, ports::ParamKind::Bool);
    }
    if (j.isInt()) {
        return decodeValue(j, ports::ParamKind::Int);
    }
    if (j.isDouble()) {
        return decodeValue(j, ports::ParamKind::Double);
    }
    if (j.isString()) {
        return decodeValue(j, ports::ParamKind::String);
    }
    if (numberArray(j, 3)) {
        return decodeValue(j, ports::ParamKind::Vec3);
    }
    if (numberArray(j, 4)) {
        return decodeValue(j, ports::ParamKind::Rect);
    }
    return std::nullopt;
}

std::string stringOr(const JsonValue& j, std::string_view key, std::string fallback = {})
{
    const JsonValue* v = j.find(key);
    return v != nullptr && v->asString() != nullptr ? *v->asString() : fallback;
}

/// Kopie von j ohne die Felder in known; null, wenn nichts übrig bleibt (dann ist "extra"
/// gleich dem Standardwert und Daten ohne Unbekanntes vergleichen gleich).
JsonValue withoutKeys(const JsonValue& j, const std::set<std::string, std::less<>>& known)
{
    JsonValue::Object rest;
    if (const auto* o = j.asObject()) {
        for (const auto& m : *o) {
            if (!known.contains(m.key)) {
                rest.push_back(m);
            }
        }
    }
    return rest.empty() ? JsonValue() : JsonValue(std::move(rest));
}

} // namespace

base::Expected<model::JsonValue> parse(std::string_view text)
{
    try {
        return fromNlohmann(Ordered::parse(text));
    } catch (const nlohmann::json::exception& e) {
        return base::makeError(base::ErrorCode::Corrupt, std::string("JSON nicht lesbar: ") + e.what());
    }
}

std::string dump(const model::JsonValue& j)
{
    return toNlohmann(j).dump(2) + "\n";
}

const ports::StepSchema* EditCodec::find(std::string_view id) const
{
    const auto it = std::ranges::find(catalog_, id, &ports::StepSchema::id);
    return it != catalog_.end() ? &*it : nullptr;
}

EditCodec::Decoded EditCodec::decode(const model::JsonValue& edits) const
{
    Decoded out;
    const auto* a = edits.asArray();
    if (a == nullptr) {
        return out;
    }
    for (const JsonValue& e : *a) {
        const std::string id = stringOr(e, "step");
        const ports::StepSchema* schema = find(id);
        const JsonValue* v = e.find("v");
        const int version = v != nullptr && v->asInt() != nullptr ? static_cast<int>(*v->asInt()) : 1;
        const JsonValue* p = e.find("p");
        if (schema == nullptr || version > schema->version || (p != nullptr && !p->isObject())) {
            out.unknown.push_back(e); // unbekannt oder neuer: unverändert erhalten
            continue;
        }
        model::EditEntry entry;
        entry.step = model::StepId(id);
        entry.version = version;
        entry.rank = schema->rank;
        const JsonValue* on = e.find("on");
        entry.enabled = on == nullptr || on->asBool() == nullptr || *on->asBool();
        entry.origin = originOf(stringOr(e, "origin", "manual")).value_or(model::Origin::Manual);
        if (const std::string from = stringOr(e, "copiedFrom"); !from.empty()) {
            if (auto id2 = model::FrameId::fromHex(from)) {
                entry.copiedFrom = *id2;
            }
        }
        bool ok = true;
        if (p != nullptr) {
            for (const auto& m : *p->asObject()) {
                const auto spec = std::ranges::find(schema->params, m.key,
                                                    &std::pair<std::string, ports::ParamKind>::first);
                const auto value =
                    spec != schema->params.end() ? decodeValue(m.value, spec->second) : guessValue(m.value);
                if (!value) {
                    ok = false;
                    break;
                }
                entry.params = entry.params.with(m.key, *value);
            }
        }
        if (!ok) {
            out.unknown.push_back(e); // nicht lesbar: lieber erhalten als verlieren
            continue;
        }
        out.edits = out.edits.with(std::move(entry));
    }
    return out;
}

model::JsonValue EditCodec::encode(const model::EditList& edits, const model::JsonValue::Array& unknown) const
{
    JsonValue::Array a;
    for (const model::EditEntry& e : edits.entries()) {
        JsonValue::Object p;
        for (const auto& [name, value] : e.params.entries()) {
            p.push_back({name, encodeValue(value)});
        }
        JsonValue entry = JsonValue::object({{"step", JsonValue(e.step.value())},
                                             {"v", JsonValue(static_cast<std::int64_t>(e.version))},
                                             {"on", JsonValue(e.enabled)},
                                             {"origin", JsonValue(toString(e.origin))}});
        if (e.copiedFrom) {
            entry.set("copiedFrom", JsonValue(e.copiedFrom->toHex()));
        }
        entry.set("p", JsonValue(std::move(p)));
        a.push_back(std::move(entry));
    }
    for (const JsonValue& u : unknown) {
        a.push_back(u);
    }
    return JsonValue(std::move(a));
}

base::Expected<ports::SidecarData> EditCodec::sidecarFromJson(const model::JsonValue& j) const
{
    if (!j.isObject()) {
        return base::makeError(base::ErrorCode::Corrupt, "Begleitdatei ist kein JSON-Objekt");
    }
    ports::SidecarData d;
    if (const JsonValue* s = j.find("source"); s != nullptr && s->isObject()) {
        d.source.file = stringOr(*s, "file");
        if (const JsonValue* size = s->find("size"); size != nullptr && size->asInt() != nullptr) {
            d.source.size = static_cast<std::uint64_t>(*size->asInt());
        }
        if (const JsonValue* mtime = s->find("mtime"); mtime != nullptr && mtime->asInt() != nullptr) {
            d.source.mtime = *mtime->asInt();
        }
        if (auto h = model::FrameId::fromHex(stringOr(*s, "hash"))) {
            d.source.hash = *h;
        }
    }
    if (const JsonValue* f = j.find("frameNo"); f != nullptr && f->asInt() != nullptr) {
        d.frameNo = static_cast<int>(*f->asInt());
    }
    d.rating = ratingOf(stringOr(j, "rating", "none"));
    Decoded edits;
    if (const JsonValue* e = j.find("edits")) {
        if (!e->isArray()) {
            return base::makeError(base::ErrorCode::Corrupt, "Feld edits ist keine Liste");
        }
        edits = decode(*e);
    }
    d.edits = std::move(edits.edits);
    d.extra = withoutKeys(j, {"schema", "app", "source", "frameNo", "rating", "edits"});
    if (!edits.unknown.empty()) {
        d.extra.set("edits", JsonValue(std::move(edits.unknown)));
    }
    return d;
}

model::JsonValue EditCodec::sidecarToJson(const ports::SidecarData& d, int schema) const
{
    JsonValue j = JsonValue::object(
        {{"schema", JsonValue(static_cast<std::int64_t>(schema))},
         {"app", JsonValue("AutoNeg 1.0.0")},
         {"source", JsonValue::object({{"file", JsonValue(d.source.file)},
                                       {"size", JsonValue(static_cast<std::int64_t>(d.source.size))},
                                       {"mtime", JsonValue(d.source.mtime)},
                                       {"hash", JsonValue(d.source.hash.toHex())}})},
         {"frameNo", JsonValue(static_cast<std::int64_t>(d.frameNo))},
         {"rating", JsonValue(toString(d.rating))}});
    JsonValue::Array unknown;
    if (const JsonValue* u = d.extra.find("edits"); u != nullptr && u->asArray() != nullptr) {
        unknown = *u->asArray();
    }
    j.set("edits", encode(d.edits, unknown));
    if (const auto* o = d.extra.asObject()) {
        for (const auto& m : *o) {
            if (m.key != "edits") {
                j.set(m.key, m.value);
            }
        }
    }
    return j;
}

base::Expected<model::Roll> EditCodec::rollFromJson(const model::JsonValue& j) const
{
    if (!j.isObject()) {
        return base::makeError(base::ErrorCode::Corrupt, "Rollendatei ist kein JSON-Objekt");
    }
    model::Roll r;
    r.name = stringOr(j, "name");
    r.filmType = filmTypeOf(stringOr(j, "filmType", "raw"));
    JsonValue filmRest;
    if (const JsonValue* f = j.find("film"); f != nullptr && f->isObject()) {
        r.film.filmName = stringOr(*f, "name");
        if (const JsonValue* iso = f->find("iso"); iso != nullptr && iso->number()) {
            r.film.iso = static_cast<int>(*iso->number());
        }
        r.film.developer = stringOr(*f, "developer");
        r.film.camera = stringOr(*f, "camera");
        r.film.shotDate = stringOr(*f, "shotDate");
        r.film.notes = stringOr(*f, "notes");
        filmRest = withoutKeys(*f, {"name", "iso", "developer", "camera", "shotDate", "notes"});
    }
    Decoded defaults;
    if (const JsonValue* d = j.find("defaults"); d != nullptr && d->isArray()) {
        defaults = decode(*d);
    }
    r.defaults = std::move(defaults.edits);
    r.extra = withoutKeys(j, {"schema", "app", "name", "filmType", "film", "defaults"});
    if (!defaults.unknown.empty()) {
        r.extra.set("defaults", JsonValue(std::move(defaults.unknown)));
    }
    if (const auto* o = filmRest.asObject(); o != nullptr && !o->empty()) {
        r.extra.set("film", filmRest);
    }
    return r;
}

model::JsonValue EditCodec::rollToJson(const model::Roll& r, int schema) const
{
    JsonValue film = JsonValue::object({{"name", JsonValue(r.film.filmName)},
                                        {"iso", JsonValue(static_cast<std::int64_t>(r.film.iso))},
                                        {"developer", JsonValue(r.film.developer)},
                                        {"camera", JsonValue(r.film.camera)},
                                        {"shotDate", JsonValue(r.film.shotDate)},
                                        {"notes", JsonValue(r.film.notes)}});
    if (const JsonValue* rest = r.extra.find("film"); rest != nullptr && rest->asObject() != nullptr) {
        for (const auto& m : *rest->asObject()) {
            film.set(m.key, m.value);
        }
    }
    JsonValue::Array unknown;
    if (const JsonValue* u = r.extra.find("defaults"); u != nullptr && u->asArray() != nullptr) {
        unknown = *u->asArray();
    }
    JsonValue j = JsonValue::object({{"schema", JsonValue(static_cast<std::int64_t>(schema))},
                                     {"name", JsonValue(r.name)},
                                     {"filmType", JsonValue(toString(r.filmType))},
                                     {"film", std::move(film)},
                                     {"defaults", encode(r.defaults, unknown)}});
    if (const auto* o = r.extra.asObject()) {
        for (const auto& m : *o) {
            if (m.key != "film" && m.key != "defaults") {
                j.set(m.key, m.value);
            }
        }
    }
    return j;
}

int SchemaMigrator::schemaOf(const model::JsonValue& j)
{
    const JsonValue* s = j.find("schema");
    return s != nullptr && s->asInt() != nullptr ? static_cast<int>(*s->asInt()) : 0;
}

base::Expected<model::JsonValue> SchemaMigrator::migrate(model::JsonValue j) const
{
    int version = schemaOf(j);
    while (version < current_) {
        const auto step = std::ranges::find(steps_, version, &Migration::from);
        if (step == steps_.end()) {
            return base::makeError(base::ErrorCode::Unsupported,
                                   "Schema " + std::to_string(version) + " kann nicht migriert werden");
        }
        auto next = step->apply(std::move(j));
        if (!next) {
            return next;
        }
        j = std::move(*next);
        ++version;
        j.set("schema", JsonValue(static_cast<std::int64_t>(version)));
    }
    return j;
}

} // namespace autoneg::adapter::json
