#include "autoneg/adapter/json/json_sidecar_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

namespace autoneg::adapter::json {

namespace {

namespace fs = std::filesystem;
using model::JsonValue;

constexpr const char* sidecarSuffix = ".autoneg.json";
constexpr const char* rollFile = "roll.autoneg.json";
constexpr const char* lockFile = ".autoneg.lock";

std::string name(const fs::path& p)
{
    return p.filename().string();
}

/// Größe und Änderungszeit (Sekunden seit 1970) einer Datei.
std::optional<std::pair<std::uint64_t, std::int64_t>> stamp(const fs::path& p)
{
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto t = fs::last_write_time(p, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return std::pair{size, std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count()};
}

std::optional<std::string> readText(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/// Stabile Kennung eines Ordnerpfads (FNV-1a 64) für die Ersatzablage der Rollendatei.
std::string folderKey(const fs::path& folder)
{
    std::error_code ec;
    const std::string s = fs::weakly_canonical(folder, ec).generic_string();
    std::uint64_t h = 14695981039346656037ULL;
    for (const char c : s) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

} // namespace

JsonSidecarStore::JsonSidecarStore(ports::StepCatalog catalog, const AtomicFileWriter& writer,
                                   const SchemaMigrator& migrator, ports::IContentHasher& hasher,
                                   std::filesystem::path fallbackDir, WritableProbe writable)
    : codec_(std::move(catalog))
    , writer_(writer)
    , migrator_(migrator)
    , hasher_(hasher)
    , fallbackDir_(std::move(fallbackDir))
    , writable_(std::move(writable))
{}

fs::path JsonSidecarStore::sidecarPath(const model::Frame& f)
{
    fs::path p = f.path;
    p += sidecarSuffix; // volle Endung des Scans bleibt erhalten (bild.ARW.autoneg.json)
    return p;
}

fs::path JsonSidecarStore::fallbackPath(const model::FrameId& hash) const
{
    return fallbackDir_ / (hash.toHex() + sidecarSuffix);
}

fs::path JsonSidecarStore::rollPath(const fs::path& folder) const
{
    if (writable(folder) || fs::exists(folder / rollFile)) {
        return folder / rollFile;
    }
    return fallbackDir_ / ("roll-" + folderKey(folder) + sidecarSuffix);
}

bool JsonSidecarStore::writable(const fs::path& folder) const
{
    if (writable_) {
        return writable_(folder);
    }
    // Probe: Datei anlegen und wieder löschen (Rechte, schreibgeschützte Datenträger)
    const fs::path probe = folder / ".autoneg.write-probe";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) {
            return false;
        }
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}

base::Expected<JsonValue> JsonSidecarStore::readJson(const fs::path& p)
{
    const auto text = readText(p);
    if (!text) {
        return base::makeError(base::ErrorCode::Io, name(p) + ": nicht lesbar");
    }
    auto j = parse(*text);
    if (!j) {
        // Defekte Datei beiseitelegen, damit sie nicht überschrieben wird (Architektur 8.8)
        fs::path bad = p;
        bad += ".defekt";
        std::error_code ec;
        fs::rename(p, bad, ec);
        return base::makeError(base::ErrorCode::Corrupt,
                               name(p) + " ist beschädigt und wurde nach " + name(bad) + " umbenannt");
    }
    return j;
}

base::Expected<model::FrameId> JsonSidecarStore::hashOf(const model::Frame& f)
{
    if (f.id != model::FrameId{}) {
        return f.id;
    }
    return hasher_.hashFile(f.path);
}

base::Expected<std::optional<ports::SidecarData>> JsonSidecarStore::load(const model::Frame& f)
{
    fs::path p = sidecarPath(f);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        // Ersatzablage nur, wenn die Prüfsumme schon bekannt ist (sonst kein Grund zu rechnen)
        if (f.id == model::FrameId{} || !fs::exists(fallbackPath(f.id), ec)) {
            return std::optional<ports::SidecarData>{};
        }
        p = fallbackPath(f.id);
    }
    auto raw = readJson(p);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    const int schema = SchemaMigrator::schemaOf(*raw);
    if (schema > migrator_.current()) {
        const std::scoped_lock lock(mutex_);
        readOnly_.insert(p); // neueres Schema: nur lesend (U-DAT-04)
    }
    auto migrated = migrator_.migrate(std::move(*raw));
    if (!migrated) {
        return std::unexpected(migrated.error());
    }
    auto data = codec_.sidecarFromJson(*migrated);
    if (!data) {
        fs::path bad = p;
        bad += ".defekt";
        fs::rename(p, bad, ec);
        return base::makeError(base::ErrorCode::Corrupt, name(p) + " ist beschädigt (" +
                                                             data.error().message + ") und wurde nach " +
                                                             name(bad) + " umbenannt");
    }
    // Zuordnung prüfen (Architektur 8.6): Größe und Zeit gleich → ohne Prüfsumme gültig
    const auto current = stamp(f.path);
    if (current && (current->first != data->source.size || current->second != data->source.mtime)) {
        auto h = hasher_.hashFile(f.path);
        if (!h) {
            return std::unexpected(h.error());
        }
        if (*h != data->source.hash) {
            return base::makeError(base::ErrorCode::Conflict,
                                   name(f.path) +
                                       " wurde ersetzt (andere Prüfsumme); Bearbeitung übernehmen?");
        }
        data->source.size = current->first;
        data->source.mtime = current->second;
    }
    return std::optional<ports::SidecarData>(std::move(*data));
}

base::Expected<void> JsonSidecarStore::save(const model::Frame& f, const ports::SidecarData& d)
{
    fs::path p = sidecarPath(f);
    std::error_code ec;
    if (!writable(f.path.parent_path())) {
        const auto h = hashOf(f);
        if (!h) {
            return std::unexpected(h.error());
        }
        fs::create_directories(fallbackDir_, ec);
        p = fallbackPath(*h);
    }
    {
        const std::scoped_lock lock(mutex_);
        if (readOnly_.contains(p) || readOnly_.contains(sidecarPath(f))) {
            return base::makeError(
                base::ErrorCode::ReadOnly,
                name(p) + " stammt aus einer neueren Programmversion und wird nicht überschrieben");
        }
    }
    return writer_.write(p, dump(codec_.sidecarToJson(d, migrator_.current())));
}

std::vector<ports::SidecarRef> JsonSidecarStore::orphans(const fs::path& folder)
{
    std::vector<ports::SidecarRef> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const std::string file = e.path().filename().string();
        if (!e.is_regular_file() || !file.ends_with(sidecarSuffix) || file == rollFile) {
            continue;
        }
        const fs::path scan = folder / file.substr(0, file.size() - std::string_view(sidecarSuffix).size());
        if (fs::exists(scan, ec)) {
            continue;
        }
        const auto text = readText(e.path());
        auto j = text ? parse(*text) : base::Expected<JsonValue>(base::makeError(base::ErrorCode::Io, ""));
        if (!j) {
            continue;
        }
        auto d = codec_.sidecarFromJson(*j);
        if (d) {
            out.push_back({e.path(), d->source});
        }
    }
    std::ranges::sort(out, {}, &ports::SidecarRef::path);
    return out;
}

base::Expected<void> JsonSidecarStore::relink(const ports::SidecarRef& orphan, const model::Frame& f)
{
    const auto h = hashOf(f);
    if (!h) {
        return std::unexpected(h.error());
    }
    if (*h != orphan.source.hash) {
        return base::makeError(base::ErrorCode::Conflict,
                               name(orphan.path) + " gehört nicht zu " + name(f.path));
    }
    const fs::path target = sidecarPath(f);
    std::error_code ec;
    if (fs::exists(target, ec)) {
        return base::makeError(base::ErrorCode::Conflict, name(target) + " existiert bereits");
    }
    // Inhalt mit neuem Dateinamen schreiben, dann die alte Datei entfernen
    auto raw = readJson(orphan.path);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    auto d = codec_.sidecarFromJson(*raw);
    if (!d) {
        return std::unexpected(d.error());
    }
    d->source.file = f.path.filename().string();
    if (auto ok = writer_.write(target, dump(codec_.sidecarToJson(*d, migrator_.current()))); !ok) {
        return ok;
    }
    fs::remove(orphan.path, ec);
    return {};
}

base::Expected<std::optional<model::Roll>> JsonSidecarStore::load(const fs::path& folder)
{
    // Reste abgebrochener Schreibvorgänge entfernen (U-DAT-07)
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (e.is_regular_file() &&
            e.path().filename().string().ends_with(std::string(sidecarSuffix) + ".tmp")) {
            fs::remove(e.path(), ec);
        }
    }
    const fs::path p = rollPath(folder);
    if (!fs::exists(p, ec)) {
        return std::optional<model::Roll>{};
    }
    auto raw = readJson(p);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    auto migrated = migrator_.migrate(std::move(*raw));
    if (!migrated) {
        return std::unexpected(migrated.error());
    }
    auto roll = codec_.rollFromJson(*migrated);
    if (!roll) {
        return std::unexpected(roll.error());
    }
    roll->folder = folder;
    return std::optional<model::Roll>(std::move(*roll));
}

