#pragma once

#include "autoneg/ports/task_executor.hpp"

#include <memory>

namespace autoneg::adapter::tbb {

/// Worker-Pool mit drei Arenen auf oneTBB (Architektur 6.2, A-02, D-12).
///
/// Je Priorität eine tbb::task_arena (hoch, normal, niedrig); freie Worker bedienen zuerst
/// die Arena höherer Priorität. Datenparallele Kernels innerhalb einer Aufgabe laufen in
/// derselben Arena. Die Threadzahl begrenzt tbb::global_control und wirkt sofort auf neue
/// Aufgaben (NF-PERF-12). Threadsicher.
class TbbTaskExecutor final : public ports::ITaskExecutor
{
public:
    explicit TbbTaskExecutor(int workers = 0);
    ~TbbTaskExecutor() override;
    TbbTaskExecutor(const TbbTaskExecutor&) = delete;
    TbbTaskExecutor& operator=(const TbbTaskExecutor&) = delete;
    TbbTaskExecutor(TbbTaskExecutor&&) = delete;
    TbbTaskExecutor& operator=(TbbTaskExecutor&&) = delete;

    void enqueue(ports::Priority p, std::function<void()> task) override;
    void run(ports::Priority p, const std::function<void()>& task) override;
    [[nodiscard]] int concurrency() const override;
    void setConcurrency(int workers) override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace autoneg::adapter::tbb
