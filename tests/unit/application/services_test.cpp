// Unit-Tests der Dienste (Architektur 9.13): Export-Dateinamen und -Job, Automatiken über den
// CommandBus, Schrittkatalog, Platzhalter für Version 1.1. Ports durch Fakes ersetzt.

#include "application/presets.hpp"
#include "application/services.hpp"
#include "mocks/fake_task_executor.hpp"
#include "mocks/fakes.hpp"
#include "steps/registry.hpp"
#include "support/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace autoneg::application {
namespace {

namespace fs = std::filesystem;
using model::EditList;
using model::FrameId;
using model::Origin;
using model::ParamSet;
using model::StepId;

model::Frame frame(int n, const fs::path& dir = "rolle")
{
    model::Frame f;
    std::array<std::uint8_t, 16> b{};
    b[15] = static_cast<std::uint8_t>(n);
    f.id = FrameId(b);
    f.frameNo = n;
    f.path = dir / ("DSC0" + std::to_string(7000 + n) + ".ARW");
    return f;
}

class NullRepository final : public ports::IEditRepository
{
public:
    base::Expected<std::optional<ports::SidecarData>> load(const model::Frame&) override
    {
        return std::nullopt;
    }
    base::Expected<void> save(const model::Frame&, const ports::SidecarData&) override { return {}; }
    std::vector<ports::SidecarRef> orphans(const fs::path&) override { return {}; }
    base::Expected<void> relink(const ports::SidecarRef&, const model::Frame&) override { return {}; }
};

/// Gleichmäßiges Bild als Render-Ergebnis.
class FlatRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        ++calls;
        EXPECT_EQ(r.profile.mode, pipeline::RenderMode::Export);
        return image::ImageBuffer::fromFloats(
            {8, 6, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(8 * 6 * 3, 0.25F));
    }
    std::atomic<int> calls{0};
};

// LF-EXP-07
TEST(ExportNaming, U_APP_14_PatternAndNoSilentOverwrite)
{
    model::Roll roll;
    roll.name = "2026-09 Toskana: Rolle 3";
    const model::Frame f = frame(12);
    EXPECT_EQ(exportName("{roll}_{frame}_{name}", roll, f), "2026-09 Toskana_ Rolle 3_012_DSC07012");
    autoneg::testing::TempDir dir;
    ExportSettings s;
    s.folder = dir.path();
    s.write.format = ports::FileFormat::Tiff;
    const auto p = ExportService::target(s, roll, f);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->filename().string(), "2026-09 Toskana_ Rolle 3_012_DSC07012.tif");
    std::ofstream(*p) << "vorhanden";
    const auto again = ExportService::target(s, roll, f);
    ASSERT_FALSE(again.has_value()); // nicht ohne Bestätigung überschreiben
    EXPECT_EQ(again.error().code, base::ErrorCode::Conflict);
    s.overwrite = OverwritePolicy::Overwrite; // nach Bestätigung
    EXPECT_TRUE(ExportService::target(s, roll, f).has_value());
}

// LF-EXP-07: leeres Muster und unter Windows ungültige Namen ergeben trotzdem eine gültige Datei
TEST(ExportNaming, M10_EmptyPatternAndReservedNames)
{
    model::Roll roll;
    const model::Frame f = frame(12);
    EXPECT_EQ(exportName("", roll, f), "DSC07012");
    EXPECT_EQ(exportName("   ", roll, f), "DSC07012");
    EXPECT_EQ(exportName("..", roll, f), "DSC07012");
    EXPECT_EQ(exportName("CON", roll, f), "CON_");
    EXPECT_EQ(exportName("nul", roll, f), "nul_");
    EXPECT_EQ(exportName("COM1", roll, f), "COM1_");
    EXPECT_EQ(exportName("LPT9", roll, f), "LPT9_");
    EXPECT_EQ(exportName("CONSOLE", roll, f), "CONSOLE");
    EXPECT_EQ(exportName("Bild. ", roll, f), "Bild");
}

