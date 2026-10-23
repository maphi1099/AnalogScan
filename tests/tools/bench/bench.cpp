// Leistungsmessung K-18 / E-49 (NF-PERF-01 … -11): misst die echte Komposition (alle Adapter)
// auf diesem Rechner und schreibt einen Bericht. Die Zielwerte gelten für den Referenzrechner
// (8 Kerne, 32 GB, NVMe); die Auswertung vermerkt die Abweichung des Messrechners.
//
//   autoneg_bench <scan-ordner> <arbeitsordner> [--frames 36] [--big]
//
// <scan-ordner>: echte Scans; sie werden nach <arbeitsordner>/rolle kopiert (Original bleibt
// unberührt). --big erzeugt zusätzlich ein künstliches 100-MP-Negativ (TIFF 16 bit).
// Nur im Release-Bau aussagekräftig.

#include "autoneg/app/composition.hpp"
#include "autoneg/steps/step_ids.hpp"
#include "autoneg/testing/synthetic.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // plattformabhängig: Spitzenspeicher (PROGRESS.md)

// nach windows.h (eigene Gruppe, damit clang-format nicht umsortiert)
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace autoneg;
using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

double peakMemoryGb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS c{};
    GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c);
    return static_cast<double>(c.PeakWorkingSetSize) / (1024.0 * 1024.0 * 1024.0);
#else
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return static_cast<double>(u.ru_maxrss) / (1024.0 * 1024.0); // KiB
#endif
}

/// Aktuell belegter Arbeitsspeicher (Working Set bzw. Resident Set) in GB.
double currentMemoryGb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS c{};
    GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c);
    return static_cast<double>(c.WorkingSetSize) / (1024.0 * 1024.0 * 1024.0);
#else
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    statm >> pages >> resident;
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGE_SIZE)) /
           (1024.0 * 1024.0 * 1024.0);
#endif
}

