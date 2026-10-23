// Unit-Tests RollService und ThumbnailService (Architektur 9.13; LF-IMP-07, LF-DAT-02,
// LF-DAT-06, NF-PERF-06). Ports durch Fakes ersetzt; Scans sind kleine Dateien im
// temporären Ordner (nur Name, Prüfsumme und Zeitstempel zählen).

#include "autoneg/application/roll_service.hpp"
#include "autoneg/application/thumbnail_service.hpp"
#include "autoneg/mocks/fake_repositories.hpp"
#include "autoneg/mocks/fake_task_executor.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <future>
#include <sstream>

namespace autoneg::application {
namespace {

namespace fs = std::filesystem;

/// Prüfsumme über den Dateiinhalt (FNV-1a), damit gleiche Inhalte gleiche Kennungen haben.
class ContentHasher final : public ports::IContentHasher
{
public:
    base::Expected<model::FrameId> hashFile(const fs::path& p) override
    {
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            return base::makeError(base::ErrorCode::NotFound, "fehlt");
        }
        std::ostringstream s;
        s << in.rdbuf();
        std::uint64_t h = 1469598103934665603ULL;
        for (const char c : s.str()) {
            h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
        }
        std::array<std::uint8_t, 16> b{};
        for (std::size_t i = 0; i < 8; ++i) {
            b[i] = static_cast<std::uint8_t>(h >> (8 * i));
        }
        return model::FrameId(b);
    }
};

void writeScan(const fs::path& p)
{
    std::ofstream(p, std::ios::binary) << "Inhalt " << p.filename().string();
}

model::EditEntry exposure(double ev)
{
    model::EditEntry e;
    e.step = model::StepId("autoneg.exposure");
    e.params = model::ParamSet{}.with("ev", ev);
    return e;
}

class RollServiceTest : public ::testing::Test
{
protected:
    RollServiceTest()
    {
        for (const char* n : {"DSC00003.ARW", "DSC00001.ARW", "scan.TIF", "DSC00002.jpg", "notiz.txt",
                              "DSC00001.ARW.autoneg.json"}) {
            writeScan(dir.path() / n);
        }
    }
    autoneg::testing::TempDir dir;
    mocks::FakeTaskExecutor executor{2};
    JobScheduler jobs{executor};
    mocks::FakeRollRepository rolls;
    mocks::FakeEditRepository edits;
    ContentHasher hasher;
    RollService service{jobs, rolls, edits, hasher};
};

// LF-IMP-07
TEST_F(RollServiceTest, M8_OpenListsSupportedScansInNameOrder)
{
    const auto r = service.open(dir.path());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    std::vector<std::string> names;
    for (const OpenedFrame& f : r->frames) {
        names.push_back(f.frame.path.filename().string());
        EXPECT_FALSE(f.problem.has_value());
        EXPECT_EQ(f.frame.id, *hasher.hashFile(f.frame.path));
        EXPECT_EQ(f.data.source.hash, f.frame.id);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"DSC00001.ARW", "DSC00002.jpg", "DSC00003.ARW", "scan.TIF"}));
    EXPECT_EQ(r->frames[2].frame.frameNo, 3);
    ASSERT_EQ(r->roll.frames.size(), 4U);
    EXPECT_EQ(r->roll.frames[0], r->frames[0].frame.id);
    EXPECT_EQ(r->roll.name, dir.path().filename().string());
    EXPECT_FALSE(r->readOnly);
}

// LF-DAT-02: gespeicherter Stand und Rollendaten kommen beim Öffnen zurück
TEST_F(RollServiceTest, M8_OpenRestoresSidecarsAndRollData)
{
    model::Roll stored;
    stored.folder = dir.path();
    stored.name = "Toskana";
    stored.filmType = model::FilmType::BwNegative;
    stored.defaults = model::EditList{}.with(exposure(0.2));
    ASSERT_TRUE(rolls.save(stored).has_value());
    model::Frame f;
    f.path = dir.path() / "DSC00002.jpg";
    ports::SidecarData d;
    d.frameNo = 17;
    d.rating = model::Rating::Reject;
    d.edits = model::EditList{}.with(exposure(1.0));
    ASSERT_TRUE(edits.save(f, d).has_value());

    const auto r = service.open(dir.path());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->roll.name, "Toskana");
    EXPECT_EQ(r->roll.filmType, model::FilmType::BwNegative);
    EXPECT_EQ(r->roll.defaults, stored.defaults);
    const OpenedFrame& two = r->frames[1];
    EXPECT_TRUE(two.stored);
    EXPECT_EQ(two.data.edits, d.edits);
    // ohne Begleitdatei: Rollen-Standard mit Herkunft RollDefault (LF-BAT-01)
    const OpenedFrame& one = r->frames[0];
    EXPECT_FALSE(one.stored);
    ASSERT_EQ(one.data.edits.size(), 1U);
    EXPECT_EQ(one.data.edits.entries()[0].origin, model::Origin::RollDefault);
    EXPECT_EQ(two.frame.frameNo, 17);
    EXPECT_EQ(two.frame.rating, model::Rating::Reject);
}