base::Expected<void> JsonSidecarStore::save(const model::Roll& r)
{
    const fs::path p = rollPath(r.folder);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return writer_.write(p, dump(codec_.rollToJson(r, migrator_.current())));
}

base::Expected<bool> JsonSidecarStore::lock(const fs::path& folder)
{
    const fs::path p = folder / lockFile;
    const std::int64_t me = currentProcessId();
    const std::string host = hostName();
    std::error_code ec;
    if (fs::exists(p, ec)) {
        const auto text = readText(p);
        const auto j =
            text ? parse(*text) : base::Expected<JsonValue>(base::makeError(base::ErrorCode::Io, ""));
        if (j) {
            const JsonValue* pid = j->find("pid");
            const JsonValue* h = j->find("host");
            const std::int64_t other = pid != nullptr && pid->asInt() != nullptr ? *pid->asInt() : 0;
            const std::string otherHost =
                h != nullptr && h->asString() != nullptr ? *h->asString() : std::string();
            if (other == me && otherHost == host) {
                return true; // eigene Sperre
            }
            // Fremder Rechner: Prozess nicht prüfbar, Sperre gilt. Gleicher Rechner: nur, wenn er läuft.
            if (otherHost != host || processAlive(other)) {
                return false;
            }
        }
        // verwaiste oder unlesbare Sperre: übernehmen
    }
    if (!writable(folder)) {
        return true; // schreibgeschützt: keine Sperre möglich, Änderungen gehen in die Ersatzablage
    }
    const JsonValue j = JsonValue::object({{"pid", JsonValue(me)}, {"host", JsonValue(host)}});
    if (auto ok = writer_.write(p, dump(j)); !ok) {
        return std::unexpected(ok.error());
    }
    return true;
}

