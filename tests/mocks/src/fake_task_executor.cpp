#include "autoneg/mocks/fake_task_executor.hpp"

#include <algorithm>

namespace autoneg::mocks {

FakeTaskExecutor::FakeTaskExecutor(int maxThreads)
    : maxThreads_(std::max(1, maxThreads))
    , limit_(maxThreads_)
{
    for (int i = 0; i < maxThreads_; ++i) {
        threads_.emplace_back([this](const std::stop_token& stop) { worker(stop); });
    }
}

FakeTaskExecutor::~FakeTaskExecutor()
{
    shutdown();
    for (auto& t : threads_) {
        t.request_stop();
    }
    cv_.notify_all();
}

void FakeTaskExecutor::enqueue(ports::Priority p, std::function<void()> task)
{
    {
        const std::scoped_lock lock(mutex_);
        if (closed_) {
            return;
        }
        queues_[static_cast<std::size_t>(p)].push_back(std::move(task));
    }
    cv_.notify_all();
}

void FakeTaskExecutor::run(ports::Priority /*p*/, const std::function<void()>& task)
{
    task(); // im aufrufenden Thread, wie task_arena::execute
}

int FakeTaskExecutor::concurrency() const
{
    const std::scoped_lock lock(mutex_);
    return limit_;
}

void FakeTaskExecutor::setConcurrency(int workers)
{
    {
        const std::scoped_lock lock(mutex_);
        limit_ = workers <= 0 ? maxThreads_ : std::min(workers, maxThreads_);
    }
    cv_.notify_all();
}

void FakeTaskExecutor::shutdown()
{
    std::unique_lock lock(mutex_);
    closed_ = true;
    cv_.wait(lock, [this] {
        return active_ == 0 && std::ranges::all_of(queues_, [](const auto& q) { return q.empty(); });
    });
}

void FakeTaskExecutor::worker(const std::stop_token& stop)
{
    std::unique_lock lock(mutex_);
    while (true) {
        cv_.wait(lock, stop, [this] {
            return active_ < limit_ && std::ranges::any_of(queues_, [](const auto& q) { return !q.empty(); });
        });
        if (stop.stop_requested()) {
            return;
        }
        auto queue = std::ranges::find_if(queues_, [](const auto& q) { return !q.empty(); }); // Priorität
        std::function<void()> task = std::move(queue->front());
        queue->pop_front();
        ++active_;
        lock.unlock();
        task();
        lock.lock();
        --active_;
        cv_.notify_all();
    }
}

} // namespace autoneg::mocks