// LF-DAT-04, NF-REL-02: gespeicherte Vorlagen sind von Hand änderbar; Werte außerhalb der
// Grenzen der Eingabemasken werden beim Laden begrenzt, unbekannte Aufzählungswerte fallen auf
// den Standard zurück (beide Seiten jeder Grenze)
TEST(PresetLimits, M10_ExportValuesAreClampedOnBothSides)
{
    const auto load = [](int quality, int longEdge, int bitDepth, int format) {
        model::JsonValue j;
        j.set("quality", quality);
        j.set("longEdge", longEdge);
        j.set("bitDepth", bitDepth);
        j.set("format", format);
        return exportFromJson(j);
    };
    const ExportSettings standard;
    EXPECT_EQ(load(0, -1, 7, -1).write.quality, 1);
    EXPECT_EQ(load(101, 0, 8, 0).write.quality, 100);
    EXPECT_EQ(load(1, 0, 8, 0).write.quality, 1);
    EXPECT_EQ(load(100, 0, 8, 0).write.quality, 100);
    EXPECT_EQ(load(90, -1, 8, 0).longEdge, 0);
    EXPECT_EQ(load(90, 20001, 8, 0).longEdge, 20000);
    EXPECT_EQ(load(90, 20000, 8, 0).longEdge, 20000);
    EXPECT_EQ(load(90, 0, 7, 0).write.bitDepth, standard.write.bitDepth);
    EXPECT_EQ(load(90, 0, 16, 0).write.bitDepth, 16);
    EXPECT_EQ(load(90, 0, 8, -1).write.format, standard.write.format);
    EXPECT_EQ(load(90, 0, 8, 5).write.format, standard.write.format);
    EXPECT_EQ(load(90, 0, 8, 4).write.format, ports::FileFormat::LinearDng);
}

TEST(PresetLimits, M10_SheetValuesAreClampedOnBothSides)
{
    const auto load = [](double binding, int perStrip, int paper) {
        model::JsonValue j;
        j.set("bindingMm", binding);
        j.set("framesPerStrip", perStrip);
        j.set("paper", paper);
        return sheetFromJson(j).spec;
    };
    EXPECT_EQ(load(-0.1, 6, 0).bindingMm, 0.0);
    EXPECT_EQ(load(50.1, 6, 0).bindingMm, 50.0);
    EXPECT_EQ(load(50.0, 6, 0).bindingMm, 50.0);
    EXPECT_EQ(load(25, -1, 0).framesPerStrip, 0); // 0 = nach Format
    EXPECT_EQ(load(25, 21, 0).framesPerStrip, 20);
    EXPECT_EQ(load(25, 1, 0).framesPerStrip, 1);
    EXPECT_EQ(load(25, 6, 3).paper, layout::ContactSheetSpec{}.paper);
    EXPECT_EQ(load(25, 6, 2).paper, layout::PaperSize::Letter);
}

// LF-GEO-01, LF-BAS-01
TEST(AnalysisServiceTest, U_APP_16_AutomaticsWriteEntries)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    std::mutex mutex;
    std::deque<std::function<void()>> uiQueue; // UI-Thread: wird im Test abgearbeitet
    const UiDispatcher ui = [&](std::function<void()> f) {
        const std::scoped_lock lock(mutex);
        uiQueue.push_back(std::move(f));
    };
    AnalysisFunctions fns;
    fns.image = [](const model::Frame&) {
        return image::ImageBuffer::fromFloats(
            {4, 4, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(48, 0.5F));
    };
    fns.detect = [](const image::ImageBuffer&, model::FilmType,
                    const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        analysis::FrameDetection d;
        d.frame = {0.1, 0.12, 0.8, 0.76};
        d.angleDeg = -0.4;
        d.confidence = 0.9;
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        analysis::BaseMeasurement m;
        m.transmission = {0.7, 0.42, 0.27};
        return m;
    };
    AnalysisService service(jobs, bus, ui, fns);
    auto roll = std::make_shared<model::Roll>();
    roll->filmType = model::FilmType::ColorNegative;
    const model::Frame f1 = frame(1);
    const model::Frame f2 = frame(2);
    bus.open(f1, {});
    ports::SidecarData manual; // Bild 2 hat einen eigenen Zuschnitt
    model::EditEntry ownCrop;
    ownCrop.step = StepId("autoneg.crop");
    ownCrop.params = ParamSet{}.with("rect", model::NormRect{0.2, 0.2, 0.5, 0.5});
    manual.edits = EditList{}.with(ownCrop);
    bus.open(f2, manual);
    ASSERT_TRUE(service.autoConvert(f1, roll, roll->filmType).wait().has_value());
    ASSERT_TRUE(service.autoConvert(f2, roll, roll->filmType).wait().has_value());
    // K-M10-08: ohne festgelegten Filmtyp keine Erkennung, verständliche Meldung
    const auto none = service.autoConvert(f1, roll, model::FilmType::Unconverted).wait();
    ASSERT_FALSE(none.has_value());
    EXPECT_NE(none.error().message.find("Filmtyp nicht festgelegt"), std::string::npos);
    {
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task(); // im UI-Thread
        }
    }
    const EditList& l1 = bus.history(f1.id).current();
    ASSERT_NE(l1.get(StepId("autoneg.crop")), nullptr);
    EXPECT_EQ(l1.get(StepId("autoneg.crop"))->origin, Origin::Automatic);
    // Zuschnitt 1 % innerhalb des erkannten Bildfelds (y 0,12, Höhe 0,76)
    EXPECT_DOUBLE_EQ(l1.get(StepId("autoneg.crop"))->params.get<model::NormRect>("rect").value().y,
                     0.12 + 0.01 * 0.76);
    ASSERT_NE(l1.get(StepId("autoneg.filmbase")), nullptr);
    EXPECT_EQ(l1.get(StepId("autoneg.filmbase"))->origin, Origin::Automatic);
    EXPECT_EQ(l1.get(StepId("autoneg.rotation"))->params.get<double>("angle").value(), -0.4);
    // Eintrag des Anwenders hat Vorrang
    EXPECT_EQ(bus.history(f2.id).current().get(StepId("autoneg.crop"))->origin, Origin::Manual);
    EXPECT_EQ(bus.history(f2.id).current().get(StepId("autoneg.filmbase"))->origin, Origin::Automatic);
}

