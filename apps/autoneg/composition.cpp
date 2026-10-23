#include "autoneg/app/composition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace autoneg::app {

namespace {

constexpr std::size_t stageCacheBytes = std::size_t{1} << 30; ///< 1 GB Zwischenstände (Architektur 5.4)
constexpr int analysisEdge = 1500;                            ///< Analyseauflösung, lange Kante

const pipeline::StepRegistry& registered(pipeline::StepRegistry& r, steps::FlatFieldCache& flat)
{
    if (auto ok = steps::registerStandardSteps(r, flat); !ok) {
        // Programmfehler (doppelte Kennung); ohne Schritte ist die Anwendung unbrauchbar
        std::abort();
    }
    return r;
}

} // namespace

Composition::Composition(ui::GuiApplication& gui, Directories dirs)
    : dirs_(std::move(dirs))
    , compiler_(registered(registry_, flatFields_))
    , cache_(stageCacheBytes)
    , sources_(decoder_, reader_)
    , executor2_(compiler_, backend_, cache_, sources_)
    , renderer_(executor2_)
    , store_(application::stepCatalog(registry_), fileWriter_, migrator_, hasher_, dirs_.config / "sidecars")
    , config_(dirs_.config, fileWriter_, {})
    , thumbnailCache_(dirs_.cache / "thumbnails")
    , codec_(application::stepCatalog(registry_))
    , queue_(store_)
    , bus_(registry_, queue_)
    , jobs_(executor_)
    , render_(renderer_, gui.previewSink())
    , rolls_(jobs_, store_, store_, hasher_)
    , thumbnails_(jobs_, renderer_, color_, writer_, thumbnailCache_,
                  [this](const model::FrameId& f) {
                      // Quelle freigeben, außer das Bild ist gerade in Bearbeitung (Speicher, NF-PERF-07)
                      const std::scoped_lock lock(currentMutex_);
                      if (f != current_) {
                          sources_.release(f);
                      }
                  })
    , analysis_(jobs_, bus_, gui.dispatcher(),
                application::AnalysisFunctions::standard(
                    vision_, [this](const model::Frame& f) { return analysisImage(f); }))
    , exports_(jobs_, renderer_, color_, writer_)
    , contactSheet_(jobs_, renderer_, color_, document_)
{
    // Gespeicherte Threadzahl und Speicherbudget (LF-DAT-04, NF-PERF-12)
    application::ThreadConfig c;
    if (const auto v = config_.get("threads"); v && v->number()) {
        c.workers = static_cast<int>(*v->number());
    }
    if (const auto v = config_.get("memoryBudgetMb"); v && v->number()) {
        c.memoryBudget = static_cast<std::size_t>(*v->number()) << 20U;
    }
    jobs_.configure(c);
}

std::string Composition::language()
{
    const auto v = config_.get("language");
    return v && v->asString() != nullptr ? *v->asString() : std::string("de");
}

Composition::~Composition()
{
    // Erst alle Jobs und das Rendern beenden, dann alles Gespeicherte schreiben
    jobs_.shutdown();
    render_.idle();
    queue_.flush();
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
    return backend_.resample({}, *img, size);
}

ui::GuiServices Composition::services()
{
    return ui::GuiServices{
        registry_,
        jobs_,
        bus_,
        session_,
        sync_,
        clipboard_,
        render_,
        rolls_,
        thumbnails_,
        analysis_,
        exports_,
        contactSheet_,
        color_,
        config_,
        config_,
        [this](const model::Frame& f) { return sources_.info(f.id, f.path); },
        [this](const model::FrameId& f) {
            const std::scoped_lock lock(currentMutex_);
            current_ = f;
        },
        [this](const model::EditList& l) { return codec_.encode(l, {}); },
        [this](const model::JsonValue& j) { return codec_.decode(j).edits; },
    };
}

} // namespace autoneg::app