/// Höchster Wert von currentMemoryGb während eines Abschnitts (Abtastung alle 20 ms).
class MemorySampler
{
public:
    MemorySampler()
        : start_(currentMemoryGb())
        , thread_([this](const std::stop_token& stop) {
            while (!stop.stop_requested()) {
                const double now = currentMemoryGb();
                double m = max_.load();
                while (now > m && !max_.compare_exchange_weak(m, now)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        })
    {}
    /// Höchstwert über der Grundlast beim Start.
    [[nodiscard]] double aboveStart()
    {
        thread_.request_stop();
        thread_.join();
        return max_.load() - start_;
    }

private:
    double start_;
    std::atomic<double> max_{0.0};
    std::jthread thread_;
};

class Queue
{
public:
    void post(std::function<void()> f)
    {
        const std::scoped_lock lock(mtx_);
        tasks_.push_back(std::move(f));
    }
    void drain()
    {
        std::deque<std::function<void()>> now;
        {
            const std::scoped_lock lock(mtx_);
            now.swap(tasks_);
        }
        for (auto& f : now) {
            f();
        }
    }

private:
    std::mutex mtx_;
    std::deque<std::function<void()>> tasks_;
};

struct Report
{
    std::ofstream out;
    void line(const std::string& what, double value, const std::string& unit, const std::string& target)
    {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%-44s %10.3f %-4s  Ziel %s", what.c_str(), value, unit.c_str(),
                      target.c_str());
        std::cout << buf << "\n";
        out << buf << "\n";
        out.flush();
    }
    void text(const std::string& t)
    {
        std::cout << t << "\n";
        out << t << "\n";
    }
};

bool wait(Queue& q, const application::JobHandle& h)
{
    while (!h.done()) {
        q.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    q.drain();
    return h.wait().has_value();
}

model::EditEntry exposure(double ev)
{
    model::EditEntry e;
    e.step = model::StepId(std::string(steps::id::exposure));
    e.params = model::ParamSet{}.with("ev", ev);
    return e;
}

/// Zeit bis zur Vorschau (RenderService, Monitorgröße 2560 × 1440) nach Regleränderungen.
std::vector<double> previewLatencies(app::Composition& c, const model::Frame& f, int n)
{
    std::vector<double> out;
    model::EditList base = c.bus().history(f.id).current();
    for (int i = 0; i < n; ++i) {
        pipeline::RenderRequest r;
        r.frame = f.id;
        r.path = f.path;
        r.list = std::make_shared<const model::EditList>(base.with(exposure(0.01 * i)));
        r.profile.mode = pipeline::RenderMode::Preview;
        r.profile.target = {2560, 1440};
        r.profile.useCache = true;
        const auto t0 = Clock::now();
        c.render().request(std::move(r));
        c.render().idle(); // bis die Vorschau fertig ist
        out.push_back(seconds(t0, Clock::now()) * 1000.0);
    }
    return out;
}

double percentile(std::vector<double> v, double p)
{
    std::ranges::sort(v);
    const auto k = static_cast<std::size_t>(
        std::clamp(p * static_cast<double>(v.size() - 1), 0.0, static_cast<double>(v.size() - 1)));
    return v[k];
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Aufruf: autoneg_bench <scan-ordner> <arbeitsordner> [--frames N] [--big]\n";
        return 2;
    }
    const fs::path source = argv[1];
    const fs::path work = argv[2];
    int frames = 36;
    bool big = false;
    bool quick = false; // nur bis zur Skalierung
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--big") {
            big = true;
        } else if (a == "--quick") {
            quick = true;
        }
    }
    fs::create_directories(work);
    const fs::path roll = work / "rolle";
    fs::remove_all(roll);
    fs::create_directories(roll);
    Report report;
    report.out.open(work / "leistung.txt");
    report.text("AutoNeg Leistungsmessung (K-18, E-49); Hardware-Threads " +
                std::to_string(std::thread::hardware_concurrency()));

    // Scans kopieren (Testdaten nur lesen)
    std::vector<fs::path> scans;
    for (const auto& e : fs::directory_iterator(source)) {
        if (e.is_regular_file() && application::RollService::isScanFile(e.path())) {
            scans.push_back(e.path());
        }
    }
    std::ranges::sort(scans);
    scans.resize(std::min<std::size_t>(scans.size(), static_cast<std::size_t>(frames)));
    for (const fs::path& p : scans) {
        fs::copy_file(p, roll / p.filename(), fs::copy_options::overwrite_existing);
    }
    report.text("Rolle: " + std::to_string(scans.size()) + " Scans aus " + source.string());

    Queue q;
    application::NoDocumentRenderer noDocument;
    app::Composition c(
        {work / "config", work / "cache"}, {}, [&q](std::function<void()> f) { q.post(std::move(f)); },
        noDocument);
    fs::remove_all(work / "cache" / "thumbnails");

    // Öffnen der Rolle (Prüfsummen, Begleitdateien)
    auto t0 = Clock::now();
    auto opened = c.rolls().open(roll);
    if (!opened) {
        std::cerr << opened.error().message << "\n";
        return 1;
    }
    report.line("Rolle öffnen (" + std::to_string(opened->frames.size()) + " Scans, Prüfsummen)",
                seconds(t0, Clock::now()), "s", "–");
    auto shared = std::make_shared<const model::Roll>(opened->roll);
    for (const application::OpenedFrame& f : opened->frames) {
        c.bus().open(f.frame, f.data);
    }
    const model::Frame first = opened->frames.front().frame;
    const auto info = c.sourceInfo(first);
    const double mp = info ? info->width * static_cast<double>(info->height) / 1e6 : 0.0;
    report.text("Erstes Bild: " + first.path.filename().string() + ", " + std::to_string(mp) + " MP");

    // Öffnen eines Bildes bis zur ersten Vorschau (NF-PERF-02)
    t0 = Clock::now();
    {
        pipeline::RenderRequest r;
        r.frame = first.id;
        r.path = first.path;
        r.list = std::make_shared<const model::EditList>(c.bus().history(first.id).current());
        r.profile.target = {2560, 1440};
        r.profile.useCache = true;
        c.render().request(std::move(r));
        c.render().idle();
    }
    report.line("Bild öffnen bis zur Vorschau (" + std::to_string(static_cast<int>(mp)) + " MP)",
                seconds(t0, Clock::now()), "s", "<= 3 s bei 100 MP");

    // Vorschau nach Regleränderung (NF-PERF-01, E-49)
    const auto lat = previewLatencies(c, first, 60);
    report.line("Vorschau nach Regler, Median", percentile(lat, 0.5), "ms", "–");
    report.line("Vorschau nach Regler, 95. Perzentil", percentile(lat, 0.95), "ms", "<= 100 ms");

    // Teilschritte eines Exports (Ursachenanalyse)
    {
        c.sources().release(first.id);
        auto t = Clock::now();
        auto full = c.sources().source(first.id, first.path, 0);
        report.line("  Teil: RAW dekodieren volle Auflösung", seconds(t, Clock::now()), "s", "–");
        pipeline::RenderRequest r;
        r.frame = first.id;
        r.path = first.path;
        r.list = std::make_shared<const model::EditList>(c.bus().history(first.id).current());
        r.film = shared->filmType;
        r.profile.mode = pipeline::RenderMode::Export;
        t = Clock::now();
        auto img = c.renderer().render(r);
        report.line("  Teil: Pipeline (Export, Kacheln)", seconds(t, Clock::now()), "s", "–");
        auto working = c.color().profile(ports::ProfileId::WorkingRec2020Linear);
        auto srgb = c.color().profile(ports::ProfileId::Srgb);
        auto xf = c.color().transform(*working, *srgb, ports::Intent::RelativeColorimetric);
        t = Clock::now();
        auto out = (*xf)->apply(*img);
        report.line("  Teil: Farbumrechnung (LCMS)", seconds(t, Clock::now()), "s", "–");
        ports::WriteOptions o;
        o.format = ports::FileFormat::Tiff;
        o.bitDepth = 16;
        o.icc = *srgb;
        fs::create_directories(work / "teile");
        t = Clock::now();
        (void)c.writer().write(work / "teile" / "x.tif", *out, o);
        report.line("  Teil: TIFF 16 bit schreiben", seconds(t, Clock::now()), "s", "–");
        (void)full;
    }

    // Export volle Auflösung (NF-PERF-03) und Skalierung (NF-PERF-05); das Speicherbudget
    // (Standard 40 % des Arbeitsspeichers) bleibt dabei erhalten
    const std::size_t budget = c.jobs().budget().total();
    report.line("Speicherbudget (NF-PERF-12)", static_cast<double>(budget) / (1U << 30U), "GB", "40 % RAM");
    const auto setThreads = [&](int threads) {
        application::ThreadConfig tc;
        tc.workers = threads;
        tc.memoryBudget = budget;
        c.jobs().configure(tc);
    };
    const auto exportOne = [&](int threads, const fs::path& dir) {
        application::ThreadConfig tc;
        tc.workers = threads;
        tc.memoryBudget = budget;
        c.jobs().configure(tc);
        fs::create_directories(dir);
        application::ExportSettings s;
        s.folder = dir;
        s.overwrite = application::OverwritePolicy::Overwrite;
        s.write.format = ports::FileFormat::Tiff;
        s.write.bitDepth = 16;
        const auto t = Clock::now();
        const bool ok =
            wait(q, c.exports().exportFrames({{first, c.bus().history(first.id).current()}}, shared, s));
        return ok ? seconds(t, Clock::now()) : -1.0;
    };
    // Erster Export ohne dekodierte Quelle (wie nach dem Öffnen einer Rolle)
    c.sources().release(first.id);
    const double tCold = exportOne(0, work / "export_kalt");
    report.line("Export 1 Bild inkl. RAW-Dekodierung, alle Kerne", tCold, "s", "<= 10 s bei 100 MP");
    // Danach mit dekodierter Quelle (aktives Bild): Pipeline, Farbumrechnung, Schreiben
    c.setCurrent(first.id);
    (void)c.sources().source(first.id, first.path, 0);
    const double tAll = exportOne(0, work / "export_alle");
    report.line("Export 1 Bild, Quelle dekodiert, alle Kerne", tAll, "s", "–");
    const double t1 = exportOne(1, work / "export_1");
    report.line("Export 1 Bild, 1 Thread", t1, "s", "–");
    const int eight = std::min(8, static_cast<int>(std::thread::hardware_concurrency()));
    const double t8 = exportOne(eight, work / "export_8");
    report.line("Export 1 Bild, " + std::to_string(eight) + " Threads", t8, "s", "–");
    report.line("Skalierung 1 -> " + std::to_string(eight) + " Threads", t1 / t8, "x", ">= 5 (8 Kerne)");
    setThreads(0);
    report.line("Spitzenspeicher bis hier (Einzelbild)", peakMemoryGb(), "GB", "<= 6 GB");
    if (quick) {
        report.line("Spitzenspeicher des Prozesses", peakMemoryGb(), "GB", "<= 6 GB");
        return 0;
    }

    // Miniaturen der Rolle (NF-PERF-06)
    t0 = Clock::now();
    std::atomic<int> done{0};
    for (const application::OpenedFrame& f : opened->frames) {
        c.thumbnails().request(f.frame, c.bus().history(f.frame.id).current(), shared->filmType, 256,
                               [&done](const application::ThumbnailService::Result&) { ++done; });
    }
    while (done < static_cast<int>(opened->frames.size())) {
        q.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    report.line("Miniaturen " + std::to_string(opened->frames.size()) + " Scans", seconds(t0, Clock::now()),
                "s", "<= 15 s für 36");

    // Stapelexport (NF-PERF-04/-10: im Hintergrund, hier nur die Dauer)
    {
        std::vector<application::ExportService::Item> items;
        for (const application::OpenedFrame& f : opened->frames) {
            items.push_back({f.frame, c.bus().history(f.frame.id).current()});
        }
        application::ExportSettings s;
        s.folder = work / "stapel";
        s.overwrite = application::OverwritePolicy::Overwrite;
        s.write.format = ports::FileFormat::Jpeg;
        s.write.bitDepth = 8;
        fs::create_directories(s.folder);
        MemorySampler sampler;
        t0 = Clock::now();
        wait(q, c.exports().exportFrames(std::move(items), shared, s));
        const double above = sampler.aboveStart();
        report.line("Stapelexport " + std::to_string(opened->frames.size()) + " Bilder (JPEG)",
                    seconds(t0, Clock::now()), "s", "–");
        report.line("Stapelexport: Speicher über der Grundlast", above, "GB", "<= Budget");
    }

    // Kontaktabzug: Rendern der 36 Felder (ohne PDF-Ausgabe, NF-PERF-09)
    {
        std::vector<application::ContactSheetService::Item> items;
        for (const application::OpenedFrame& f : opened->frames) {
            items.push_back(
                {{f.frame.id, f.frame.frameNo, 1.5}, f.frame, c.bus().history(f.frame.id).current()});
        }
        t0 = Clock::now();
        const auto pages =
            c.contactSheet().render({}, items, *layout::FilmFormat::find("Kleinbild 24×36"), *shared, {});
        report.line("Kontaktabzug rendern (" + std::to_string(items.size()) + " Felder, 300 dpi)",
                    pages ? seconds(t0, Clock::now()) : -1.0, "s", "<= 30 s");
    }

    // Künstliches 100-MP-Negativ (Öffnen, Vorschau, Export)
    if (big) {
        testing::NegativeSpec spec;
        spec.width = 11656;
        spec.height = 8742;
        spec.pattern = testing::Pattern::Gradient;
        spec.dust = 20;
        const fs::path bigDir = work / "gross";
        fs::remove_all(bigDir);
        fs::create_directories(bigDir);
        {
            ports::WriteOptions o;
            o.format = ports::FileFormat::Tiff;
            o.bitDepth = 16;
            if (auto ok = c.writer().write(bigDir / "N100MP.tif", testing::makeNegative(spec).scan, o); !ok) {
                std::cerr << ok.error().message << "\n";
            }
        }
        auto broll = c.rolls().open(bigDir);
        if (broll && !broll->frames.empty()) {
            const model::Frame bf = broll->frames.front().frame;
            c.bus().open(bf, broll->frames.front().data);
            c.setCurrent(bf.id);
            t0 = Clock::now();
            {
                pipeline::RenderRequest r;
                r.frame = bf.id;
                r.path = bf.path;
                r.list = std::make_shared<const model::EditList>(c.bus().history(bf.id).current());
                r.profile.target = {2560, 1440};
                r.profile.useCache = true;
                c.render().request(std::move(r));
                c.render().idle();
            }
            report.line("100 MP (TIFF): öffnen bis Vorschau", seconds(t0, Clock::now()), "s", "<= 3 s");
            const auto blat = previewLatencies(c, bf, 40);
            report.line("100 MP: Vorschau nach Regler, 95. Perzentil", percentile(blat, 0.95), "ms",
                        "<= 100 ms");
            application::ExportSettings s;
            s.folder = bigDir / "export";
            s.overwrite = application::OverwritePolicy::Overwrite;
            s.write.format = ports::FileFormat::Tiff;
            s.write.bitDepth = 16;
            fs::create_directories(s.folder);
            t0 = Clock::now();
            const bool ok =
                wait(q, c.exports().exportFrames({{bf, c.bus().history(bf.id).current()}},
                                                 std::make_shared<const model::Roll>(broll->roll), s));
            report.line("100 MP: Export volle Auflösung (TIFF 16 bit)", ok ? seconds(t0, Clock::now()) : -1.0,
                        "s", "<= 10 s");
        }
    }
    report.line("Spitzenspeicher des Prozesses", peakMemoryGb(), "GB", "<= 6 GB");
    return 0;
}
