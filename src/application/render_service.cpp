#include "application/render_service.hpp"

#include <chrono>
#include <string>

namespace autoneg::application {

RenderService::RenderService(IRenderer& renderer, ReadyHandler onReady)
    : renderer_(renderer)
    , onReady_(std::move(onReady))
    , thread_([this](const std::stop_token& stop) { loop(stop); })
{}

RenderService::~RenderService()
{
    {
        const std::scoped_lock lock(mutex_);
        pending_.reset();
        if (running_) {
            running_->cancel();
        }
    }
    thread_.request_stop();
    cv_.notify_all();
}

std::uint64_t RenderService::request(pipeline::RenderRequest r)
{
    std::uint64_t seq = 0;
    {
        const std::scoped_lock lock(mutex_);
        seq = nextSeq_++;
        r.cancel = image::CancelToken{}; // eigenes Token je Anfrage
        pending_ = std::pair{seq, std::move(r)};
        if (running_) {
            running_->cancel(); // veraltete Berechnung abbrechen
        }
    }
    cv_.notify_all();
    return seq;
}

void RenderService::idle()
{
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !pending_ && !busy_; });
}

void RenderService::loop(const std::stop_token& stop)
{
    std::unique_lock lock(mutex_);
    while (true) {
        cv_.wait(lock, stop, [this] { return pending_.has_value(); });
        if (stop.stop_requested()) {
            return;
        }
        auto [seq, req] = std::move(*pending_);
        pending_.reset();
        running_ = req.cancel;
        busy_ = true;
        lock.unlock();
        const auto started = std::chrono::steady_clock::now();
        const char* mode = req.profile.mode == pipeline::RenderMode::Detail   ? "Detail"
                           : req.profile.mode == pipeline::RenderMode::Export ? "Export"
                                                                              : "Vorschau";
        auto img = renderer_.render(req);
        const bool stale = req.cancel.cancelled();
        const std::string what = "Rendern #" + std::to_string(seq) + " (" + mode + ", " + req.path.filename().string() + ")";
        if (stale) {
            log_.info(what + " veraltet, verworfen nach " + std::to_string(EventLog::msSince(started)) + " ms");
        } else if (!img) {
            log_.error(what + " fehlgeschlagen: " + img.error().message);
        } else {
            log_.info(what + " fertig in " + std::to_string(EventLog::msSince(started)) + " ms, " +
                      std::to_string(img->spec().width) + " x " + std::to_string(img->spec().height));
        }
        if (img && !stale && onReady_) {
            onReady_(seq, req, std::move(*img));
        }
        lock.lock();
        running_.reset();
        busy_ = false;
        cv_.notify_all();
    }
}

} // namespace autoneg::application
