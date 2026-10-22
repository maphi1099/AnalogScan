#include "autoneg/application/render_service.hpp"

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
        auto img = renderer_.render(req);
        const bool stale = req.cancel.cancelled();
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