// K-M10-08 (Absturz beim Schließen während der Erkennung): ein Ergebnis, das erst nach dem
// Abbau des Dienstes im UI-Thread ankommt, wird verworfen und schreibt nichts mehr
TEST(AnalysisServiceTest, M10_ResultAfterShutdownIsDropped)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    std::deque<std::function<void()>> uiQueue; // UI-Thread: erst nach dem Abbau abgearbeitet
    std::mutex mutex;
    const UiDispatcher ui = [&](std::function<void()> f) {
        const std::scoped_lock lock(mutex);
        uiQueue.push_back(std::move(f));
    };
    AnalysisFunctions fns;
    fns.image = [](const model::Frame&) {
        return image::ImageBuffer::fromFloats(
            {4, 4, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(48, 0.5F));
    };
    fns.detect = [](const image::ImageBuffer&, model::FilmType,
                    const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        analysis::FrameDetection d;
        d.frame = {0.1, 0.1, 0.8, 0.8};
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        analysis::BaseMeasurement m;
        m.transmission = {0.7, 0.42, 0.27};
        return m;
    };
    const model::Frame f1 = frame(1);
    bus.open(f1, {});
    auto roll = std::make_shared<model::Roll>();
    roll->filmType = model::FilmType::ColorNegative;
    {
        AnalysisService service(jobs, bus, ui, fns);
        ASSERT_TRUE(service.autoConvert(f1, roll, roll->filmType).wait().has_value());
    }
    const std::scoped_lock lock(mutex);
    ASSERT_FALSE(uiQueue.empty());
    for (auto& task : uiQueue) {
        task();
    }
    EXPECT_TRUE(bus.history(f1.id).current().empty());
}

