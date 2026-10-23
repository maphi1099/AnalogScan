#include "autoneg/adapter/json/json_config_store.hpp"

#include "autoneg/adapter/json/json_codec.hpp"
#include "autoneg/adapter/json/schema_migrator.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>

namespace autoneg::adapter::json {

namespace {

namespace fs = std::filesystem;
using model::JsonValue;

const char* folderOf(ports::PresetKind k)
{
    switch (k) {
    case ports::PresetKind::Edit:
        return "edit";
    case ports::PresetKind::Export:
        return "export";
    case ports::PresetKind::ContactSheet:
        return "contactsheet";
    case ports::PresetKind::Base:
        return "base";
    case ports::PresetKind::FlatField:
        return "flatfield";
    }
    return "edit";
}

/// Vorlagennamen werden Dateinamen: Trennzeichen und reservierte Zeichen sind nicht erlaubt.
/// Vorlagennamen sind Anwendereingaben (UTF-8). Verboten sind nur Pfadtrenner, Steuerzeichen und
/// "."/".."; Zeichen, die ein Dateisystem nicht erlaubt, kodiert fileStem().
bool validName(const std::string& name)
{
    if (name.empty() || name == "." || name == ".." || name.size() > 120) {
        return false;
    }
    return std::ranges::none_of(
        name, [](char c) { return c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20; });
}

/// Dateiname zum Vorlagennamen: : * ? " < > | % und ein Punkt bzw. Leerzeichen am Ende als %XX
/// (unter Windows unzulässig); alles Übrige bleibt, Umlaute als UTF-8.
std::string fileStem(const std::string& name)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool last = i + 1 == name.size();
        if (std::string_view(":*?\"<>|%").find(c) != std::string_view::npos ||
            (last && (c == '.' || c == ' '))) {
            const auto u = static_cast<unsigned char>(c);
            out += '%';
            out += hex[u >> 4U];
            out += hex[u & 0xFU];
        } else {
            out += c;
        }
    }
    return out;
}

std::string nameOf(const std::string& stem)
{
    std::string out;
    for (std::size_t i = 0; i < stem.size(); ++i) {
        if (stem[i] == '%' && i + 2 < stem.size()) {
            const auto digit = [](char h) { return h <= '9' ? h - '0' : h - 'A' + 10; };
            out += static_cast<char>(digit(stem[i + 1]) * 16 + digit(stem[i + 2]));
            i += 2;
        } else {
            out += stem[i];
        }
    }
    return out;
}

/// UTF-8 ↔ Pfad (unter Windows sonst ANSI-Codepage; plattformabhängig, PROGRESS.md)
fs::path u8path(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string u8string(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::int64_t mtimeOf(const fs::path& p)
{
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch())
        .count();
}

} // namespace

JsonConfigStore::JsonConfigStore(std::filesystem::path root, const AtomicFileWriter& writer,
                                 std::map<std::string, model::JsonValue> defaults)
    : root_(std::move(root))
    , writer_(writer)
    , defaults_(std::move(defaults))
    , settings_(JsonValue::Object{})
{
    const fs::path p = root_ / "settings.json";
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return;
    }
    std::ostringstream s;
    {
        std::ifstream in(p, std::ios::binary); // vor dem Umbenennen schließen (Windows)
        s << in.rdbuf();
    }
    auto j = parse(s.str());
    const JsonValue* values = j ? j->find("values") : nullptr;
    if (!j || values == nullptr || !values->isObject()) {
        fs::path bad = p;
        bad += ".defekt";
        fs::rename(p, bad, ec);
        loadMessage_ =
            "settings.json war beschädigt und wurde nach settings.json.defekt umbenannt; es gelten die "
            "Standardeinstellungen.";
        return;
    }
    // Werte mit falschem Typ (gegenüber dem Standard) werden verworfen
    for (const auto& m : *values->asObject()) {
        const auto d = defaults_.find(m.key);
        const bool typeOk =
            d == defaults_.end() || (d->second.isNumber() && m.value.isNumber()) ||
            (d->second.isBool() == m.value.isBool() && d->second.isString() == m.value.isString() &&
             d->second.isArray() == m.value.isArray() && d->second.isObject() == m.value.isObject());
        if (typeOk) {
            settings_.set(m.key, m.value);
        } else {
            loadMessage_ = "Einstellung " + m.key + " hatte einen falschen Typ; Standardwert gilt.";
        }
    }
}

