#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/cancel_token.hpp"
#include "autoneg/ports/task_executor.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace autoneg::application {

/// Laufzeiteinstellungen der Nebenläufigkeit (Architektur 9.13, NF-PERF-11, -12).
struct ThreadConfig
{
    int workers = 0;                                 ///< 0 = alle logischen Kerne
    std::size_t memoryBudget = std::size_t{4} << 30; ///< Bytes für Zwischenbilder (Export)
    int tileSize = 1024;
    double previewMaxMp = 8.0;
};

/// Speicherbudget für große Zwischenbilder (NF-PERF-07, -11): die Summe der vergebenen
/// Anteile überschreitet nie das Budget. acquire wartet, bis genug frei ist. Threadsicher.
class MemoryBudget
{
public:
    /// Anteil am Budget; gibt ihn im Destruktor zurück. Nur verschiebbar.
    class Lease
    {
    public:
        Lease() = default;
        Lease(Lease&& o) noexcept;
        Lease& operator=(Lease&& o) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease();
        [[nodiscard]] std::size_t size() const noexcept { return bytes_; }

    private:
        friend class MemoryBudget;
        Lease(MemoryBudget* b, std::size_t bytes)
            : budget_(b)
            , bytes_(bytes)
        {}
        MemoryBudget* budget_ = nullptr;
        std::size_t bytes_ = 0;
    };

    explicit MemoryBudget(std::size_t bytes)
        : total_(bytes)
    {}

    /// Wartet, bis bytes frei sind. Fehler InvalidArgument, wenn bytes größer als das Budget ist.
    [[nodiscard]] base::Expected<Lease> acquire(std::size_t bytes);
    [[nodiscard]] std::optional<Lease> tryAcquire(std::size_t bytes);
    [[nodiscard]] std::size_t total() const noexcept { return total_; }
    [[nodiscard]] std::size_t used() const;

private:
    void release(std::size_t bytes);

    std::size_t total_;
    std::size_t used_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

enum class JobStatus
{
    Queued,
    Running,
    Done,
    Failed,
    Cancelled
};

/// Umgebung eines laufenden Jobs (Architektur 9.13).
struct JobContext
{
    image::CancelToken cancel;
    ports::Priority priority = ports::Priority::Background;
    std::function<void(int done, int total)> progress;
};

/// Eine Aufgabe für den JobScheduler (Export, Analyse, Miniaturen …).
class Job
{
public:
    virtual ~Job() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual base::Expected<void> run(JobContext& c) = 0;
};

namespace detail {
struct JobState;
}

/// Griff auf einen eingereihten Job: Fortschritt, Abbruch, Warten (threadsicher).
class JobHandle
{
public:
    JobHandle() = default;
    explicit JobHandle(std::shared_ptr<detail::JobState> s)
        : state_(std::move(s))
    {}
    [[nodiscard]] double progress() const;
    [[nodiscard]] JobStatus status() const;
    void cancel() const;
    /// Wartet auf das Ende; Fehler Cancelled bei Abbruch, sonst das Ergebnis des Jobs.
    [[nodiscard]] base::Expected<void> wait() const;
    [[nodiscard]] bool done() const;

private:
    std::shared_ptr<detail::JobState> state_;
};

/// Verteilt Jobs nach Priorität auf den gemeinsamen Worker-Pool (Architektur 6, 9.13).
/// Die Arenen und die Threadzahl stellt ports::ITaskExecutor (A-02); hier liegen Status,
/// Fortschritt, Abbruch und das geordnete Herunterfahren. Threadsicher.
class JobScheduler
{
public:
    explicit JobScheduler(ports::ITaskExecutor& executor)
        : executor_(executor)
    {}
    ~JobScheduler();
    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;
    JobScheduler(JobScheduler&&) = delete;
    JobScheduler& operator=(JobScheduler&&) = delete;

    /// Reiht den Job ein; nach shutdown() wird er sofort als abgebrochen gemeldet.
    JobHandle submit(std::unique_ptr<Job> job, ports::Priority p);
    /// Threadzahl zur Laufzeit (NF-PERF-12); wirkt auf neue Aufgaben.
    void configure(const ThreadConfig& c);
    /// Bricht alle offenen Jobs ab und wartet, bis keiner mehr läuft (ohne Verklemmung).
    void shutdown();

private:
    ports::ITaskExecutor& executor_;
    std::mutex mutex_;
    std::vector<std::weak_ptr<detail::JobState>> jobs_;
    bool closed_ = false;
};

} // namespace autoneg::application
