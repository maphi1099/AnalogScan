// Unit-Tests Befehle, Verlauf und Sofortspeichern (Architektur 9.12, 8.5).

#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/commands.hpp"
#include "autoneg/application/persistence_queue.hpp"
#include "autoneg/steps/registry.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace autoneg::application {
namespace {

using model::EditList;
using model::FrameId;
using model::Origin;
using model::ParamSet;
using model::StepId;

FrameId frameId(int n)
{
    std::array<std::uint8_t, 16> b{};
    b[15] = static_cast<std::uint8_t>(n);
    b[14] = static_cast<std::uint8_t>(n >> 8);
    return FrameId(b);
}

model::EditEntry entry(const std::string& id, ParamSet p, Origin o = Origin::Manual)
{
    model::EditEntry e;
    e.step = StepId(id);
    e.params = std::move(p);
    e.origin = o;
    return e;
}

/// Repository-Fake: zählt Schreibvorgänge, speichert den letzten Stand je Bild.
class CountingRepository final : public ports::IEditRepository
{
public:
    base::Expected<std::optional<ports::SidecarData>> load(const model::Frame& f) override
    {
        const std::scoped_lock lock(mutex);
        const auto it = saved.find(f.id);
        return it != saved.end() ? std::optional(it->second) : std::nullopt;
    }
    base::Expected<void> save(const model::Frame& f, const ports::SidecarData& d) override
    {
        std::this_thread::sleep_for(delay);
        const std::scoped_lock lock(mutex);
        saved[f.id] = d;
        ++writes;
        return {};
    }
    std::vector<ports::SidecarRef> orphans(const std::filesystem::path& /*folder*/) override { return {}; }
    base::Expected<void> relink(const ports::SidecarRef& /*o*/, const model::Frame& /*f*/) override
    {
        return {};
    }

    std::mutex mutex;
    std::map<FrameId, ports::SidecarData> saved;
    std::atomic<int> writes{0};
    std::chrono::milliseconds delay{0};
};

class Commands : public ::testing::Test
{
protected:
    Commands()
    {
        EXPECT_TRUE(steps::registerStandardSteps(registry, flatFields).has_value());
        for (int i = 1; i <= 36; ++i) {
            model::Frame f;
            f.id = frameId(i);
            f.path = "rolle/bild" + std::to_string(i) + ".ARW";
            f.frameNo = i;
            frames.push_back(f);
            roll.frames.push_back(f.id);
        }
    }

    void openAll(const EditList& initial = {})
    {
        for (const model::Frame& f : frames) {
            ports::SidecarData d;
            d.edits = initial;
            bus.open(f, d);
        }
    }

    const EditList& current(int n) const { return bus.history(frameId(n)).current(); }

    steps::FlatFieldCache flatFields;
    pipeline::StepRegistry registry;
    model::Roll roll;
    std::vector<model::Frame> frames;
    CountingRepository repo;
    PersistenceQueue queue{repo};
    CommandBus bus{registry, queue};
};

// LF-LST-05
TEST_F(Commands, U_APP_01_SetEntryIsManual)
{
    openAll();
    const std::vector<FrameId> one{frameId(1)};
    bus.execute(SetEntryCommand(entry("autoneg.exposure", ParamSet{}.with("ev", 0.5))), one, roll);
    const model::EditEntry* e = current(1).get(StepId("autoneg.exposure"));
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->origin, Origin::Manual);
    EXPECT_EQ(e->params.get<double>("ev").value(), 0.5);
    EXPECT_EQ(e->rank, registry.find(StepId("autoneg.exposure"))->descriptor().rank);
}

// LF-LST-03
TEST_F(Commands, U_APP_02_RemoveGivesNeutralState)
{
    const EditList without = EditList{}.with(entry("autoneg.exposure", ParamSet{}.with("ev", 0.3)));
    openAll(without);
    const std::vector<FrameId> one{frameId(1)};
    bus.execute(SetEntryCommand(entry("autoneg.contrast", ParamSet{}.with("amount", 0.4))), one, roll);
    // Rang wie im Ausgangsstand normalisieren, damit nur der Inhalt verglichen wird
    EditList reference = without;
    bus.execute(RemoveEntryCommand(StepId("autoneg.contrast")), one, roll);
    EXPECT_EQ(current(1).hash(), reference.hash());
}

