#pragma once

// Fake von ports::ITaskExecutor: eigener Thread-Pool mit drei Warteschlangen; freie Threads
// nehmen Interactive vor Background vor Batch. Vertragstreu (K-14), ohne oneTBB.

#include "autoneg/ports/task_executor.hpp"

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace autoneg::mocks {

class FakeTaskExecutor final : public ports::ITaskExecutor
{
public:
    /// maxThreads: Größe des Pools; aktiv sind höchstens concurrency() davon.
    explicit FakeTaskExecutor(int maxThreads = 8);
    ~FakeTaskExecutor() override;
    FakeTaskExecutor(const FakeTaskExecutor&) = delete;
    FakeTaskExecutor& operator=(const FakeTaskExecutor&) = delete;
    FakeTaskExecutor(FakeTaskExecutor&&) = delete;
    FakeTaskExecutor& operator=(FakeTaskExecutor&&) = delete;

    void enqueue(ports::Priority p, std::function<void()> task) override;
    void run(ports::Priority p, const std::function<void()>& task) override;
    [[nodiscard]] int concurrency() const override;
    void setConcurrency(int workers) override;
    void shutdown() override;

private:
    void worker(const std::stop_token& stop);

    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::array<std::deque<std::function<void()>>, 3> queues_;
    int maxThreads_;
    int limit_;
    int active_ = 0;
    bool closed_ = false;
    std::vector<std::jthread> threads_;
};

} // namespace autoneg::mocks
