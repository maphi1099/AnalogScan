// Unit-Tests ParamSet, FrameId, Roll, JsonValue (Architektur 9.1, A-03).

#include "autoneg/model/ids.hpp"
#include "autoneg/model/json_value.hpp"
#include "autoneg/model/param_set.hpp"
#include "autoneg/model/roll.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace autoneg::model {
namespace {

// NF-REL-02
TEST(ParamSet, U_MOD_12_WrongTypeOrUnknownNameIsDefinedError)
{
    const ParamSet params = ParamSet{}.with("ev", 0.7);

    const auto asInt = params.get<int>("ev");
    ASSERT_FALSE(asInt.has_value());
    EXPECT_EQ(asInt.error().code, base::ErrorCode::TypeMismatch);

    const auto unknown = params.get<double>("unbekannt");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, base::ErrorCode::NotFound);

    ASSERT_TRUE(params.get<double>("ev").has_value());
    EXPECT_DOUBLE_EQ(*params.get<double>("ev"), 0.7);
}

TEST(ParamSet, M1_SortedAndOrderIndependent)
{
    const ParamSet a = ParamSet{}.with("radius", 1.2).with("amount", 0.6).with("threshold", 2);
    const ParamSet b = ParamSet{}.with("threshold", 2).with("amount", 0.6).with("radius", 1.2);

    EXPECT_EQ(a, b);
    EXPECT_EQ(a.hash(), b.hash());
    EXPECT_EQ(a.entries().front().first, "amount");
    EXPECT_NE(a.with("radius", 1.3).hash(), a.hash());
    EXPECT_FALSE(a.without("amount").contains("amount"));
    // int 2 und double 2.0 sind verschiedene Werte
    EXPECT_NE(ParamSet{}.with("x", 2).hash(), ParamSet{}.with("x", 2.0).hash());
}

// LF-DAT-06
TEST(FrameId, U_MOD_13_CompareAndHex)
{
    std::array<std::uint8_t, 16> lowBytes{};
    std::array<std::uint8_t, 16> highBytes{};
    for (std::size_t i = 0; i < 16; ++i) {
        lowBytes[i] = static_cast<std::uint8_t>(i);
        highBytes[i] = static_cast<std::uint8_t>(0xf0 + i);
    }
    const FrameId low(lowBytes);
    const FrameId high(highBytes);

    EXPECT_EQ(low.toHex(), "000102030405060708090a0b0c0d0e0f");
    EXPECT_EQ(high.toHex(), "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    EXPECT_EQ(low.toHex().size(), 32U);
    EXPECT_LT(low, high);
    EXPECT_EQ(low, FrameId(lowBytes));

    std::vector<FrameId> ids{high, low, high};
    std::ranges::sort(ids);
    EXPECT_EQ(ids, (std::vector<FrameId>{low, high, high}));

    const auto parsed = FrameId::fromHex("F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, high);
    EXPECT_FALSE(FrameId::fromHex("zz").has_value());
    EXPECT_FALSE(FrameId::fromHex("g0f1f2f3f4f5f6f7f8f9fafbfcfdfeff").has_value());
}

TEST(Roll, M1_IndexOf)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 1;
    const FrameId a(bytes);
    bytes[0] = 2;
    const FrameId b(bytes);
    Roll roll;
    roll.frames = {a, b};

    EXPECT_EQ(roll.indexOf(b), 1);
    EXPECT_EQ(roll.indexOf(FrameId{}), -1);
}

TEST(JsonValue, M1_ObjectsKeepKeyOrderAndCompare)
{
    JsonValue doc = JsonValue::object({{"schema", 1}, {"zukunft", JsonValue::array({1, "x", nullptr})}});
    doc.set("app", "AutoNeg 1.0.0");
    doc.set("schema", 2);

    const auto* members = doc.asObject();
    ASSERT_NE(members, nullptr);
    ASSERT_EQ(members->size(), 3U);
    EXPECT_EQ((*members)[0].key, "schema");
    EXPECT_EQ((*members)[1].key, "zukunft");
    EXPECT_EQ((*members)[2].key, "app");
    EXPECT_EQ(doc.find("schema")->number(), 2.0);
    EXPECT_TRUE(doc.find("zukunft")->isArray());
    EXPECT_EQ(doc.find("fehlt"), nullptr);
    EXPECT_FALSE(JsonValue("text").number().has_value());

    JsonValue copy = doc;
    EXPECT_EQ(copy, doc);
    copy.set("app", "andere");
    EXPECT_NE(copy, doc);
    EXPECT_TRUE(JsonValue{}.isNull());
}

} // namespace
} // namespace autoneg::model