std::optional<model::JsonValue> JsonConfigStore::get(const std::string& key)
{
    const std::scoped_lock lock(mutex_);
    if (const JsonValue* v = settings_.find(key)) {
        return *v;
    }
    if (const auto d = defaults_.find(key); d != defaults_.end()) {
        return d->second;
    }
    return std::nullopt;
}

void JsonConfigStore::set(const std::string& key, const model::JsonValue& v)
{
    const std::scoped_lock lock(mutex_);
    const auto d = defaults_.find(key);
    JsonValue::Object kept;
    for (const auto& m : *settings_.asObject()) {
        if (m.key != key) {
            kept.push_back(m);
        }
    }
    settings_ = JsonValue(std::move(kept));
    if (d == defaults_.end() || !(d->second == v)) {
        settings_.set(key, v); // Standardwerte werden nicht gespeichert (U-DAT-14)
    }
    persist();
}

void JsonConfigStore::persist()
{
    std::error_code ec;
    fs::create_directories(root_, ec);
    const JsonValue j =
        JsonValue::object({{"schema", JsonValue(static_cast<std::int64_t>(SchemaMigrator::currentSchema))},
                           {"values", settings_}});
    (void)writer_.write(root_ / "settings.json", dump(j));
}

fs::path JsonConfigStore::presetPath(ports::PresetKind k, const std::string& name) const
{
    return root_ / "presets" / folderOf(k) / u8path(fileStem(name) + ".json");
}

std::vector<ports::PresetInfo> JsonConfigStore::list(ports::PresetKind k)
{
    std::vector<ports::PresetInfo> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root_ / "presets" / folderOf(k), ec)) {
        if (e.is_regular_file() && e.path().extension() == ".json") {
            out.push_back({{k, nameOf(u8string(e.path().stem()))}, mtimeOf(e.path())});
        }
    }
    std::ranges::sort(out, {}, [](const ports::PresetInfo& p) { return p.id.name; });
    return out;
}

base::Expected<model::JsonValue> JsonConfigStore::load(const ports::PresetId& id)
{
    if (!validName(id.name)) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Ungültiger Vorlagenname: " + id.name);
    }
    std::ifstream in(presetPath(id.kind, id.name), std::ios::binary);
    if (!in) {
        return base::makeError(base::ErrorCode::NotFound, "Vorlage " + id.name + " nicht gefunden");
    }
    std::ostringstream s;
    s << in.rdbuf();
    auto j = parse(s.str());
    if (!j) {
        return std::unexpected(j.error());
    }
    const JsonValue* content = j->find("content");
    return content != nullptr ? *content : *j;
}

base::Expected<void> JsonConfigStore::save(ports::PresetKind k, const std::string& name,
                                           const model::JsonValue& j)
{
    if (!validName(name)) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Ungültiger Vorlagenname: " + name);
    }
    const fs::path p = presetPath(k, name);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const JsonValue file = JsonValue::object(
        {{"schema", JsonValue(static_cast<std::int64_t>(SchemaMigrator::currentSchema))}, {"content", j}});
    return writer_.write(p, dump(file));
}

base::Expected<void> JsonConfigStore::remove(const ports::PresetId& id)
{
    if (!validName(id.name)) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Ungültiger Vorlagenname: " + id.name);
    }
    std::error_code ec;
    if (!fs::remove(presetPath(id.kind, id.name), ec)) {
        return base::makeError(base::ErrorCode::NotFound, "Vorlage " + id.name + " nicht gefunden");
    }
    return {};
}

} // namespace autoneg::adapter::json