// K-M10-13 (Fehler des Nutzers: Auto-Bildfeld konvertierte das ganze Bild): jede Automatik
// trägt nur ihren Teil ein
TEST(AnalysisServiceTest, M10_EachAutomaticWritesOnlyItsPart)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    std::deque<std::function<void()>> uiQueue;
    std::mutex mutex;
    const UiDispatcher ui = [&](std::function<void()> f) {
        const std::scoped_lock lock(mutex);
        uiQueue.push_back(std::move(f));
    };
    AnalysisFunctions fns;
    fns.image = [](const model::Frame&) {
        return image::ImageBuffer::fromFloats(
            {32, 32, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(32 * 32 * 3, 0.2F));
    };
    fns.detect = [](const image::ImageBuffer&, model::FilmType,
                    const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        analysis::FrameDetection d;
        d.frame = {0.1, 0.1, 0.8, 0.8};
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        analysis::BaseMeasurement m;
        m.transmission = {0.7, 0.42, 0.27};
        return m;
    };
    AnalysisService service(jobs, bus, ui, fns);
    auto roll = std::make_shared<model::Roll>();
    roll->filmType = model::FilmType::ColorNegative;
    const model::Frame f1 = frame(1);
    bus.open(f1, {});
    using Automatic = JobHandle (AnalysisService::*)(const model::Frame&, std::shared_ptr<const model::Roll>,
                                                     model::FilmType, model::EditList);
    const auto run = [&](Automatic a) {
        ASSERT_TRUE((service.*a)(f1, roll, roll->filmType, bus.history(f1.id).current()).wait());
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task();
        }
        uiQueue.clear();
    };
    const auto has = [&](const char* step) {
        return bus.history(f1.id).current().get(StepId(step)) != nullptr;
    };
    run(&AnalysisService::autoFrame);
    EXPECT_TRUE(has("autoneg.crop"));
    EXPECT_FALSE(has("autoneg.filmbase"));
    EXPECT_FALSE(has("autoneg.densityrange"));
    run(&AnalysisService::autoBase);
    EXPECT_TRUE(has("autoneg.filmbase"));
    EXPECT_FALSE(has("autoneg.densityrange"));
    run(&AnalysisService::autoLevels);
    EXPECT_TRUE(has("autoneg.densityrange"));
    EXPECT_FALSE(has("autoneg.convert"));
    run(&AnalysisService::convert);
    ASSERT_TRUE(has("autoneg.convert"));
    // Schalter Konvertieren: nicht als Autokonvertierung markiert
    EXPECT_FALSE(
        bus.history(f1.id).current().get(StepId("autoneg.convert"))->params.get<bool>("auto").value());
    // Neuberechnung nach dem Einfügen: nur die genannten Schritte
    bus.execute(RemoveEntryCommand(StepId("autoneg.crop")), std::vector{f1.id}, *roll);
    bus.execute(RemoveEntryCommand(StepId("autoneg.filmbase")), std::vector{f1.id}, *roll);
    ASSERT_TRUE(
        service.redetect(f1, roll, roll->filmType, bus.history(f1.id).current(), {StepId("autoneg.crop")})
            .wait());
    {
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task();
        }
        uiQueue.clear();
    }
    EXPECT_TRUE(has("autoneg.crop"));
    EXPECT_FALSE(has("autoneg.filmbase"));
}

// Reflexion: jede Teil-Automatik dekodierte das RAW und erkannte das Bildfeld neu (Auto-Basis
// über 10 s). Analysebild und Erkennung werden je Bild einmal berechnet; ein anderer Filmtyp
// oder Hinweis erkennt neu, das Bild bleibt.
TEST(AnalysisServiceTest, M10_AutomaticsShareImageAndDetection)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    const UiDispatcher ui = [](const std::function<void()>&) {};
    std::atomic<int> decoded{0};
    std::atomic<int> detected{0};
    AnalysisFunctions fns;
    fns.image = [&decoded](const model::Frame&) {
        ++decoded;
        return image::ImageBuffer::fromFloats(
            {32, 32, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(32 * 32 * 3, 0.2F));
    };
    fns.detect = [&detected](const image::ImageBuffer&, model::FilmType,
                             const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        ++detected;
        analysis::FrameDetection d;
        d.frame = {0.1, 0.1, 0.8, 0.8};
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        analysis::BaseMeasurement m;
        m.transmission = {0.7, 0.42, 0.27};
        return m;
    };
    fns.measureRect = [](const image::ImageBuffer&,
                         const model::NormRect&) -> base::Expected<analysis::BaseMeasurement> {
        return analysis::BaseMeasurement{};
    };
    AnalysisService service(jobs, bus, ui, fns);
    auto roll = std::make_shared<model::Roll>();
    const model::Frame f1 = frame(1);
    bus.open(f1, {});
    const auto film = model::FilmType::ColorNegative;
    ASSERT_TRUE(service.autoFrame(f1, roll, film).wait());
    ASSERT_TRUE(service.autoBase(f1, roll, film).wait());
    ASSERT_TRUE(service.convert(f1, roll, film).wait());
    ASSERT_TRUE(service.measureBase(f1, roll, {0.0, 0.0, 0.1, 0.1}).wait());
    EXPECT_EQ(decoded, 1);
    EXPECT_EQ(detected, 1);
    ASSERT_TRUE(service.autoFrame(f1, roll, model::FilmType::BwNegative).wait());
    EXPECT_EQ(detected, 2);
    analysis::FrameHint portrait;
    portrait.orientation = analysis::FrameHint::Orientation::Portrait;
    service.setFrameHint(portrait);
    ASSERT_TRUE(service.autoFrame(f1, roll, model::FilmType::BwNegative).wait());
    EXPECT_EQ(detected, 3);
    EXPECT_EQ(decoded, 1);
    // mehr Bilder als Plätze: das älteste fällt heraus
    for (int i = 2; i <= 8; ++i) {
        const model::Frame f = frame(i);
        bus.open(f, {});
        ASSERT_TRUE(service.autoFrame(f, roll, film).wait());
    }
    ASSERT_TRUE(service.autoFrame(f1, roll, film).wait());
    EXPECT_EQ(decoded, 9);
}

