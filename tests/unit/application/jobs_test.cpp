// Unit-Tests Nebenläufigkeit der Anwendung (Architektur 6, 9.13): JobScheduler, MemoryBudget,
// RenderService. Der Worker-Pool ist der FakeTaskExecutor (tests/mocks).

#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/mocks/fake_task_executor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace autoneg::application {
namespace {

using namespace std::chrono_literals;

/// Job aus einer Funktion.
class FnJob final : public Job
{
public:
    explicit FnJob(std::function<base::Expected<void>(JobContext&)> f)
        : f_(std::move(f))
    {}
    std::string name() const override { return "Test"; }
    base::Expected<void> run(JobContext& c) override { return f_(c); }

private:
    std::function<base::Expected<void>(JobContext&)> f_;
};

std::unique_ptr<Job> job(std::function<base::Expected<void>(JobContext&)> f)
{
    return std::make_unique<FnJob>(std::move(f));
}

// NF-PERF-10
TEST(JobSchedulerTest, U_APP_09_InteractiveBeforeQueuedBatch)
{
    mocks::FakeTaskExecutor executor(2);
    JobScheduler scheduler(executor);
    std::mutex mutex;
    std::vector<std::string> started;
    std::atomic<bool> release{false};
    const auto record = [&](std::string name) {
        return job([&, name](JobContext&) -> base::Expected<void> {
            {
                const std::scoped_lock lock(mutex);
                started.push_back(name);
            }
            while (!release.load()) {
                std::this_thread::sleep_for(1ms);
            }
            return {};
        });
    };
    std::vector<JobHandle> handles;
    for (int i = 0; i < 6; ++i) { // Stapel füllt beide Threads, 4 warten
        handles.push_back(scheduler.submit(record("stapel" + std::to_string(i)), ports::Priority::Batch));
    }
    while (true) {
        const std::scoped_lock lock(mutex);
        if (started.size() == 2) {
            break;
        }
    }
    handles.push_back(scheduler.submit(record("interaktiv"), ports::Priority::Interactive));
    release = true;
    for (const JobHandle& h : handles) {
        ASSERT_TRUE(h.wait().has_value());
    }
    ASSERT_EQ(started.size(), 7U);
    EXPECT_EQ(started[2], "interaktiv"); // nächster freier Thread nimmt zuerst die höhere Priorität
}

// LF-BAT-09
TEST(JobSchedulerTest, U_APP_10_CancelAndShutdown)
{
    mocks::FakeTaskExecutor executor(2);
    JobScheduler scheduler(executor);
    const auto longRunning = [] {
        return job([](JobContext& c) -> base::Expected<void> {
            for (int i = 0; i < 2000 && !c.cancel.cancelled(); ++i) {
                std::this_thread::sleep_for(1ms);
                c.progress(i, 2000);
            }
            if (c.cancel.cancelled()) {
                return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
            }
            return {};
        });
    };
    const JobHandle a = scheduler.submit(longRunning(), ports::Priority::Batch);
    const JobHandle b = scheduler.submit(longRunning(), ports::Priority::Batch);
    const JobHandle queued = scheduler.submit(longRunning(), ports::Priority::Batch);
    while (a.status() != JobStatus::Running || b.status() != JobStatus::Running) {
        std::this_thread::sleep_for(1ms);
    }
    a.cancel();
    const auto r = a.wait();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::Cancelled);
    EXPECT_EQ(a.status(), JobStatus::Cancelled);
    const auto t0 = std::chrono::steady_clock::now();
    scheduler.shutdown(); // bricht b und die wartende Aufgabe ab, ohne Verklemmung
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1500ms);
    EXPECT_EQ(b.status(), JobStatus::Cancelled);
    EXPECT_EQ(queued.status(), JobStatus::Cancelled);
    EXPECT_EQ(scheduler.submit(longRunning(), ports::Priority::Batch).status(), JobStatus::Cancelled);
}