// Architektur 8.8: Rolle einer anderen laufenden Instanz nur lesend
TEST_F(RollServiceTest, M8_LockedElsewhereOpensReadOnly)
{
    rolls.lockedElsewhere(dir.path());
    const auto r = service.open(dir.path());
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->readOnly);
}

// LF-DAT-06: umbenannter Scan bekommt seine Begleitdatei über die Prüfsumme zurück
TEST_F(RollServiceTest, M8_RenamedScanIsRelinkedByChecksum)
{
    const fs::path before = dir.path() / "DSC00003.ARW";
    model::Frame f;
    f.path = before;
    f.id = *hasher.hashFile(before);
    ports::SidecarData d;
    d.source.hash = f.id;
    d.edits = model::EditList{}.with(exposure(-0.5));
    ASSERT_TRUE(edits.save(f, d).has_value());
    fs::rename(before, dir.path() / "Toskana_03.ARW");

    const auto r = service.open(dir.path());
    ASSERT_TRUE(r.has_value());
    const auto it = std::ranges::find_if(
        r->frames, [](const OpenedFrame& o) { return o.frame.path.filename() == "Toskana_03.ARW"; });
    ASSERT_NE(it, r->frames.end());
    EXPECT_EQ(it->data.edits, d.edits);
    EXPECT_TRUE(r->orphans.empty());
}

TEST_F(RollServiceTest, M8_OpenFolderJobDeliversResult)
{
    std::optional<base::Expected<OpenedRoll>> got;
    const JobHandle h =
        service.openFolder(dir.path(), [&](base::Expected<OpenedRoll> r) { got = std::move(r); });
    ASSERT_TRUE(h.wait().has_value());
    ASSERT_TRUE(got.has_value() && got->has_value());
    EXPECT_EQ((*got)->frames.size(), 4U);
    const auto missing = service.openFolder(dir.path() / "fehlt", {}).wait();
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, base::ErrorCode::NotFound);
}

/// Zählt Render-Aufrufe; liefert ein kleines Bild.
class CountingRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        ++calls;
        if (gate.valid()) {
            gate.wait(); // hält das Rendern an, bis alle Anfragen gestellt sind
        }
        EXPECT_EQ(r.profile.mode, pipeline::RenderMode::Thumbnail);
        EXPECT_EQ(r.profile.target.width, 160);
        return image::ImageBuffer::fromFloats(
            {16, 12, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(16 * 12 * 3, 0.3F));
    }
    std::atomic<int> calls{0};
    std::shared_future<void> gate;
};

class ThumbnailServiceTest : public ::testing::Test
{
protected:
    ThumbnailServiceTest()
    {
        frame.path = "rolle/DSC00001.ARW";
        frame.id = model::FrameId(std::array<std::uint8_t, 16>{1});
    }
    mocks::FakeTaskExecutor executor{4};
    JobScheduler jobs{executor};
    CountingRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::FakeImageWriter writer;
    mocks::FakeCacheStore cache;
    std::atomic<int> released{0};
    ThumbnailService service{jobs,   renderer, color,
                             writer, cache,    [this](const model::FrameId&) { ++released; }};
    model::Frame frame;
};

// NF-PERF-06: einmal rechnen, danach aus dem Cache
TEST_F(ThumbnailServiceTest, M8_RenderOnceThenFromCache)
{
    const model::EditList l = model::EditList{}.with(exposure(0.5));
    const auto first = service.request(frame, l, model::FilmType::ColorNegative, 160).get();
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_FALSE(first->empty());
    EXPECT_EQ(renderer.calls.load(), 1);
    EXPECT_EQ(released.load(), 1);
    const auto again = service.request(frame, l, model::FilmType::ColorNegative, 160).get();
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *first);
    EXPECT_EQ(renderer.calls.load(), 1);
    // geänderte Bearbeitung oder anderer Filmtyp: neue Miniatur
    ASSERT_TRUE(
        service.request(frame, model::EditList{}.with(exposure(0.6)), model::FilmType::ColorNegative, 160)
            .get()
            .has_value());
    ASSERT_TRUE(service.request(frame, l, model::FilmType::Slide, 160).get().has_value());
    EXPECT_EQ(renderer.calls.load(), 3);
}

TEST_F(ThumbnailServiceTest, M8_ConcurrentRequestsShareOneRender)
{
    std::promise<void> open;
    renderer.gate = open.get_future().share();
    const model::EditList l;
    std::vector<std::shared_future<ThumbnailService::Result>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(service.request(frame, l, model::FilmType::ColorNegative, 160));
    }
    open.set_value();
    for (auto& f : futures) {
        ASSERT_TRUE(f.get().has_value());
    }
    EXPECT_EQ(renderer.calls.load(), 1);
}

} // namespace
} // namespace autoneg::application
