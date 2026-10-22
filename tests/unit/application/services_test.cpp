// Unit-Tests der Dienste (Architektur 9.13): Export-Dateinamen und -Job, Automatiken über den
// CommandBus, Schrittkatalog, Platzhalter für Version 1.1. Ports durch Fakes ersetzt.

#include "autoneg/application/services.hpp"
#include "autoneg/mocks/fake_task_executor.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/steps/registry.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <deque>
#include <fstream>
#include <mutex>

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
    fns.detect = [](const image::ImageBuffer&, model::FilmType) -> base::Expected<analysis::FrameDetection> {
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
    ASSERT_TRUE(service.detectFrame(f1, roll).wait().has_value());
    ASSERT_TRUE(service.detectFrame(f2, roll).wait().has_value());
    {
        const std::scoped_lock lock(mutex);
        for (auto& task : uiQueue) {
            task(); // im UI-Thread
        }
    }
    const EditList& l1 = bus.history(f1.id).current();
    ASSERT_NE(l1.get(StepId("autoneg.crop")), nullptr);
    EXPECT_EQ(l1.get(StepId("autoneg.crop"))->origin, Origin::Automatic);
    EXPECT_EQ(l1.get(StepId("autoneg.crop"))->params.get<model::NormRect>("rect").value().y, 0.12);
    ASSERT_NE(l1.get(StepId("autoneg.filmbase")), nullptr);
    EXPECT_EQ(l1.get(StepId("autoneg.filmbase"))->origin, Origin::Automatic);
    EXPECT_EQ(l1.get(StepId("autoneg.rotation"))->params.get<double>("angle").value(), -0.4);
    // Eintrag des Anwenders hat Vorrang
    EXPECT_EQ(bus.history(f2.id).current().get(StepId("autoneg.crop"))->origin, Origin::Manual);
    EXPECT_EQ(bus.history(f2.id).current().get(StepId("autoneg.filmbase"))->origin, Origin::Automatic);
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

} // namespace
} // namespace autoneg::application