void JsonSidecarStore::unlock(const fs::path& folder)
{
    const fs::path p = folder / lockFile;
    const auto text = readText(p);
    const auto j = text ? parse(*text) : base::Expected<JsonValue>(base::makeError(base::ErrorCode::Io, ""));
    if (j) {
        const JsonValue* pid = j->find("pid");
        if (pid != nullptr && pid->asInt() != nullptr && *pid->asInt() == currentProcessId()) {
            std::error_code ec;
            fs::remove(p, ec);
        }
    }
}

#if defined(_WIN32)

std::int64_t JsonSidecarStore::currentProcessId()
{
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

std::string JsonSidecarStore::hostName()
{
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = sizeof buf;
    return GetComputerNameA(buf, &n) != 0 ? std::string(buf, n) : std::string("unbekannt");
}

bool JsonSidecarStore::processAlive(std::int64_t pid)
{
    if (pid <= 0) {
        return false;
    }
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED; // existiert, aber geschützt
    }
    const bool running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return running;
}

#else

std::int64_t JsonSidecarStore::currentProcessId()
{
    return static_cast<std::int64_t>(::getpid());
}

std::string JsonSidecarStore::hostName()
{
    char buf[256] = {};
    return ::gethostname(buf, sizeof buf - 1) == 0 ? std::string(buf) : std::string("unbekannt");
}

bool JsonSidecarStore::processAlive(std::int64_t pid)
{
    if (pid <= 0) {
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

#endif

} // namespace autoneg::adapter::json
