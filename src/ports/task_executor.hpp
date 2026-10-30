#pragma once

#include <functional>

namespace autoneg::ports {

/// Priorität einer Aufgabe; zugleich die Arena im Worker-Pool (Kapitel 6.2).
enum class Priority
{
    Interactive, ///< Vorschau, Detailansicht
    Background,  ///< Analyse, Miniaturen
    Batch        ///< Export, Kontaktabzug
};

/// Ausführung von Aufgaben in drei Arenen eines gemeinsamen Worker-Pools (A-02).
/// Adapter: autoneg_adapter_tbb. Der JobScheduler (application) baut darauf auf.
///
/// Vertrag:
///  - threadsicher;
///  - freie Threads übernehmen Arbeit in der Reihenfolge Interactive vor Background vor Batch;
///  - Datenparallelität innerhalb einer Aufgabe (z. B. im CpuBackend) läuft in derselben Arena;
///  - eine geänderte Threadzahl wirkt auf neue Aufgaben, ohne Neustart (NF-PERF-12).
class ITaskExecutor
{
public:
    virtual ~ITaskExecutor() = default;

    /// Reiht die Aufgabe in die Arena ein und kehrt sofort zurück.
    virtual void enqueue(Priority p, std::function<void()> task) = 0;
    /// Führt die Aufgabe in der Arena aus und wartet auf ihr Ende.
    virtual void run(Priority p, const std::function<void()>& task) = 0;

    [[nodiscard]] virtual int concurrency() const = 0;
    /// workers = 0: alle logischen Kerne.
    virtual void setConcurrency(int workers) = 0;
    /// Wartet auf alle eingereihten Aufgaben; danach werden keine neuen angenommen.
    virtual void shutdown() = 0;
};

} // namespace autoneg::ports
