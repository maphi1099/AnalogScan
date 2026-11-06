#include "autoneg/composition.hpp"

#include "adapters/tbb/system_memory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>

namespace autoneg::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t stageCacheBytes = std::size_t{1} << 30; ///< 1 GB Zwischenstände (Architektur 5.4)
constexpr int analysisEdge = 1500;                            ///< Analyseauflösung, lange Kante

/// Standard-Speicherbudget: unbegrenzt, also der ganze Arbeitsspeicher ohne Auslagerung
/// (Wunsch des Nutzers, K-M10-15); ohne Angabe des Systems der Vorgabewert aus ThreadConfig.
std::size_t defaultMemoryBudget()
{
    const std::uint64_t ram = adapter::tbb::physicalMemoryBytes();
    if (ram == 0) {
        return application::ThreadConfig{}.memoryBudget;
    }
    return static_cast<std::size_t>(ram);
}

const pipeline::StepRegistry& registered(pipeline::StepRegistry& r, steps::FlatFieldCache& flat)
{
    if (auto ok = steps::registerStandardSteps(r, flat); !ok) {
        // Programmfehler (doppelte Kennung); ohne Schritte ist die Anwendung unbrauchbar
        std::abort();
    }
    return r;
}

std::optional<fs::path> envPath(const char* name)
{
#if defined(_WIN32)
    // Windows: Umgebung als UTF-16 lesen (Benutzernamen mit Umlauten), plattformabhängig
    wchar_t* value = nullptr;
    std::size_t size = 0;
    std::wstring wname(name, name + std::char_traits<char>::length(name));
    std::optional<fs::path> out;
    if (_wdupenv_s(&value, &size, wname.c_str()) == 0 && value != nullptr && *value != L'\0') {
        out = fs::path(value);
    }
    std::free(value);
    return out;
#else
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? std::optional<fs::path>(v) : std::nullopt;
#endif
}

} // namespace

Directories userDirectories()
{
#if defined(_WIN32)
    const fs::path roaming = envPath("APPDATA").value_or(fs::temp_directory_path());
    const fs::path local = envPath("LOCALAPPDATA").value_or(roaming);
    return {roaming / "AutoNeg", local / "AutoNeg"};
#else
    const fs::path home = envPath("HOME").value_or(fs::temp_directory_path());
    const fs::path config = envPath("XDG_CONFIG_HOME").value_or(home / ".config");
    const fs::path cache = envPath("XDG_CACHE_HOME").value_or(home / ".cache");
    return {config / "autoneg", cache / "autoneg"};
#endif
}

