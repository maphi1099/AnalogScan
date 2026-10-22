#pragma once

#include "autoneg/model/roll.hpp"
#include "autoneg/ports/repositories.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace autoneg::application {

/// Sofortspeichern im eigenen Thread (Architektur 8.5, LF-DAT-03).
///
/// Hält je Bild nur den neuesten Stand; schreibt sofort, wenn für dieses Bild kein Schreib-
/// vorgang läuft, sonst unmittelbar danach. Zwischenstände schneller Änderungsfolgen werden
/// übersprungen. Ein Intervall gibt es nicht. flush() kehrt erst zurück, wenn alles
/// geschrieben ist. Der Destruktor schreibt noch Ausstehendes und beendet den Thread.
/// enqueue/flush sind threadsicher.
class PersistenceQueue
{
public:
    using ErrorHandler = std::function<void(const model::Frame&, const base::Error&)>;

    explicit PersistenceQueue(ports::IEditRepository& repo, ErrorHandler onError = {});
    ~PersistenceQueue();
    PersistenceQueue(const PersistenceQueue&) = delete;
    PersistenceQueue& operator=(const PersistenceQueue&) = delete;
    PersistenceQueue(PersistenceQueue&&) = delete;
    PersistenceQueue& operator=(PersistenceQueue&&) = delete;

    void enqueue(model::Frame f, ports::SidecarData d);
    void flush();

private:
    void loop(const std::stop_token& stop);

    struct Pending
    {
        model::Frame frame;
        ports::SidecarData data;
    };

    ports::IEditRepository& repo_;
    ErrorHandler onError_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::map<std::string, Pending> latest_; ///< Schlüssel: Pfad des Scans
    int writing_ = 0;
    std::jthread thread_; ///< zuletzt: startet nach allen anderen Mitgliedern
};

} // namespace autoneg::application
