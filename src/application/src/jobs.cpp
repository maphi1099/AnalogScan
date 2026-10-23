#include "autoneg/application/jobs.hpp"

#include <algorithm>
#include <utility>

namespace autoneg::application {

// --- MemoryBudget --------------------------------------------------------------------------

MemoryBudget::Lease::Lease(Lease&& o) noexcept
    : budget_(std::exchange(o.budget_, nullptr))
    , bytes_(std::exchange(o.bytes_, 0))
{}

MemoryBudget::Lease& MemoryBudget::Lease::operator=(Lease&& o) noexcept
{
    if (this != &o) {
        if (budget_ != nullptr) {
            budget_->release(bytes_);
        }
        budget_ = std::exchange(o.budget_, nullptr);
        bytes_ = std::exchange(o.bytes_, 0);
    }
    return *this;
}

MemoryBudget::Lease::~Lease()
{
    if (budget_ != nullptr) {
        budget_->release(bytes_);
    }
}

base::Expected<MemoryBudget::Lease> MemoryBudget::acquire(std::size_t bytes)
{
    std::unique_lock lock(mutex_);
    if (bytes > total_) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Anforderung größer als das Speicherbudget");
    }
    cv_.wait(lock, [&] { return used_ + bytes <= total_; });
    used_ += bytes;
    return Lease(this, bytes);
}

MemoryBudget::Lease MemoryBudget::acquireUpTo(std::size_t bytes)
{
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return used_ + std::min(bytes, total_) <= total_; });
    const std::size_t n = std::min(bytes, total_);
    used_ += n;
    return Lease(this, n);
}

std::optional<MemoryBudget::Lease> MemoryBudget::tryAcquire(std::size_t bytes)
{
    const std::scoped_lock lock(mutex_);
    if (bytes > total_ || used_ + bytes > total_) {
        return std::nullopt;
    }
    used_ += bytes;
    return Lease(this, bytes);
}

std::size_t MemoryBudget::total() const
{
    const std::scoped_lock lock(mutex_);
    return total_;
}

void MemoryBudget::setTotal(std::size_t bytes)
{
    const std::scoped_lock lock(mutex_);
    total_ = bytes;
    cv_.notify_all();
}

std::size_t MemoryBudget::used() const
{
    const std::scoped_lock lock(mutex_);
    return used_;
}

void MemoryBudget::release(std::size_t bytes)
{
    const std::scoped_lock lock(mutex_); // unter der Sperre benachrichtigen (wie TbbTaskExecutor)
    used_ -= bytes;
    cv_.notify_all();
}

// --- JobHandle -----------------------------------------------------------------------------

namespace detail {

struct JobState
{
    std::mutex mutex;
    std::condition_variable cv;
    JobStatus status = JobStatus::Queued;
    std::optional<base::Error> error;
    image::CancelToken cancel;
    std::atomic<double> progress{0.0};
};

} // namespace detail

double JobHandle::progress() const
{
    return state_ ? state_->progress.load() : 0.0;
}

JobStatus JobHandle::status() const
{
    if (!state_) {
        return JobStatus::Cancelled;
    }
    const std::scoped_lock lock(state_->mutex);
    return state_->status;
}

void JobHandle::cancel() const
{
    if (state_) {
        state_->cancel.cancel();
    }
}

bool JobHandle::done() const
{
    const JobStatus s = status();
    return s == JobStatus::Done || s == JobStatus::Failed || s == JobStatus::Cancelled;
}

base::Expected<void> JobHandle::wait() const
{
    if (!state_) {
        return base::makeError(base::ErrorCode::Cancelled, "kein Job");
    }
    std::unique_lock lock(state_->mutex);
    state_->cv.wait(lock, [this] {
        return state_->status == JobStatus::Done || state_->status == JobStatus::Failed ||
               state_->status == JobStatus::Cancelled;
    });
    if (state_->status == JobStatus::Cancelled) {
        return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
    }
    if (state_->error) {
        return std::unexpected(*state_->error);
    }
    return {};
}