// LF-LST-06
TEST_F(Commands, U_APP_03_ToggleKeepsValues)
{
    openAll();
    const std::vector<FrameId> one{frameId(1)};
    bus.execute(SetEntryCommand(entry("autoneg.contrast", ParamSet{}.with("amount", 0.4))), one, roll);
    bus.execute(ToggleEntryCommand(StepId("autoneg.contrast"), false), one, roll);
    ASSERT_NE(current(1).get(StepId("autoneg.contrast")), nullptr);
    EXPECT_FALSE(current(1).get(StepId("autoneg.contrast"))->enabled);
    bus.execute(ToggleEntryCommand(StepId("autoneg.contrast"), true), one, roll);
    const model::EditEntry* e = current(1).get(StepId("autoneg.contrast"));
    EXPECT_TRUE(e->enabled);
    EXPECT_EQ(e->params.get<double>("amount").value(), 0.4);
}

// LF-BAT-02, LF-BAT-03
TEST_F(Commands, U_APP_04_PasteWithSelectionAndRedetect)
{
    const EditList source =
        EditList{}
            .with(entry("autoneg.crop", ParamSet{}.with("rect", model::NormRect{0.1, 0.1, 0.8, 0.8})))
            .with(entry("autoneg.filmbase", ParamSet{}.with("base", base::Vec3{0.7, 0.4, 0.3})))
            .with(entry("autoneg.exposure", ParamSet{}.with("ev", 0.7)));
    EditClipboard clip;
    clip.copy(source, frameId(3));
    const auto sel = clip.defaultSelection();
    EXPECT_EQ(sel, (std::set<StepId>{StepId("autoneg.exposure")})); // bildbezogene abgewählt
    EXPECT_TRUE(isFrameSpecific(StepId("autoneg.retouch")));

    // Modus Redetect mit crop: crop wird nicht kopiert, sondern zur Erkennung gemeldet
    const PasteCommand redetect =
        clip.paste({StepId("autoneg.crop"), StepId("autoneg.exposure")}, PasteMode::Redetect);
    EXPECT_EQ(redetect.redetect(), (std::set<StepId>{StepId("autoneg.crop")}));
    openAll();
    const std::vector<FrameId> one{frameId(4)};
    bus.execute(redetect, one, roll);
    EXPECT_EQ(current(4).get(StepId("autoneg.crop")), nullptr);
    ASSERT_NE(current(4).get(StepId("autoneg.exposure")), nullptr);
    // Modus Values kopiert crop
    bus.execute(clip.paste({StepId("autoneg.crop")}, PasteMode::Values), one, roll);
    ASSERT_NE(current(4).get(StepId("autoneg.crop")), nullptr);
}

// LF-BAT-02, LF-BAT-06
TEST_F(Commands, U_APP_05_PasteOn36FramesIsOneUndoStep)
{
    openAll(EditList{}.with(entry("autoneg.exposure", ParamSet{}.with("ev", 0.1))));
    std::vector<FrameId> all;
    for (const model::Frame& f : frames) {
        all.push_back(f.id);
    }
    EditClipboard clip;
    clip.copy(EditList{}.with(entry("autoneg.exposure", ParamSet{}.with("ev", 0.9))), frameId(1));
    const std::size_t groupsBefore = bus.undoStack().size();
    bus.execute(clip.paste(clip.defaultSelection(), PasteMode::Values), all, roll);
    EXPECT_EQ(bus.undoStack().size(), groupsBefore + 1);
    for (int i = 1; i <= 36; ++i) {
        EXPECT_EQ(current(i).get(StepId("autoneg.exposure"))->params.get<double>("ev").value(), 0.9);
    }
    ASSERT_TRUE(bus.undo());
    for (int i = 1; i <= 36; ++i) {
        EXPECT_EQ(current(i).get(StepId("autoneg.exposure"))->params.get<double>("ev").value(), 0.1) << i;
    }
    EXPECT_FALSE(bus.undoStack().canUndo());
    ASSERT_TRUE(bus.redo());
    EXPECT_EQ(current(36).get(StepId("autoneg.exposure"))->params.get<double>("ev").value(), 0.9);
}