Composition::Composition(Directories dirs, application::RenderService::ReadyHandler sink,
                         application::UiDispatcher dispatcher, ports::IDocumentRenderer& document)
    : dirs_(std::move(dirs))
    , logger_(dirs_.cache / "autoneg.log")
    , compiler_(registered(registry_, flatFields_))
    , cache_(stageCacheBytes)
    , sources_(decoder_, reader_)
    , pipeline_(compiler_, backend_, cache_, sources_)
    , renderer_(pipeline_)
    , store_(application::stepCatalog(registry_), fileWriter_, migrator_, hasher_, dirs_.config / "sidecars")
    , config_(dirs_.config, fileWriter_, {})
    , thumbnailCache_(dirs_.cache / "thumbnails")
    , codec_(application::stepCatalog(registry_))
    , queue_(store_,
             [this](const model::Frame& f, const base::Error& e) {
                 events_.log(ports::LogLevel::Error, "Begleitdatei für " + f.path.filename().string() +
                                                         " nicht gespeichert: " + e.message);
             })
    , bus_(registry_, queue_)
    , jobs_(executor_)
    , render_(renderer_, sink ? std::move(sink)
                              : application::RenderService::ReadyHandler(
                                    [](std::uint64_t, const pipeline::RenderRequest&, image::ImageBuffer) {}))
    , rolls_(jobs_, store_, store_, hasher_)
    , thumbnails_(jobs_, renderer_, color_, writer_, thumbnailCache_,
                  [this](const model::FrameId& f) { releaseUnlessCurrent(f); })
    , analysis_(jobs_, bus_, std::move(dispatcher),
                application::AnalysisFunctions::standard(
                    vision_, [this](const model::Frame& f) { return analysisImage(f); }))
    , exports_(jobs_, renderer_, color_, writer_,
               [this](const model::FrameId& f) { releaseUnlessCurrent(f); })
    , contactSheet_(jobs_, renderer_, color_, document,
                    [this](const model::FrameId& f) { releaseUnlessCurrent(f); })
{
    events_.add(&logger_);
#ifndef NDEBUG
    // Debug-Bau: Sitzungsprotokoll aller internen Ereignisse (Aufträge, Befehle, Rendern,
    // Speichern, Bedienung, Meldungen) mit Uhrzeit und Thread; eine Datei je Sitzung, nie
    // rotiert oder gelöscht (Wunsch des Nutzers für Langzeittests)
    {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        const std::string stamp =
            std::format("{:%Y-%m-%d_%H-%M-%S}", std::chrono::zoned_time{std::chrono::current_zone(),
                                                                         std::chrono::floor<std::chrono::seconds>(now)}) +
            std::format("-{:03}", ms);
        sessionLogFile_ = dirs_.cache / "logs" / ("autoneg-" + stamp + ".log");
        sessionLog_ = std::make_unique<adapter::spdlog::SpdLogger>(
            sessionLogFile_, std::numeric_limits<std::size_t>::max(), 1, ports::LogLevel::Info, ports::LogLevel::Info);
        events_.add(sessionLog_.get());
        jobs_.setLogger(sessionLog_.get());
        bus_.setLogger(sessionLog_.get());
        render_.setLogger(sessionLog_.get());
        queue_.setLogger(sessionLog_.get());
        sessionLog_->log(ports::LogLevel::Info, "Sitzungsprotokoll " + sessionLogFile_.string() + "; Einstellungen " +
                                                    dirs_.config.string() + "; Zwischenspeicher " + dirs_.cache.string());
    }
#endif
    // Gespeicherte Threadzahl und Speicherbudget (LF-DAT-04, NF-PERF-12)
    application::ThreadConfig c;
    c.memoryBudget = defaultMemoryBudget();
    if (const auto v = config_.get("threads"); v && v->number()) {
        c.workers = std::clamp(static_cast<int>(*v->number()), 0, 256); // wie im Dialog
    }
    if (const auto v = config_.get("memoryBudgetMb");
        v && v->number() && *v->number() > 0) { // 0 = unbegrenzt
        c.memoryBudget = static_cast<std::size_t>(*v->number()) << 20U;
    }
    jobs_.configure(c);
    if (!config_.loadMessage().empty()) {
        events_.log(ports::LogLevel::Warning, config_.loadMessage());
    }
    events_.log(ports::LogLevel::Info,
                "AutoNeg gestartet; Threads " + std::to_string(executor_.concurrency()));
}

Composition::~Composition()
{
    // Erst alle Jobs und das Rendern beenden, dann alles Gespeicherte schreiben
    jobs_.shutdown();
    render_.idle();
    queue_.flush();
    events_.log(ports::LogLevel::Info, "AutoNeg beendet");
}

std::string Composition::language()
{
    const auto v = config_.get("language");
    return v && v->asString() != nullptr ? *v->asString() : std::string("de");
}

void Composition::releaseUnlessCurrent(const model::FrameId& f)
{
    // Quelle freigeben, außer das Bild ist gerade in Bearbeitung (Speicher, NF-PERF-07)
    const std::scoped_lock lock(currentMutex_);
    if (f != current_) {
        sources_.release(f);
    }
}

void Composition::setCurrent(const model::FrameId& f)
{
    const std::scoped_lock lock(currentMutex_);
    current_ = f;
}

base::Expected<model::SourceInfo> Composition::sourceInfo(const model::Frame& f)
{
    return sources_.info(f.id, f.path);
}

model::JsonValue Composition::encodeEdits(const model::EditList& l) const
{
    return codec_.encode(l, {});
}

model::EditList Composition::decodeEdits(const model::JsonValue& j) const
{
    return codec_.decode(j).edits;
}

base::Expected<image::ImageBuffer> Composition::analysisImage(const model::Frame& f)
{
    auto img = sources_.source(f.id, f.path, 1);
    if (!img) {
        return img;
    }
    const double k = static_cast<double>(analysisEdge) / std::max(img->spec().width, img->spec().height);
    if (k >= 1.0) {
        return img;
    }
    const base::Size size{std::max(1, static_cast<int>(std::lround(img->spec().width * k))),
                          std::max(1, static_cast<int>(std::lround(img->spec().height * k)))};
    auto small = backend_.resample({}, *img, size);
    // Quelle freigeben: sonst hält die Erkennung einer ganzen Rolle alle Scans im Speicher
    releaseUnlessCurrent(f.id);
    return small;
}

} // namespace autoneg::app
