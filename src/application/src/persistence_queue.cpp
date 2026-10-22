#include "autoneg/application/persistence_queue.hpp"

namespace autoneg::application {

PersistenceQueue::PersistenceQueue(ports::IEditRepository& repo, ErrorHandler onError)
    : repo_(repo)
    , onError_(std::move(onError))
    , thread_([this](const std::stop_token& stop) { loop(stop); })
{}

PersistenceQueue::~PersistenceQueue()
{
    flush();
    thread_.request_stop();
    cv_.notify_all();
}

void PersistenceQueue::enqueue(model::Frame f, ports::SidecarData d)
{
    {
        const std::scoped_lock lock(mutex_);
        const std::string key = f.path.generic_string();
        latest_.insert_or_assign(key, Pending{std::move(f), std::move(d)}); // älteren Stand ersetzen
    }
    cv_.notify_all();
}

void PersistenceQueue::flush()
{
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return latest_.empty() && writing_ == 0; });
}

void PersistenceQueue::loop(const std::stop_token& stop)
{
    std::unique_lock lock(mutex_);
    while (true) {
        cv_.wait(lock, stop, [this] { return !latest_.empty(); });
        if (latest_.empty()) {
            return; // Stopp angefordert und nichts mehr zu tun
        }
        auto node = latest_.extract(latest_.begin());
        ++writing_;
        lock.unlock();
        const Pending& p = node.mapped();
        const auto ok = repo_.save(p.frame, p.data);
        if (!ok && onError_) {
            onError_(p.frame, ok.error());
        }
        lock.lock();
        --writing_;
        cv_.notify_all(); // flush() wartet darauf
    }
}

} // namespace autoneg::application