// LF-BAS-05 c: ohne Filmrand schätzt Auto-Basis die Basis aus dem Bild und sagt das dazu
TEST(AnalysisServiceTest, M10_BaseWithoutRebateIsEstimated)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    std::deque<std::function<void()>> uiQueue;
    std::mutex mutex;
    const UiDispatcher ui = [&](std::function<void()> f) {
        const std::scoped_lock lock(mutex);
        uiQueue.push_back(std::move(f));
    };
    AnalysisFunctions fns;
    fns.image = [](const model::Frame&) {
        std::vector<float> px(32 * 32 * 3, 0.2F);
        for (std::size_t i = 0; i < 32 * 3; i += 3) { // hellste Stellen: nahe der Basis
            px[i] = 0.8F;
            px[i + 1] = 0.5F;
            px[i + 2] = 0.3F;
        }
        return image::ImageBuffer::fromFloats(
            {32, 32, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, std::move(px));
    };
    fns.detect = [](const image::ImageBuffer&, model::FilmType,
                    const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        analysis::FrameDetection d;
        d.frame = {0.0, 0.0, 1.0, 1.0};
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        return base::makeError(base::ErrorCode::NotFound, "kein Filmrand");
    };
    AnalysisService service(jobs, bus, ui, fns);
    std::vector<std::string> notes;
    service.setReport(
        [&](const model::FrameId&, AnalysisService::FrameReport r) { notes = std::move(r.hints); });
    auto roll = std::make_shared<model::Roll>();
    roll->filmType = model::FilmType::ColorNegative;
    const model::Frame f1 = frame(1);
    bus.open(f1, {});
    ASSERT_TRUE(service.autoBase(f1, roll, roll->filmType).wait());
    {
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task();
        }
    }
    const model::EditEntry* base = bus.history(f1.id).current().get(StepId("autoneg.filmbase"));
    ASSERT_NE(base, nullptr);
    const base::Vec3 b = base->params.get<base::Vec3>("base").value();
    EXPECT_GT(b.x, b.y); // Orange-Maske: rot > grün > blau
    EXPECT_GT(b.y, b.z);
    EXPECT_FALSE(base->params.contains("area")); // kein Messbereich
    ASSERT_EQ(notes.size(), 1U);
    EXPECT_NE(notes[0].find("geschätzt"), std::string::npos);
}

// K-M6-01
TEST(StepCatalogTest, M6_CatalogFromRegistry)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    const ports::StepCatalog c = stepCatalog(registry);
    EXPECT_EQ(c.size(), registry.ordered().size());
    const auto crop = std::ranges::find(c, std::string("autoneg.crop"), &ports::StepSchema::id);
    ASSERT_NE(crop, c.end());
    EXPECT_EQ(crop->rank, registry.find(StepId("autoneg.crop"))->descriptor().rank);
    EXPECT_NE(std::ranges::find(crop->params,
                                std::pair<std::string, ports::ParamKind>{"rect", ports::ParamKind::Rect}),
              crop->params.end());
}

// LF-EXP-01, LF-EXP-05
TEST(ExportServiceTest, M6_ExportJobWritesAllFrames)
{
    autoneg::testing::TempDir dir;
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    FlatRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::FakeImageWriter writer;
    ExportService service(jobs, renderer, color, writer);
    auto roll = std::make_shared<model::Roll>();
    roll->name = "Rolle";
    ExportSettings s;
    s.folder = dir.path();
    s.pattern = "{roll}_{frame}";
    s.write.format = ports::FileFormat::Tiff;
    s.write.bitDepth = 16;
    std::vector<ExportService::Item> items;
    for (int i = 1; i <= 3; ++i) {
        items.push_back({frame(i), EditList{}});
    }
    const JobHandle h = service.exportFrames(items, roll, s);
    ASSERT_TRUE(h.wait().has_value());
    EXPECT_EQ(renderer.calls.load(), 3);
    EXPECT_TRUE(fs::exists(dir.path() / "Rolle_001.tif"));
    EXPECT_TRUE(fs::exists(dir.path() / "Rolle_003.tif"));
    EXPECT_DOUBLE_EQ(h.progress(), 1.0);
    // zweiter Lauf ohne Bestätigung: Konflikt, nichts überschrieben
    const JobHandle again = service.exportFrames(items, roll, s);
    const auto r = again.wait();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::Conflict);
}