// LF-BAT-05
TEST_F(Commands, U_APP_06_SyncAppliesToSelection)
{
    openAll();
    Session s;
    s.current = frameId(2);
    s.selection = {frameId(1), frameId(2), frameId(3), frameId(4), frameId(5)};
    SyncController sync;
    sync.setEnabled(true);
    bus.execute(SetEntryCommand(entry("autoneg.exposure", ParamSet{}.with("ev", 0.4))), sync.targets(s),
                roll);
    for (int i = 1; i <= 5; ++i) {
        EXPECT_NE(current(i).get(StepId("autoneg.exposure")), nullptr) << i;
    }
    EXPECT_EQ(current(6).get(StepId("autoneg.exposure")), nullptr);
    sync.setEnabled(false);
    bus.execute(SetEntryCommand(entry("autoneg.contrast", ParamSet{}.with("amount", 0.2))), sync.targets(s),
                roll);
    EXPECT_NE(current(2).get(StepId("autoneg.contrast")), nullptr);
    EXPECT_EQ(current(1).get(StepId("autoneg.contrast")), nullptr);
}

// LF-BAT-01, LF-BAS-06
TEST_F(Commands, U_APP_07_RollDefaultAndOwnValue)
{
    const auto ev = [](double v) { return entry("autoneg.exposure", ParamSet{}.with("ev", v)); };
    const auto base = [](double v) {
        return entry("autoneg.filmbase", ParamSet{}.with("base", base::Vec3{v, v, v}));
    };
    openAll();
    std::vector<FrameId> all;
    for (const model::Frame& f : frames) {
        all.push_back(f.id);
    }
    const std::set<StepId> locked{StepId("autoneg.filmbase")};
    bus.execute(SetRollDefaultsCommand(EditList{}.with(ev(0.3)).with(base(0.5)), locked), all, roll);
    // Bild 7: eigene Belichtung und eigene (aber gesperrte) Basis
    const std::vector<FrameId> seven{frameId(7)};
    bus.execute(SetEntryCommand(ev(-0.2)), seven, roll);
    bus.execute(SetEntryCommand(base(0.6)), seven, roll);
    bus.execute(SetRollDefaultsCommand(EditList{}.with(ev(0.5)).with(base(0.55)), locked), all, roll);
    EXPECT_EQ(current(7).get(StepId("autoneg.exposure"))->params.get<double>("ev").value(), -0.2);
    EXPECT_EQ(current(7).get(StepId("autoneg.exposure"))->origin, Origin::Manual);
    for (const int i : {1, 6, 8, 36}) {
        EXPECT_EQ(current(i).get(StepId("autoneg.exposure"))->params.get<double>("ev").value(), 0.5) << i;
        EXPECT_EQ(current(i).get(StepId("autoneg.exposure"))->origin, Origin::RollDefault);
    }
    for (const int i : {1, 7, 36}) { // gesperrte Basis gilt für alle
        EXPECT_EQ(current(i).get(StepId("autoneg.filmbase"))->params.get<base::Vec3>("base").value().x, 0.55)
            << i;
    }
}

// LF-DAT-03
TEST_F(Commands, U_APP_08_RapidChangesAreCoalesced)
{
    repo.delay = std::chrono::milliseconds(5);
    openAll();
    const std::vector<FrameId> one{frameId(1)};
    for (int i = 1; i <= 50; ++i) {
        bus.execute(SetEntryCommand(entry("autoneg.exposure", ParamSet{}.with("ev", i / 100.0))), one, roll);
    }
    queue.flush(); // kehrt erst nach dem Schreiben zurück
    EXPECT_LT(repo.writes.load(), 50);
    EXPECT_GE(repo.writes.load(), 1);
    const std::scoped_lock lock(repo.mutex);
    EXPECT_EQ(
        repo.saved.at(frameId(1)).edits.get(StepId("autoneg.exposure"))->params.get<double>("ev").value(),
        0.5);
}

// LF-LST-05
TEST_F(Commands, U_APP_15_PastedEntriesRememberSource)
{
    openAll();
    EditClipboard clip;
    clip.copy(EditList{}.with(entry("autoneg.contrast", ParamSet{}.with("amount", 0.3))), frameId(3));
    const std::vector<FrameId> four{frameId(4)};
    bus.execute(clip.paste(clip.defaultSelection(), PasteMode::Values), four, roll);
    const model::EditEntry* e = current(4).get(StepId("autoneg.contrast"));
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->origin, Origin::Copied);
    ASSERT_TRUE(e->copiedFrom.has_value());
    EXPECT_EQ(*e->copiedFrom, frameId(3));
}

} // namespace
} // namespace autoneg::application
