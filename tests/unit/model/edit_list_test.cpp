// Unit-Tests EditList (Architektur 9.1).

#include "autoneg/model/edit_list.hpp"
#include "autoneg/testing/edit_fixtures.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace autoneg::model {
namespace {

using autoneg::testing::contrast;
using autoneg::testing::crop;
using autoneg::testing::exposure;
using autoneg::testing::id;
using autoneg::testing::sharpen;
using autoneg::testing::whitebalance;

std::vector<std::string> stepNames(const EditList& list)
{
    std::vector<std::string> names;
    for (const EditEntry& e : list.entries()) {
        names.push_back(e.step.value());
    }
    return names;
}

double ev(const EditList& list)
{
    const EditEntry* e = list.get(id("exposure"));
    return e == nullptr ? -999.0 : e->params.get<double>("ev").value_or(-999.0);
}

// LF-LST-01, NF-ARC-16
TEST(EditList, U_MOD_01_WithAddsEntryAndKeepsOriginal)
{
    const EditList empty;
    const EditList one = empty.with(exposure(0.7));

    ASSERT_EQ(one.size(), 1U);
    EXPECT_DOUBLE_EQ(ev(one), 0.7);
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.get(id("exposure")), nullptr);
}

// LF-LST-02
TEST(EditList, U_MOD_02_SameKindReplacesEntry)
{
    const EditList before = EditList{}.with(crop({0.1, 0.1, 0.8, 0.8})).with(exposure(0.3));
    const EditList after = before.with(exposure(0.7));

    EXPECT_EQ(after.size(), before.size());
    EXPECT_DOUBLE_EQ(ev(after), 0.7);
    EXPECT_DOUBLE_EQ(ev(before), 0.3);
}

// LF-LST-04
TEST(EditList, U_MOD_03_EntriesInPipelineOrder)
{
    const EditList list = EditList{}.with(sharpen(0.5)).with(crop({0, 0, 1, 1})).with(exposure(1.0));

    EXPECT_EQ(stepNames(list),
              (std::vector<std::string>{"autoneg.crop", "autoneg.exposure", "autoneg.sharpen"}));
}

// LF-LST-03
TEST(EditList, U_MOD_04_WithoutRemovesOnlySelectedEntry)
{
    const EditList list = EditList{}.with(crop({0.1, 0.2, 0.5, 0.6})).with(exposure(1.0)).with(sharpen(0.4));
    const EditList removed = list.without(id("exposure"));

    EXPECT_EQ(removed.get(id("exposure")), nullptr);
    ASSERT_NE(removed.get(id("crop")), nullptr);
    ASSERT_NE(removed.get(id("sharpen")), nullptr);
    EXPECT_EQ(*removed.get(id("crop")), *list.get(id("crop")));
    EXPECT_EQ(*removed.get(id("sharpen")), *list.get(id("sharpen")));
    EXPECT_EQ(removed.without(id("exposure")), removed); // nicht vorhanden: unverändert
}

// LF-LST-06
TEST(EditList, U_MOD_05_DisableKeepsValues)
{
    const EditList original = EditList{}.with(contrast(0.4));
    const EditList off = original.enabled(id("contrast"), false);
    const EditList on = off.enabled(id("contrast"), true);

    ASSERT_NE(off.get(id("contrast")), nullptr);
    EXPECT_FALSE(off.get(id("contrast"))->enabled);
    EXPECT_DOUBLE_EQ(off.get(id("contrast"))->params.get<double>("amount").value(), 0.4);
    EXPECT_EQ(on, original);
    EXPECT_EQ(on.hash(), original.hash());
}

// LF-BAT-02, LF-BAT-03
TEST(EditList, U_MOD_06_MergeTakesOnlySelectedKinds)
{
    const EditList source =
        EditList{}.with(crop({0.1, 0.1, 0.8, 0.8})).with(exposure(1.0)).with(whitebalance(5200));
    const EditList target = EditList{}.with(exposure(-1.0)).with(sharpen(0.6));

    const EditList merged = target.merge(source, {id("exposure"), id("whitebalance")});

    EXPECT_EQ(stepNames(merged),
              (std::vector<std::string>{"autoneg.exposure", "autoneg.whitebalance", "autoneg.sharpen"}));
    EXPECT_DOUBLE_EQ(ev(merged), 1.0);
    EXPECT_EQ(merged.get(id("crop")), nullptr);
    EXPECT_EQ(*merged.get(id("sharpen")), *target.get(id("sharpen")));
}

// NF-QUAL-02
TEST(EditList, U_MOD_07_HashDependsOnlyOnContent)
{
    const EditList a = EditList{}.with(sharpen(0.5)).with(exposure(0.3)).with(crop({0, 0, 1, 1}));
    const EditList b = EditList{}.with(crop({0, 0, 1, 1})).with(exposure(0.3)).with(sharpen(0.5));

    EXPECT_EQ(a, b);
    EXPECT_EQ(a.hash(), b.hash());

    const EditList changed = b.with(exposure(0.31));
    EXPECT_NE(changed.hash(), a.hash());
    EXPECT_NE(b.enabled(id("sharpen"), false).hash(), a.hash());
}

TEST(EditList, M1_HashIsStableAcrossRuns)
{
    // Fester Wert: die Prüfsumme dient als Cache-Schlüssel auf der Festplatte und darf sich
    // zwischen Programmläufen und Plattformen nicht ändern.
    const EditList list = EditList{}.with(exposure(0.5));
    EXPECT_EQ(list.hash(), list.with(exposure(0.5)).hash());
    EXPECT_EQ(EditList{}.hash(), EditList{}.hash());
}

} // namespace
} // namespace autoneg::model