/// Renderer mit Quellengröße, Rechenzeit und Zählung gleichzeitiger Aufrufe; schlägt für
/// gewählte Bildnummern fehl.
class ConcurrentRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        const int now = ++running;
        int p = peak.load();
        while (now > p && !peak.compare_exchange_weak(p, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        --running;
        ++calls;
        if (std::ranges::find(failing, static_cast<int>(r.frame.bytes()[15])) != failing.end()) {
            return base::makeError(base::ErrorCode::Corrupt,
                                   "Bild " + std::to_string(r.frame.bytes()[15]) + " defekt");
        }
        return image::ImageBuffer::fromFloats(
            {8, 6, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(8 * 6 * 3, 0.25F));
    }
    base::Expected<model::SourceInfo> info(const FrameId&, const fs::path&) override
    {
        model::SourceInfo i;
        i.width = 1000;
        i.height = 1000; // 1 MP: 40 MB je Bild im Budget
        return i;
    }
    std::atomic<int> running{0};
    std::atomic<int> peak{0};
    std::atomic<int> calls{0};
    std::vector<int> failing;
};

struct ExportFixture
{
    explicit ExportFixture(int threads, std::size_t budget)
        : executor(8)
        , jobs(executor)
    {
        ThreadConfig c;
        c.workers = threads;
        c.memoryBudget = budget;
        jobs.configure(c);
        roll->name = "Rolle";
        s.folder = dir.path();
        s.pattern = "{roll}_{frame}";
        s.write.format = ports::FileFormat::Tiff;
        s.write.bitDepth = 16;
        for (int i = 1; i <= 12; ++i) {
            items.push_back({frame(i), EditList{}});
        }
    }
    autoneg::testing::TempDir dir;
    mocks::FakeTaskExecutor executor;
    JobScheduler jobs;
    ConcurrentRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::FakeImageWriter writer;
    std::shared_ptr<model::Roll> roll = std::make_shared<model::Roll>();
    ExportSettings s;
    std::vector<ExportService::Item> items;
};

constexpr std::size_t mb = std::size_t{1} << 20U;

// NF-PERF-11: mehrere Bilder gleichzeitig, alle geschrieben
TEST(ExportServiceTest, M10_BatchExportRunsFramesConcurrently)
{
    ExportFixture f(4, 1024 * mb);
    ExportService service(f.jobs, f.renderer, f.color, f.writer);
    const JobHandle h = service.exportFrames(f.items, f.roll, f.s);
    ASSERT_TRUE(h.wait().has_value());
    EXPECT_EQ(f.renderer.calls.load(), 12);
    EXPECT_GE(f.renderer.peak.load(), 2);
    EXPECT_LE(f.renderer.peak.load(), 4); // nie mehr als Threads
    for (int i = 1; i <= 12; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "Rolle_%03d.tif", i);
        EXPECT_TRUE(fs::exists(f.dir.path() / name)) << name;
    }
    EXPECT_DOUBLE_EQ(h.progress(), 1.0);
}

// NF-PERF-11: das Speicherbudget begrenzt die Zahl gleichzeitiger Bilder
TEST(ExportServiceTest, M10_BatchExportRespectsMemoryBudget)
{
    ExportFixture f(8, 2 * ExportService::exportBytesPerPixel * mb); // Platz für zwei Bilder zu 1 MP
    ExportService service(f.jobs, f.renderer, f.color, f.writer);
    ASSERT_TRUE(service.exportFrames(f.items, f.roll, f.s).wait().has_value());
    EXPECT_EQ(f.renderer.calls.load(), 12);
    EXPECT_LE(f.renderer.peak.load(), 2);
    EXPECT_EQ(f.jobs.budget().used(), 0U); // alle Anteile zurückgegeben
}

// LF-EXP-01: Fehler wird in Bildreihenfolge gemeldet; danach beginnt kein neues Bild
TEST(ExportServiceTest, M10_BatchExportReportsFirstErrorInFrameOrder)
{
    ExportFixture f(4, 1024 * mb);
    f.renderer.failing = {5, 3};
    ExportService service(f.jobs, f.renderer, f.color, f.writer);
    const auto r = service.exportFrames(f.items, f.roll, f.s).wait();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "Bild 3 defekt");
    EXPECT_LT(f.renderer.calls.load(), 12);
}

