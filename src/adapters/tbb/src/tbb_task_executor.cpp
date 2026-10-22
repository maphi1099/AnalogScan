#include "autoneg/adapter/tbb/tbb_task_executor.hpp"

#include <tbb/global_control.h>
#include <tbb/info.h>
#include <tbb/task_arena.h>

#include <array>
#include <condition_variable>
#include <mutex>

namespace autoneg::adapter::tbb {

namespace {

namespace oneapi = ::tbb;

oneapi::task_arena::priority priorityOf(ports::Priority p)
{
    switch (p) {
    case ports::Priority::Interactive:
        return oneapi::task_arena::priority::high;
    case ports::Priority::Background:
        return oneapi::task_arena::priority::normal;
    case ports::Priority::Batch:
        return oneapi::task_arena::priority::low;
    }
    return oneapi::task_arena::priority::normal;
}

int hardware()
{
    return oneapi::info::default_concurrency();
}

} // namespace

struct TbbTaskExecutor::Impl
{
    std::array<std::unique_ptr<oneapi::task_arena>, 3> arenas;
    std::unique_ptr<oneapi::global_control> limit;
    int workers = 0;
    std::mutex mutex;
    std::condition_variable cv;
    int pending = 0;
    bool closed = false;

    void setLimit(int n)
    {
        workers = n <= 0 ? hardware() : n;
        // max_allowed_parallelism zählt einen externen Thread mit; eingereihte Aufgaben laufen
        // nur auf Workern, deshalb workers + 1 (sonst bei workers = 1 gar kein Worker)
        limit.reset();
        limit = std::make_unique<oneapi::global_control>(oneapi::global_control::max_allowed_parallelism,
                                                         static_cast<std::size_t>(workers) + 1);
    }
};

TbbTaskExecutor::TbbTaskExecutor(int workers)
    : impl_(std::make_unique<Impl>())
{
    impl_->setLimit(workers);
    for (const ports::Priority p :
         {ports::Priority::Interactive, ports::Priority::Background, ports::Priority::Batch}) {
        impl_->arenas[static_cast<std::size_t>(p)] =
            std::make_unique<oneapi::task_arena>(static_cast<int>(oneapi::task_arena::automatic), 1,
                                                 priorityOf(p)); // als Wert (keine ODR-Nutzung)
    }
}

TbbTaskExecutor::~TbbTaskExecutor()
{
    shutdown();
}

void TbbTaskExecutor::enqueue(ports::Priority p, std::function<void()> task)
{
    {
        const std::scoped_lock lock(impl_->mutex);
        if (impl_->closed) {
            return;
        }
        ++impl_->pending;
    }
    impl_->arenas[static_cast<std::size_t>(p)]->enqueue([impl = impl_.get(), task = std::move(task)] {
        task();
        // Benachrichtigen unter der Sperre: sonst kann shutdown() zurückkehren und das Objekt
        // zerstören, bevor notify_all() die Bedingungsvariable benutzt (gefunden mit TSan)
        const std::scoped_lock lock(impl->mutex);
        --impl->pending;
        impl->cv.notify_all();
    });
}

void TbbTaskExecutor::run(ports::Priority p, const std::function<void()>& task)
{
    impl_->arenas[static_cast<std::size_t>(p)]->execute(task);
}

int TbbTaskExecutor::concurrency() const
{
    const std::scoped_lock lock(impl_->mutex);
    return impl_->workers;
}

void TbbTaskExecutor::setConcurrency(int workers)
{
    const std::scoped_lock lock(impl_->mutex);
    impl_->setLimit(workers);
}

void TbbTaskExecutor::shutdown()
{
    std::unique_lock lock(impl_->mutex);
    impl_->closed = true;
    impl_->cv.wait(lock, [this] { return impl_->pending == 0; });
}

} // namespace autoneg::adapter::tbb
