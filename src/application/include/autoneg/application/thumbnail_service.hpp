#pragma once

#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/model/roll.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/repositories.hpp"

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace autoneg::application {

/// Miniaturen für den Filmstreifen (Architektur 9.13, LF-IMP-07, NF-PERF-06).
///
/// Ergebnis ist das JPEG (sRGB) der Miniatur, wie es im ICacheStore liegt; die Oberfläche
/// dekodiert es selbst. Schlüssel: Prüfsumme des Scans, Größenklasse, Prüfsumme der
/// Bearbeitungsliste. Treffer im Cache kommen sofort; sonst rendert ein Hintergrund-Job
/// (RenderMode::Thumbnail), wandelt nach sRGB, codiert als JPEG und legt es im Cache ab.
/// Gleichzeitige Anfragen mit gleichem Schlüssel rechnen einmal. Threadsicher.
class ThumbnailService
{
public:
    using Result = base::Expected<base::Bytes>;
    /// Wird nach dem Rendern aufgerufen, damit die dekodierte Quelle freigegeben werden kann
    /// (SourceProvider::release), außer das Bild ist gerade in Bearbeitung.
    using Release = std::function<void(const model::FrameId&)>;

    ThumbnailService(JobScheduler& jobs, IRenderer& renderer, ports::IColorEngine& color,
                     ports::IImageWriter& encoder, ports::ICacheStore& cache, Release release = {})
        : jobs_(jobs)
        , renderer_(renderer)
        , color_(color)
        , encoder_(encoder)
        , cache_(cache)
        , release_(std::move(release))
    {}

    using Done = std::function<void(const Result&)>;

    /// Größenklasse: längere Seite in Pixel.
    [[nodiscard]] std::shared_future<Result> request(const model::Frame& f, const model::EditList& edits,
                                                     model::FilmType film, int size);
    /// Wie oben, done wird mit dem Ergebnis immer im Job-Thread aufgerufen (auch bei einem
    /// Cache-Treffer), damit die Oberfläche dort dekodieren kann.
    void request(const model::Frame& f, const model::EditList& edits, model::FilmType film, int size,
                 Done done);

private:
    using Key = std::tuple<model::FrameId, int, std::uint64_t>;
    struct Pending
    {
        std::shared_future<Result> future;
        std::vector<Done> waiting;
    };
    std::shared_future<Result> start(const model::Frame& f, const model::EditList& edits,
                                     model::FilmType film, int size, Done done);
    Result make(const model::Frame& f, const model::EditList& edits, model::FilmType film, int size,
                const image::CancelToken& cancel);

    JobScheduler& jobs_;
    IRenderer& renderer_;
    ports::IColorEngine& color_;
    ports::IImageWriter& encoder_;
    ports::ICacheStore& cache_;
    Release release_;
    std::mutex mtx_;
    std::map<Key, Pending> pending_;
};

} // namespace autoneg::application