// LF-EXP-07: ein Konflikt bricht ab, bevor irgendein Bild geschrieben ist
TEST(ExportServiceTest, M10_ConflictStopsBeforeAnyWrite)
{
    ExportFixture f(4, 1024 * mb);
    std::ofstream(f.dir.path() / "Rolle_009.tif") << "alt";
    ExportService service(f.jobs, f.renderer, f.color, f.writer);
    const auto r = service.exportFrames(f.items, f.roll, f.s).wait();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::Conflict);
    EXPECT_EQ(f.renderer.calls.load(), 0);
    EXPECT_FALSE(fs::exists(f.dir.path() / "Rolle_001.tif"));
}

// NF-PERF-11, NF-PERF-12: ein Thread – nacheinander, ohne Verklemmung
TEST(ExportServiceTest, M10_BatchExportWithOneThread)
{
    ExportFixture f(1, 1024 * mb);
    ExportService service(f.jobs, f.renderer, f.color, f.writer);
    ASSERT_TRUE(service.exportFrames(f.items, f.roll, f.s).wait().has_value());
    EXPECT_EQ(f.renderer.calls.load(), 12);
    EXPECT_EQ(f.renderer.peak.load(), 1);
}

// NF-ARC-07, NF-ARC-15
TEST(PlaceholdersTest, M6_InferenceAndProfilesArePresent)
{
    NoInferenceRuntime runtime;
    EXPECT_FALSE(runtime.available());
    const auto m = runtime.load("modell.onnx");
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code, base::ErrorCode::Unsupported);
    NoFilmProfiles profiles;
    EXPECT_TRUE(profiles.list().empty());
    EXPECT_FALSE(profiles.load("kodak-portra-400").has_value());
}

// Film-Farbprofil (Wunsch des Nutzers, LF-KON-05/-06): Unbearbeitet setzt keine Grenzen, Neutral
// die automatisch gemessenen je Kanal, ein eigenes Profil die Automatik plus seine Abweichungen
TEST(FilmProfileTest, M10_ProfileParams)
{
    using P = AnalysisService::FilmProfile;
    P raw;
    raw.kind = P::Kind::Unprocessed;
    raw.name = "Unbearbeitet";
    const ParamSet r = raw.params({0.3, 0.3, 0.3}, {1.8, 1.8, 1.8});
    EXPECT_FALSE(r.get<base::Vec3>("dMin").has_value());
    EXPECT_FALSE(r.get<base::Vec3>("dMax").has_value());
    EXPECT_EQ(r.get<std::string>("profile").value_or(""), "Unbearbeitet");

    P neutral;
    const ParamSet n = neutral.params({0.2, 0.3, 0.4}, {1.6, 1.8, 2.0});
    EXPECT_EQ(n.get<base::Vec3>("dMin").value(), (base::Vec3{0.2, 0.3, 0.4}));
    EXPECT_EQ(n.get<base::Vec3>("dMax").value(), (base::Vec3{1.6, 1.8, 2.0}));
    EXPECT_EQ(n.get<base::Vec3>("autoMin").value(), (base::Vec3{0.2, 0.3, 0.4})); // gemessene Automatik
    EXPECT_FALSE(n.get<base::Vec3>("gamma").has_value());

    // gespeichert von einem Bild mit dMin (0,2/0,3/0,4), dMax (1,6/1,8/2,0), Gamma Rot 1,2
    model::EditEntry saved;
    saved.step = StepId("autoneg.densityrange");
    saved.params = ParamSet{}
                       .with("dMin", base::Vec3{0.2, 0.3, 0.4})
                       .with("dMax", base::Vec3{1.6, 1.8, 2.0})
                       .with("gamma", base::Vec3{1.2, 1.0, 1.0});
    const P portra = P::fromEntry(saved, "Portra 160");
    EXPECT_EQ(portra.kind, P::Kind::Custom);
    // ältere Vorgabe ohne Automatik: Abweichung vom Mittel; auf ein Bild mit Automatik 0,5 / 2,0
    const ParamSet c = portra.params({0.5, 0.5, 0.5}, {2.0, 2.0, 2.0});
    const auto lo = c.get<base::Vec3>("dMin").value();
    const auto hi = c.get<base::Vec3>("dMax").value();
    EXPECT_NEAR(lo.x, 0.4, 1e-12);
    EXPECT_NEAR(lo.y, 0.5, 1e-12);
    EXPECT_NEAR(lo.z, 0.6, 1e-12);
    EXPECT_NEAR(hi.x, 1.8, 1e-12);
    EXPECT_NEAR(hi.z, 2.2, 1e-12);
    EXPECT_EQ(c.get<base::Vec3>("gamma").value(), (base::Vec3{1.2, 1.0, 1.0}));
    EXPECT_EQ(c.get<std::string>("profile").value_or(""), "Portra 160");
    // gespeichert mit Automatik: nur die Abweichung davon wird übertragen (kein doppelter Ausgleich)
    model::EditEntry withAuto = saved;
    withAuto.params = withAuto.params.with("autoMin", base::Vec3{0.2, 0.25, 0.4})
                          .with("autoMax", base::Vec3{1.6, 1.8, 2.0});
    const ParamSet d = P::fromEntry(withAuto, "Portra 160").params({0.1, 0.2, 0.3}, {1.5, 1.7, 1.9});
    const auto lo2 = d.get<base::Vec3>("dMin").value();
    EXPECT_NEAR(lo2.x, 0.1, 1e-12);
    EXPECT_NEAR(lo2.y, 0.25, 1e-12); // +0,05 Grün aus dem Profil
    EXPECT_NEAR(lo2.z, 0.3, 1e-12);
}

