// Unit-Tests EditHistory (Architektur 9.1).

#include "autoneg/model/edit_history.hpp"
#include "autoneg/testing/edit_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace autoneg::model {
namespace {

using autoneg::testing::exposure;
using autoneg::testing::id;

EditList listWithEv(double value)
{
    return EditList{}.with(exposure(value));
}

// LF-EDT-15
TEST(EditHistory, U_MOD_08_CommitUndoRedo)
{
    EditHistory history;
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.canRedo());

    history.commit(listWithEv(1), "Belichtung");
    history.commit(listWithEv(2), "Belichtung");
    history.commit(listWithEv(3), "Belichtung");
    EXPECT_EQ(history.current(), listWithEv(3));
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());

    ASSERT_TRUE(history.undo().has_value());
    EXPECT_EQ(history.current(), listWithEv(2));
    ASSERT_TRUE(history.undo().has_value());
    EXPECT_EQ(history.current(), listWithEv(1));
    EXPECT_TRUE(history.canUndo());
    EXPECT_TRUE(history.canRedo());

    const auto redone = history.redo();
    ASSERT_TRUE(redone.has_value());
    EXPECT_EQ(redone->list, listWithEv(2));
    EXPECT_EQ(history.current(), listWithEv(2));
    EXPECT_TRUE(history.canRedo());
}

// LF-EDT-15
TEST(EditHistory, U_MOD_09_NewCommitDropsRedoBranch)
{
    EditHistory history;
    history.commit(listWithEv(1), "a");
    history.commit(listWithEv(2), "b");
    history.commit(listWithEv(3), "c");
    (void)history.undo();
    (void)history.undo();

    const Revision fresh = history.commit(listWithEv(9), "neu");

    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.current(), listWithEv(9));
    // Ausgangsstand und Stand 1 liegen vor dem neuen Stand
    EXPECT_EQ(history.position(), 2U);
    EXPECT_EQ(history.size(), 3U);
    // Folgenummern bleiben eindeutig, auch nach verworfenem Zweig
    EXPECT_EQ(fresh.seq, 4);
}

// LF-EDT-15
TEST(EditHistory, U_MOD_10_AtLeast100UndoSteps)
{
    constexpr int steps = 150;
    EditHistory history;
    for (int i = 1; i <= steps; ++i) {
        history.commit(listWithEv(i), "Belichtung");
    }
    for (int i = steps - 1; i >= 0; --i) {
        ASSERT_TRUE(history.undo().has_value()) << "Schritt " << i;
        EXPECT_EQ(history.current(), i == 0 ? EditList{} : listWithEv(i));
    }
    EXPECT_FALSE(history.canUndo());
    EXPECT_EQ(history.current(), EditList{});
}

// LF-EDT-15, LF-LST-03 (eigenschaftsbasiert)
TEST(EditHistory, U_MOD_11_RandomSequencesAreFullyReversible)
{
    constexpr std::uint64_t seed = 20261019;
    constexpr int sequences = 1000;
    // Nur Rohwerte des Generators verwenden: Verteilungen der Standardbibliothek liefern
    // je Plattform andere Folgen, der Zufallsgenerator selbst ist überall gleich.
    std::mt19937_64 rng(seed);
    const auto pick = [&rng](std::uint64_t n) { return rng() % n; };
    const std::vector<std::string> kinds = {"crop", "exposure", "contrast", "sharpen", "saturation"};

    for (int s = 0; s < sequences; ++s) {
        SCOPED_TRACE("Seed " + std::to_string(seed) + ", Folge " + std::to_string(s));
        EditList start;
        for (std::size_t k = 0; k < kinds.size(); ++k) {
            if (pick(2) == 0) {
                start = start.with(autoneg::testing::entry(kinds[k], static_cast<int>(k) * 10, "v",
                                                           static_cast<double>(pick(100))));
            }
        }
        EditHistory history(start);
        std::vector<EditList> states{start};
        const int length = 1 + static_cast<int>(pick(30));
        for (int i = 0; i < length; ++i) {
            const std::size_t k = pick(kinds.size());
            const EditList& cur = history.current();
            EditList next;
            switch (pick(3)) {
            case 0:
                next = cur.with(autoneg::testing::entry(kinds[k], static_cast<int>(k) * 10, "v",
                                                        static_cast<double>(pick(100))));
                break;
            case 1:
                next = cur.without(id(kinds[k]));
                break;
            default:
                next = cur.enabled(id(kinds[k]), pick(2) == 0);
                break;
            }
            history.commit(next, "zufällig");
            states.push_back(next);
        }
        for (std::size_t i = states.size() - 1; i > 0; --i) {
            ASSERT_TRUE(history.undo().has_value());
            ASSERT_EQ(history.current(), states[i - 1]);
        }
        EXPECT_FALSE(history.canUndo());
        EXPECT_EQ(history.current(), start);
        EXPECT_EQ(history.current().hash(), start.hash());
        for (std::size_t i = 1; i < states.size(); ++i) {
            ASSERT_TRUE(history.redo().has_value());
        }
        EXPECT_EQ(history.current(), states.back());
    }
}

} // namespace
} // namespace autoneg::model
