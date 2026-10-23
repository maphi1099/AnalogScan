// Komponententest Nebenläufigkeit unter Last (K-16, NF-ARC-16, NF-PERF-10, NF-PERF-11): Kern und
// Anwendung mit echten Adaptern, ein Stress-Treiber statt der Oberfläche. Gleichzeitig laufen
// Regleränderungen (CommandBus, sofortiges Speichern), Vorschauanfragen (neueste gewinnt),
// Miniaturen, Erkennung, Stapelexport und zufällige Abbrüche. Geprüft: keine Verklemmung
// (Watchdog 60 s), Begleitdateien entsprechen am Ende dem Stand im Speicher; Datenrennen findet
// der Lauf unter ThreadSanitizer (linux-clang-tsan).
// Dauer: AUTONEG_STRESS_SECONDS (Standard 10 s; Szenario: 600 s unter TSan).

#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/app/composition.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

namespace autoneg {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

int seconds()
{
#if defined(_WIN32)
    char* v = nullptr;
    std::size_t n = 0;
    int s = 10;
    if (_dupenv_s(&v, &n, "AUTONEG_STRESS_SECONDS") == 0 && v != nullptr) {
        s = std::atoi(v);
    }
    std::free(v);
    return s > 0 ? s : 10;
#else
    const char* v = std::getenv("AUTONEG_STRESS_SECONDS");
    const int s = v != nullptr ? std::atoi(v) : 10;
    return s > 0 ? s : 10;
#endif
}

/// Aufgaben aus Jobs im Hauptthread (wie die Oberfläche).
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

// K-16: NF-ARC-16, NF-PERF-10, NF-PERF-11
TEST(StressComponent, K_16_ConcurrencyUnderLoad)
{
    autoneg::testing::TempDir tmp;
    const fs::path roll = tmp.path() / "rolle";
    fs::create_directories(roll);
    {
        adapter::oiio::OiioImageWriter writer;
        for (int i = 1; i <= 8; ++i) {
            testing::NegativeSpec spec;
            spec.width = 320;
            spec.height = 220;
            spec.seed = static_cast<std::uint64_t>(i);
            spec.dust = i;
            spec.frame.x = 0.1 + 0.002 * i;
            ports::WriteOptions o;
            o.format = ports::FileFormat::Tiff;
            o.bitDepth = 16;
            ASSERT_TRUE(
                writer.write(roll / ("S" + std::to_string(i) + ".tif"), testing::makeNegative(spec).scan, o)
                    .has_value());
        }
    }
    Queue queue;
    std::atomic<std::uint64_t> previews{0};
    std::atomic<std::uint64_t> thumbs{0};
    application::NoDocumentRenderer document;
    app::Composition c(
        {tmp.path() / "config", tmp.path() / "cache"},
        [&previews](std::uint64_t, const pipeline::RenderRequest&, image::ImageBuffer) { ++previews; },
        [&queue](std::function<void()> f) { queue.post(std::move(f)); }, document);
    auto opened = c.rolls().open(roll);
    ASSERT_TRUE(opened.has_value());
    const auto shared = std::make_shared<const model::Roll>(opened->roll);
    std::vector<model::Frame> frames;
    for (const application::OpenedFrame& f : opened->frames) {
        c.bus().open(f.frame, f.data);
        frames.push_back(f.frame);
    }

    std::mt19937 rng(1234); // fester Seed
    std::atomic<Clock::rep> heartbeat{Clock::now().time_since_epoch().count()};
    std::atomic<bool> stop{false};
    std::atomic<bool> stuck{false};
    std::jthread watchdog([&] {
        while (!stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const auto last = Clock::time_point(Clock::duration(heartbeat.load()));
            if (Clock::now() - last > std::chrono::seconds(60)) {
                stuck = true;
                std::cerr << "Watchdog: seit 60 s kein Fortschritt\n";
                std::abort(); // Verklemmung: Prozess beenden, statt ewig zu hängen
            }
        }
    });

    const auto end = Clock::now() + std::chrono::seconds(seconds());
    std::vector<application::JobHandle> jobs;
    std::uint64_t commands = 0;
    const fs::path exportDir = tmp.path() / "export";
    fs::create_directories(exportDir);
    while (Clock::now() < end) {
        heartbeat = Clock::now().time_since_epoch().count();
        queue.drain();
        const model::Frame& f = frames[rng() % frames.size()];
        switch (rng() % 10) {
        case 0:
        case 1:
        case 2: { // Regler (UI-Thread): Befehl und sofortiges Speichern
            model::EditEntry e;
            e.step = model::StepId("autoneg.exposure");
            e.params = model::ParamSet{}.with("ev", static_cast<double>(rng() % 200) / 100.0 - 1.0);
            const std::vector<model::FrameId> t{f.id};
            c.bus().execute(application::SetEntryCommand(e), t, *shared);
            ++commands;
            break;
        }
        case 3:
        case 4: { // Vorschau: neueste Anfrage gewinnt
            pipeline::RenderRequest r;
            r.frame = f.id;
            r.path = f.path;
            r.list = std::make_shared<const model::EditList>(c.bus().history(f.id).current());
            r.profile.target = {200, 140};
            r.profile.useCache = true;
            c.render().request(std::move(r));
            break;
        }
        case 5:
            c.thumbnails().request(f, c.bus().history(f.id).current(), model::FilmType::ColorNegative, 96,
                                   [&thumbs](const application::ThumbnailService::Result&) { ++thumbs; });
            break;
        case 6:
            jobs.push_back(c.analysis().detectFrame(f, shared));
            break;
        case 7: { // Stapelexport aller Bilder
            std::vector<application::ExportService::Item> items;
            for (const model::Frame& fr : frames) {
                items.push_back({fr, c.bus().history(fr.id).current()});
            }
            application::ExportSettings s;
            s.folder = exportDir;
            s.overwrite = application::OverwritePolicy::Overwrite;
            s.write.format = ports::FileFormat::Tiff;
            s.write.bitDepth = 8;
            jobs.push_back(c.exports().exportFrames(std::move(items), shared, s));
            break;
        }
        case 8: // zufälliger Abbruch
            if (!jobs.empty()) {
                jobs[rng() % jobs.size()].cancel();
            }
            break;
        default:
            if (rng() % 4 == 0) {
                c.bus().undo();
            }
            break;
        }
        std::erase_if(jobs, [](const application::JobHandle& h) { return h.done(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Rest abarbeiten
    for (const application::JobHandle& h : jobs) {
        while (!h.done()) {
            heartbeat = Clock::now().time_since_epoch().count();
            queue.drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    c.render().idle();
    queue.drain();
    c.queue().flush();
    stop = true;
    watchdog.join();
    EXPECT_FALSE(stuck);
    std::cout << "  " << commands << " Befehle, " << previews << " Vorschauen, " << thumbs << " Miniaturen\n";
    EXPECT_GT(commands, 0U);

    // Begleitdateien = Stand im Speicher (Konsistenz nach Last)
    auto reread = c.rolls().open(roll);
    ASSERT_TRUE(reread.has_value());
    for (const application::OpenedFrame& f : reread->frames) {
        EXPECT_EQ(f.data.edits, c.bus().history(f.frame.id).current()) << f.frame.path;
    }
    // keine halben Exportdateien
    for (const auto& e : fs::directory_iterator(exportDir)) {
        EXPECT_EQ(e.path().string().find(".tmp"), std::string::npos) << e.path();
    }
}

} // namespace
} // namespace autoneg
