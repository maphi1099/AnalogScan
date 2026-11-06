// Gruppierung der Retusche-Flächen (K-M10-23, Wunsch des Nutzers): zusammenhängende Flächen sind
// ein Listeneintrag, getrennte mehrere; verbinden vereinigt, radieren teilt.

#include "ui/retouch_geometry.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace autoneg::ui {
namespace {

using model::RetouchShape;
using model::RetouchShapes;
using model::RetouchStroke;

struct Names
{
    int n = 0;
    std::string operator()() { return "Fläche " + std::to_string(++n); }
};

RetouchStroke line(double x0, double y0, double x1, double y1, double width = 0.02, bool erase = false)
{
    return RetouchStroke{erase, width, {{x0, y0}, {x1, y1}}};
}

TEST(RetouchGeometryTest, M10_SeparateAreasAreSeparateShapes)
{
    const RetouchGeometry geo(2.0 / 3.0);
    Names names;
    RetouchShapes s = geo.addStroke({}, line(0.1, 0.2, 0.2, 0.2), std::ref(names));
    s = geo.addStroke(s, line(0.6, 0.6, 0.7, 0.6), std::ref(names));
    ASSERT_EQ(s.size(), 2U);
    EXPECT_EQ(s[0].name, "Fläche 1");
    EXPECT_EQ(s[1].name, "Fläche 2");
    EXPECT_EQ(s[0].kind, RetouchShape::Kind::Brush);
}

TEST(RetouchGeometryTest, M10_ConnectingStrokeMergesAreas)
{
    const RetouchGeometry geo(2.0 / 3.0);
    Names names;
    RetouchShapes s = geo.addStroke({}, line(0.1, 0.2, 0.2, 0.2), std::ref(names));
    s = geo.addStroke(s, line(0.6, 0.2, 0.7, 0.2), std::ref(names));
    s[1].enabled = false;
    s = geo.addStroke(s, line(0.2, 0.2, 0.6, 0.2), std::ref(names)); // Brücke
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "Fläche 1"); // Name und Haken der frühesten Fläche
    EXPECT_TRUE(s[0].enabled);
    EXPECT_EQ(s[0].strokes.size(), 3U);
}

TEST(RetouchGeometryTest, M10_EraserSplitsArea)
{
    const RetouchGeometry geo(1.0);
    Names names;
    RetouchShapes s = geo.addStroke({}, line(0.1, 0.5, 0.9, 0.5, 0.04), std::ref(names));
    ASSERT_EQ(s.size(), 1U);
    s = geo.addStroke(s, line(0.5, 0.3, 0.5, 0.7, 0.08, true), std::ref(names)); // quer durch
    ASSERT_EQ(s.size(), 2U);
    EXPECT_EQ(s[0].name, "Fläche 1");
    EXPECT_NE(s[1].name, s[0].name);
    // jede Hälfte nur auf ihrer Seite
    EXPECT_EQ(geo.hit(s, {0.2, 0.5}), std::optional<std::size_t>(0));
    EXPECT_EQ(geo.hit(s, {0.8, 0.5}), std::optional<std::size_t>(1));
    EXPECT_FALSE(geo.hit(s, {0.5, 0.5}).has_value()); // radiert
    // Radierer entfernt eine ganze Fläche
    s = geo.addStroke(s, line(0.05, 0.5, 0.47, 0.5, 0.2, true), std::ref(names));
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(geo.hit(s, {0.8, 0.5}), std::optional<std::size_t>(0));
}

TEST(RetouchGeometryTest, M10_CirclesStayAndAreHit)
{
    const RetouchGeometry geo(0.5);
    RetouchShape c;
    c.kind = RetouchShape::Kind::Circle;
    c.name = "Kreis 1";
    c.center = {0.5, 0.5};
    c.radius = 0.05;
    Names names;
    RetouchShapes s = geo.addStroke({c}, line(0.45, 0.5, 0.55, 0.5), std::ref(names)); // Fläche über dem Kreis
    ASSERT_EQ(s.size(), 2U);
    EXPECT_EQ(s[0].kind, RetouchShape::Kind::Circle);
    // Kreis: Abstand in Bildbreiten (y mit dem Seitenverhältnis)
    EXPECT_FALSE(geo.hit({c}, {0.5, 0.65}).has_value()); // 0,15 Höhe = 0,075 Breite > 0,05
    EXPECT_TRUE(geo.hit({c}, {0.5, 0.58}).has_value());  // 0,08 Höhe = 0,04 Breite
}

} // namespace
} // namespace autoneg::ui