// NF-PERF-11, NF-PERF-07
TEST(MemoryBudgetTest, U_APP_11_BudgetIsNeverExceeded)
{
    MemoryBudget budget(1000);
    std::atomic<bool> exceeded{false};
    std::atomic<std::size_t> peak{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 16; ++t) {
        threads.emplace_back([&, t] {
            std::mt19937 rng(static_cast<unsigned>(1100 + t));
            for (int i = 0; i < 200; ++i) {
                const std::size_t n = 1 + rng() % 400;
                auto lease = budget.acquire(n);
                ASSERT_TRUE(lease.has_value());
                const std::size_t used = budget.used();
                exceeded = exceeded || used > budget.total();
                std::size_t p = peak.load();
                while (used > p && !peak.compare_exchange_weak(p, used)) {
                }
                std::this_thread::yield();
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_FALSE(exceeded);
    EXPECT_LE(peak.load(), 1000U);
    EXPECT_EQ(budget.used(), 0U);
    EXPECT_FALSE(budget.acquire(1001).has_value());
    auto all = budget.tryAcquire(1000);
    ASSERT_TRUE(all.has_value());
    EXPECT_FALSE(budget.tryAcquire(1).has_value());
}

// NF-PERF-11: jede Zahl genau einmal, höchstens lanes gleichzeitig
TEST(JobSchedulerTest, M10_ParallelForCoversAllIndicesWithinLanes)
{
    mocks::FakeTaskExecutor executor(8);
    JobScheduler scheduler(executor);
    std::vector<std::atomic<int>> seen(40);
    std::atomic<int> running{0};
    std::atomic<int> peak{0};
    const auto h = scheduler.submit(job([&](JobContext& c) -> base::Expected<void> {
                                        scheduler.parallelFor(c.priority, seen.size(), 3, [&](std::size_t i) {
                                            const int now = ++running;
                                            int p = peak.load();
                                            while (now > p && !peak.compare_exchange_weak(p, now)) {
                                            }
                                            std::this_thread::sleep_for(2ms);
                                            ++seen[i];
                                            --running;
                                        });
                                        return {};
                                    }),
                                    ports::Priority::Batch);
    ASSERT_TRUE(h.wait().has_value());
    for (const auto& n : seen) {
        EXPECT_EQ(n.load(), 1);
    }
    EXPECT_LE(peak.load(), 3);
    EXPECT_GE(peak.load(), 2); // tatsächlich parallel
}

// NF-PERF-11: mit einem einzigen Worker keine Verklemmung (der Job arbeitet selbst mit)
TEST(JobSchedulerTest, M10_ParallelForWithOneWorkerFinishes)
{
    mocks::FakeTaskExecutor executor(1);
    JobScheduler scheduler(executor);
    ThreadConfig one;
    one.workers = 1;
    scheduler.configure(one);
    std::atomic<int> sum{0};
    const auto h = scheduler.submit(
        job([&](JobContext& c) -> base::Expected<void> {
            scheduler.parallelFor(c.priority, 10, 4, [&](std::size_t i) { sum += static_cast<int>(i); });
            return {};
        }),
        ports::Priority::Batch);
    ASSERT_TRUE(h.wait().has_value());
    EXPECT_EQ(sum.load(), 45);
}

// NF-PERF-12: Budget zur Laufzeit ändern; acquireUpTo begrenzt auf das ganze Budget
TEST(MemoryBudgetTest, M10_ResizeAndAcquireUpTo)
{
    MemoryBudget budget(100);
    budget.setTotal(300);
    EXPECT_EQ(budget.total(), 300U);
    auto a = budget.acquireUpTo(1000); // größer als das Budget: bekommt alles
    EXPECT_EQ(a.size(), 300U);
    EXPECT_FALSE(budget.tryAcquire(1).has_value());
    a = MemoryBudget::Lease{};
    EXPECT_EQ(budget.used(), 0U);
}

/// Renderer-Fake: rechnet ein Bild je Anfrage, zählt Aufrufe, braucht etwas Zeit und bricht
/// über das CancelToken ab.
class SlowRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        ++calls;
        for (int i = 0; i < 10; ++i) {
            if (r.cancel.cancelled()) {
                return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
            }
            std::this_thread::sleep_for(1ms);
        }
        return image::ImageBuffer::fromFloats(
            {1, 1, 1, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>{static_cast<float>(r.frame.bytes()[15])});
    }
    std::atomic<int> calls{0};
};

// NF-PERF-01, NF-PERF-04
TEST(RenderServiceTest, U_APP_12_NewestRequestWins)
{
    SlowRenderer renderer;
    std::mutex mutex;
    std::vector<std::uint64_t> delivered;
    RenderService service(renderer,
                          [&](std::uint64_t seq, const pipeline::RenderRequest&, image::ImageBuffer) {
                              const std::scoped_lock lock(mutex);
                              delivered.push_back(seq);
                          });
    std::uint64_t last = 0;
    for (int i = 1; i <= 100; ++i) {
        pipeline::RenderRequest r;
        std::array<std::uint8_t, 16> id{};
        id[15] = static_cast<std::uint8_t>(i);
        r.frame = model::FrameId(id);
        last = service.request(r);
    }
    service.idle();
    const std::scoped_lock lock(mutex);
    ASSERT_FALSE(delivered.empty());
    EXPECT_EQ(delivered.back(), last);              // letzte Anfrage geliefert
    EXPECT_LT(renderer.calls.load(), 100);          // veraltete verworfen
    EXPECT_TRUE(std::ranges::is_sorted(delivered)); // nie ein älteres Bild nach einem neueren
}

// NF-PERF-12
TEST(JobSchedulerTest, U_APP_13_ThreadCountAtRuntime)
{
    mocks::FakeTaskExecutor executor(8);
    JobScheduler scheduler(executor);
    const auto measure = [&] {
        std::atomic<int> running{0};
        std::atomic<int> maxRunning{0};
        std::vector<JobHandle> hs;
        for (int i = 0; i < 12; ++i) {
            hs.push_back(scheduler.submit(job([&](JobContext&) -> base::Expected<void> {
                                              const int now = ++running;
                                              int m = maxRunning.load();
                                              while (now > m && !maxRunning.compare_exchange_weak(m, now)) {
                                              }
                                              std::this_thread::sleep_for(5ms);
                                              --running;
                                              return {};
                                          }),
                                          ports::Priority::Background));
        }
        for (const auto& h : hs) {
            EXPECT_TRUE(h.wait().has_value());
        }
        return maxRunning.load();
    };
    EXPECT_GT(measure(), 2);
    ThreadConfig config;
    config.workers = 2;
    scheduler.configure(config); // ohne Neustart
    EXPECT_EQ(executor.concurrency(), 2);
    EXPECT_LE(measure(), 2);
}

} // namespace
} // namespace autoneg::application
