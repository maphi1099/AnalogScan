#include "autoneg/application/thumbnail_service.hpp"

namespace autoneg::application {

std::shared_future<ThumbnailService::Result>
ThumbnailService::request(const model::Frame& f, const model::EditList& edits, model::FilmType film, int size)
{
    return start(f, edits, film, size, {});
}

void ThumbnailService::request(const model::Frame& f, const model::EditList& edits, model::FilmType film,
                               int size, Done done)
{
    (void)start(f, edits, film, size, std::move(done));
}

std::shared_future<ThumbnailService::Result> ThumbnailService::start(const model::Frame& f,
                                                                     const model::EditList& edits,
                                                                     model::FilmType film, int size,
                                                                     Done done)
{
    const std::uint64_t editHash = edits.hash() ^ (static_cast<std::uint64_t>(film) << 56U);
    const Key key{f.id, size, editHash};
    // Mit Rückruf prüft erst der Job den Cache: der Rückruf dekodiert das JPEG und soll nie
    // im UI-Thread laufen
    if (!done) {
        if (auto hit = cache_.thumbnail(f.id, size, editHash)) {
            std::promise<Result> p;
            p.set_value(std::move(*hit));
            return p.get_future().share();
        }
    }
    auto promise = std::make_shared<std::promise<Result>>();
    std::shared_future<Result> future;
    {
        const std::scoped_lock lock(mtx_);
        if (const auto it = pending_.find(key); it != pending_.end()) {
            if (done) {
                it->second.waiting.push_back(std::move(done));
            }
            return it->second.future;
        }
        future = promise->get_future().share();
        Pending p{future, {}};
        if (done) {
            p.waiting.push_back(std::move(done));
        }
        pending_.emplace(key, std::move(p));
    }

    class ThumbJob final : public Job
    {
    public:
        ThumbJob(ThumbnailService& svc, model::Frame f, model::EditList edits, model::FilmType film, int size,
                 Key key, std::shared_ptr<std::promise<Result>> promise)
            : svc_(svc)
            , f_(std::move(f))
            , edits_(std::move(edits))
            , film_(film)
            , size_(size)
            , key_(std::move(key))
            , promise_(std::move(promise))
        {}
        ~ThumbJob() override
        {
            if (!delivered_) { // vor dem Start abgebrochen: Wartende nicht hängen lassen
                finish(base::makeError(base::ErrorCode::Cancelled, "Miniatur abgebrochen"));
            }
        }
        std::string name() const override { return "Miniatur"; }
        base::Expected<void> run(JobContext& c) override
        {
            Result r = svc_.make(f_, edits_, film_, size_, c.cancel);
            base::Expected<void> status = r ? base::Expected<void>{} : std::unexpected(r.error());
            finish(std::move(r));
            return status;
        }

    private:
        void finish(Result r)
        {
            delivered_ = true;
            std::vector<Done> waiting;
            {
                const std::scoped_lock lock(svc_.mtx_);
                if (const auto it = svc_.pending_.find(key_); it != svc_.pending_.end()) {
                    waiting = std::move(it->second.waiting);
                    svc_.pending_.erase(it);
                }
            }
            promise_->set_value(r);
            for (const Done& d : waiting) {
                d(r);
            }
        }
        ThumbnailService& svc_;
        model::Frame f_;
        model::EditList edits_;
        model::FilmType film_;
        int size_;
        Key key_;
        std::shared_ptr<std::promise<Result>> promise_;
        bool delivered_ = false;
    };
    jobs_.submit(std::make_unique<ThumbJob>(*this, f, edits, film, size, key, promise),
                 ports::Priority::Background);
    return future;
}

ThumbnailService::Result ThumbnailService::make(const model::Frame& f, const model::EditList& edits,
                                                model::FilmType film, int size,
                                                const image::CancelToken& cancel)
{
    const std::uint64_t editHash = edits.hash() ^ (static_cast<std::uint64_t>(film) << 56U);
    if (auto hit = cache_.thumbnail(f.id, size, editHash)) {
        return std::move(*hit);
    }
    auto working = color_.profile(ports::ProfileId::WorkingRec2020Linear);
    auto srgb = color_.profile(ports::ProfileId::Srgb);
    if (!working || !srgb) {
        return base::makeError(base::ErrorCode::Internal, "Farbprofile nicht verfügbar");
    }
    auto xf = color_.transform(*working, *srgb, ports::Intent::Perceptual);
    if (!xf) {
        return std::unexpected(xf.error());
    }
    pipeline::RenderRequest r;
    r.frame = f.id;
    r.path = f.path;
    r.list = std::make_shared<const model::EditList>(edits);
    r.film = film;
    r.profile.mode = pipeline::RenderMode::Thumbnail;
    r.profile.target = {size, size};
    r.profile.priority = ports::Priority::Background;
    r.profile.output.transform = *xf;
    r.cancel = cancel;
    // Speicherbudget wie beim Stapel (NF-PERF-11): sonst rendert jeder Worker eine Miniatur
    // aus einer dekodierten Quelle, bei 36 RAW-Scans rund 16 GB gleichzeitig
    std::size_t bytes = 0;
    if (const auto info = renderer_.info(f.id, f.path); info) {
        bytes = static_cast<std::size_t>(info->width) * static_cast<std::size_t>(info->height) *
                proxyRenderBytesPerPixel;
    }
    const MemoryBudget::Lease lease = jobs_.budget().acquireUpTo(bytes);
    auto display = renderer_.render(r);
    if (release_) {
        release_(f.id);
    }
    if (!display) {
        return std::unexpected(display.error());
    }
    ports::WriteOptions o;
    o.format = ports::FileFormat::Jpeg;
    o.bitDepth = 8;
    o.quality = 85;
    o.icc = *srgb;
    auto jpeg = encoder_.encode(*display, o);
    if (!jpeg) {
        return std::unexpected(jpeg.error());
    }
    cache_.put(f.id, size, editHash, *jpeg);
    return std::move(*jpeg);
}

} // namespace autoneg::application