// Entscheidung des Nutzers: das Standardprofil „Neutral“ gleicht Farbstiche automatisch je Kanal
// aus (Kanäle verschieden dicht, verschiedene Grenzen); „Unbearbeitet“ setzt nichts
TEST(AnalysisServiceTest, M10_NeutralLevelsBalanceChannels)
{
    steps::FlatFieldCache flat;
    pipeline::StepRegistry registry;
    ASSERT_TRUE(steps::registerStandardSteps(registry, flat).has_value());
    NullRepository repo;
    PersistenceQueue queue(repo);
    CommandBus bus(registry, queue);
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    std::deque<std::function<void()>> uiQueue;
    std::mutex mutex;
    const UiDispatcher ui = [&](std::function<void()> f) {
        const std::scoped_lock lock(mutex);
        uiQueue.push_back(std::move(f));
    };
    AnalysisFunctions fns;
    fns.image = [](const model::Frame&) {
        // Kanäle verschieden: Rot hell, Blau dunkel, mit Verlauf
        std::vector<float> v(64 * 64 * 3);
        for (std::size_t i = 0; i < 64 * 64; ++i) {
            const float t = 0.2F + 0.6F * static_cast<float>(i % 64) / 63.0F;
            v[i * 3] = 0.7F * t;
            v[i * 3 + 1] = 0.4F * t;
            v[i * 3 + 2] = 0.2F * t;
        }
        return image::ImageBuffer::fromFloats({64, 64, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
                                              v);
    };
    fns.detect = [](const image::ImageBuffer&, model::FilmType,
                    const analysis::FrameHint&) -> base::Expected<analysis::FrameDetection> {
        analysis::FrameDetection d;
        d.frame = {0.0, 0.0, 1.0, 1.0};
        return d;
    };
    fns.measureBase = [](const image::ImageBuffer&,
                         const analysis::RegionMap&) -> base::Expected<analysis::BaseMeasurement> {
        analysis::BaseMeasurement m;
        m.transmission = {0.9, 0.9, 0.9};
        return m;
    };
    AnalysisService service(jobs, bus, ui, fns);
    auto roll = std::make_shared<model::Roll>();
    roll->filmType = model::FilmType::ColorNegative;
    const model::Frame f1 = frame(1);
    bus.open(f1, {});
    const auto levels = [&] {
        bus.execute(RemoveEntryCommand(StepId("autoneg.densityrange")), std::vector{f1.id}, *roll);
        EXPECT_TRUE(service.autoConvert(f1, roll, roll->filmType, bus.history(f1.id).current()).wait());
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task();
        }
        uiQueue.clear();
        return *bus.history(f1.id).current().get(StepId("autoneg.densityrange"));
    };
    const model::EditEntry n = levels();
    const auto lo = n.params.get<base::Vec3>("dMin").value();
    const auto hi = n.params.get<base::Vec3>("dMax").value();
    EXPECT_NE(lo.x, lo.z); // Rot hell, Blau dunkel: eigene Grenzen je Kanal
    EXPECT_LT(lo.x, hi.x);
    EXPECT_LT(lo.z, hi.z);
    EXPECT_EQ(n.params.get<base::Vec3>("autoMin").value(), lo);

    AnalysisService::FilmProfile raw;
    raw.kind = AnalysisService::FilmProfile::Kind::Unprocessed;
    raw.name = "Unbearbeitet";
    service.setFilmProfile(raw);
    const model::EditEntry r = levels();
    EXPECT_FALSE(r.params.get<base::Vec3>("dMin").has_value()); // nichts gemessen
    EXPECT_EQ(r.params.get<std::string>("profile").value_or(""), "Unbearbeitet");
}

} // namespace
} // namespace autoneg::application