// --- JobScheduler --------------------------------------------------------------------------

namespace {

void finish(detail::JobState& s, JobStatus status, std::optional<base::Error> error = std::nullopt)
{
    {
        const std::scoped_lock lock(s.mutex);
        s.status = status;
        s.error = std::move(error);
    }
    s.cv.notify_all();
}

} // namespace

JobScheduler::~JobScheduler()
{
    shutdown();
}

JobHandle JobScheduler::submit(std::unique_ptr<Job> job, ports::Priority p)
{
    auto state = std::make_shared<detail::JobState>();
    {
        const std::scoped_lock lock(mutex_);
        if (closed_) {
            finish(*state, JobStatus::Cancelled);
            return JobHandle(state);
        }
        std::erase_if(jobs_, [](const std::weak_ptr<detail::JobState>& w) { return w.expired(); });
        jobs_.push_back(state);
    }
    std::shared_ptr<Job> shared(std::move(job));
    executor_.enqueue(p, [state, shared, p] {
        if (state->cancel.cancelled()) {
            finish(*state, JobStatus::Cancelled); // vor dem Start abgebrochen
            return;
        }
        {
            const std::scoped_lock lock(state->mutex);
            state->status = JobStatus::Running;
        }
        JobContext ctx;
        ctx.cancel = state->cancel;
        ctx.priority = p;
        ctx.progress = [state](int done, int total) {
            state->progress.store(total > 0 ? static_cast<double>(done) / total : 0.0);
        };
        const auto r = shared->run(ctx);
        if (state->cancel.cancelled() || (!r && r.error().code == base::ErrorCode::Cancelled)) {
            finish(*state, JobStatus::Cancelled);
        } else if (!r) {
            finish(*state, JobStatus::Failed, r.error());
        } else {
            state->progress.store(1.0);
            finish(*state, JobStatus::Done);
        }
    });
    return JobHandle(state);
}

void JobScheduler::configure(const ThreadConfig& c)
{
    executor_.setConcurrency(c.workers);
    budget_.setTotal(c.memoryBudget);
}

void JobScheduler::parallelFor(ports::Priority p, std::size_t count, std::size_t lanes,
                               const std::function<void(std::size_t)>& body)
{
    // Gemeinsamer Zustand lebt so lange wie die letzte Bahn; späte Bahnen finden nichts mehr
    // zu tun und berühren body nicht (closed)
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::size_t next = 0;
        std::size_t count = 0;
        std::size_t active = 0;
        bool closed = false;
        const std::function<void(std::size_t)>* body = nullptr;
    };
    auto s = std::make_shared<Shared>();
    s->count = count;
    s->body = &body;
    const auto lane = [s] {
        for (;;) {
            std::size_t i = 0;
            {
                const std::scoped_lock lock(s->mutex);
                if (s->closed || s->next >= s->count) {
                    return;
                }
                i = s->next++;
                ++s->active;
            }
            (*s->body)(i);
            const std::scoped_lock lock(s->mutex); // unter der Sperre benachrichtigen (TSan)
            --s->active;
            s->cv.notify_all();
        }
    };
    const std::size_t used = std::min(lanes, count);
    for (std::size_t k = 1; k < used; ++k) {
        executor_.enqueue(p, lane);
    }
    lane();
    std::unique_lock lock(s->mutex);
    s->closed = true;
    s->cv.wait(lock, [&] { return s->active == 0; });
}

void JobScheduler::shutdown()
{
    std::vector<std::shared_ptr<detail::JobState>> open;
    {
        const std::scoped_lock lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        for (const auto& w : jobs_) {
            if (auto s = w.lock()) {
                open.push_back(std::move(s));
            }
        }
    }
    for (const auto& s : open) {
        s->cancel.cancel();
    }
    executor_.shutdown(); // wartet auf alle eingereihten Aufgaben; abgebrochene enden schnell
}

} // namespace autoneg::application
