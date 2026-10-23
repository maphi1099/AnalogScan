// Unit-Tests der Oberfläche (U-UI-01 … U-UI-05, Architektur 9.14). Der UiController ist durch
// eine Attrappe ersetzt, die Befehle nur aufzeichnet; Widgets laufen ohne Bildschirm.

#include "autoneg/steps/registry.hpp"
#include "autoneg/ui/canvas_tools.hpp"
#include "autoneg/ui/edit_list_model.hpp"
#include "autoneg/ui/panels.hpp"

#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>

namespace autoneg::ui {
namespace {

void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value); // plattformabhängig (PROGRESS.md)
#else
    setenv(name, value, 1);
#endif
}

/// Hält die Qt-Instanz für alle Tests und baut sie nach dem letzten Test ab (sonst meldet
/// LeakSanitizer Qts Schriftcaches als Lecks).
template <class T>
class QtInstance final : public ::testing::Environment
{
public:
    static std::unique_ptr<T>& slot()
    {
        static std::unique_ptr<T> instance;
        return instance;
    }
    void TearDown() override { slot().reset(); }
};

/// Eine QApplication für alle Tests des Programms, ohne Bildschirm.
void ensureApp()
{
    auto& app = QtInstance<QApplication>::slot();
    if (!app) {
        setEnv("QT_QPA_PLATFORM", "offscreen");
#if defined(AUTONEG_QT_PLUGIN_DIR)
        setEnv("QT_PLUGIN_PATH", AUTONEG_QT_PLUGIN_DIR);
#endif
        static int argc = 1;
        static char arg0[] = "autoneg_test_ui";
        static char* argv[] = {arg0, nullptr};
        app = std::make_unique<QApplication>(argc, argv);
    }
}

// Abbau nach dem letzten Test; gtest übernimmt das Objekt
[[maybe_unused]] ::testing::Environment* const qtTeardown =
    ::testing::AddGlobalTestEnvironment(new QtInstance<QApplication>);

const pipeline::StepRegistry& standardRegistry()
{
    static pipeline::StepRegistry r;
    static steps::FlatFieldCache flat;
    static const bool ok = steps::registerStandardSteps(r, flat).has_value();
    EXPECT_TRUE(ok);
    return r;
}

/// Zeichnet Befehle auf und wendet sie auf eine eigene Liste an.
class RecordingController final : public UiController
{
public:
    RecordingController()
        : UiController(standardRegistry())
    {}
    bool apply(std::unique_ptr<application::ICommand> c) override
    {
        model::Roll roll;
        const application::CommandContext ctx{model::FrameId{}, roll, standardRegistry()};
        list = c->apply(list, ctx);
        commands.push_back(std::move(c));
        return true;
    }
    void preview(const model::EditList& l) override { previews.push_back(l); }
    [[nodiscard]] model::EditList currentList() const override { return list; }

    model::EditList list;
    std::vector<std::unique_ptr<application::ICommand>> commands;
    std::vector<model::EditList> previews;
};

model::EditEntry entry(const std::string& id, model::ParamSet p, model::Origin o)
{
    model::EditEntry e;
    e.step = model::StepId(id);
    e.params = std::move(p);
    e.origin = o;
    if (const pipeline::IStep* s = standardRegistry().find(e.step)) {
        e.rank = s->descriptor().rank;
    }
    return e;
}

model::EditList fiveEntries()
{
    // absichtlich nicht in Pipeline-Reihenfolge eingefügt
    return model::EditList{}
        .with(entry("autoneg.sharpen", model::ParamSet{}.with("amount", 0.8), model::Origin::Preset))
        .with(entry("autoneg.exposure", model::ParamSet{}.with("ev", 0.5), model::Origin::Manual))
        .with(entry("autoneg.crop", model::ParamSet{}.with("rect", model::NormRect{0.1, 0.1, 0.8, 0.8}),
                    model::Origin::Automatic))
        .with(entry("autoneg.contrast", model::ParamSet{}.with("amount", 0.2), model::Origin::Copied))
        .with(entry("autoneg.saturation", model::ParamSet{}.with("saturation", 0.1),
                    model::Origin::RollDefault));
}

// U-UI-01: LF-LST-01, LF-LST-04, LF-LST-05
TEST(EditListModelTest, U_UI_01_TableInPipelineOrder)
{
    ensureApp();
    RecordingController ui;
    EditListModel m(ui);
    m.setList(fiveEntries());
    ASSERT_EQ(m.rowCount(), 5);
    const auto text = [&m](int r, int c) { return m.data(m.index(r, c)).toString().toStdString(); };
    // Pipeline-Reihenfolge nach Rang: Zuschnitt, Belichtung, Kontrast, Sättigung, Schärfen
    EXPECT_EQ(text(0, EditListModel::Name), "Zuschnitt");
    EXPECT_EQ(text(1, EditListModel::Name), "Belichtung");
    EXPECT_EQ(text(2, EditListModel::Name), "Kontrast");
    EXPECT_EQ(text(3, EditListModel::Name),
              stepName(standardRegistry(), model::StepId("autoneg.saturation")));
    EXPECT_EQ(text(4, EditListModel::Name), "Schärfen");
    for (int r = 0; r + 1 < 5; ++r) {
        EXPECT_LE(m.entryAt(r)->rank, m.entryAt(r + 1)->rank);
    }
    EXPECT_EQ(text(1, EditListModel::Value), "ev +0,50");
    EXPECT_EQ(text(0, EditListModel::Origin), "automatisch");
    EXPECT_EQ(text(1, EditListModel::Origin), "manuell");
    EXPECT_EQ(text(2, EditListModel::Origin), "kopiert");
    EXPECT_EQ(text(3, EditListModel::Origin), "Rollen-Standard");
    EXPECT_EQ(text(4, EditListModel::Origin), "Vorlage");
    EXPECT_EQ(m.data(m.index(0, EditListModel::Active), Qt::CheckStateRole).toInt(), Qt::Checked);
}

// U-UI-02: LF-LST-03
TEST(EditListModelTest, U_UI_02_RemoveCreatesOneRemoveCommand)
{
    ensureApp();
    RecordingController ui;
    ui.list = fiveEntries();
    EditListModel m(ui);
    m.setList(ui.list);
    const model::StepId row2 = m.entryAt(2)->step;
    m.removeEntry(2);
    ASSERT_EQ(ui.commands.size(), 1U);
    EXPECT_NE(dynamic_cast<application::RemoveEntryCommand*>(ui.commands[0].get()), nullptr);
    EXPECT_EQ(ui.list.get(row2), nullptr);
    EXPECT_EQ(ui.list.size(), 4U);
    // Umschalten: ein ToggleEntryCommand, Werte bleiben (LF-LST-06)
    m.setList(ui.list);
    ASSERT_TRUE(m.setData(m.index(0, EditListModel::Active), Qt::Unchecked, Qt::CheckStateRole));
    ASSERT_EQ(ui.commands.size(), 2U);
    EXPECT_NE(dynamic_cast<application::ToggleEntryCommand*>(ui.commands[1].get()), nullptr);
    EXPECT_FALSE(ui.list.get(m.entryAt(0)->step)->enabled);
}

// U-UI-03: LF-EDT-02
TEST(CropRotateToolTest, U_UI_03_StraightenByLine)
{
    CropRotateTool t;
    t.setSourceSize({1000, 800});
    const double a = t.straighten({100, 200}, {900, 230});
    const double expected = std::atan(30.0 / 800.0) * 180.0 / std::numbers::pi;
    EXPECT_NEAR(a, expected, 0.1);
    EXPECT_NEAR(t.angle(), expected, 0.1);
    // senkrechte Linie: Winkel bezogen auf die Senkrechte
    EXPECT_NEAR(t.straighten({500, 100}, {520, 900}), -std::atan(20.0 / 800.0) * 180.0 / std::numbers::pi,
                0.1);
}

// U-UI-04: LF-EDT-01
TEST(CropRotateToolTest, U_UI_04_AspectKeptWhileDragging)
{
    CropRotateTool t;
    t.setSourceSize({6000, 4000});
    t.setState({0.1, 0.1, 0.8, 0.8}, 0.0);
    t.setAspect(4.0 / 3.0);
    EXPECT_NEAR(t.pixelAspect(), 4.0 / 3.0, 1e-6);
    for (const auto& [h, p] : std::vector<std::pair<CropRotateTool::Handle, QPointF>>{
             {CropRotateTool::Handle::BottomRight, {0.7, 0.9}},
             {CropRotateTool::Handle::TopLeft, {0.2, 0.05}},
             {CropRotateTool::Handle::Right, {0.6, 0.5}},
             {CropRotateTool::Handle::Bottom, {0.5, 0.6}}}) {
        t.drag(h, p);
        EXPECT_NEAR(t.pixelAspect(), 4.0 / 3.0, 1e-6);
        EXPECT_GE(t.rect().x, 0.0);
        EXPECT_LE(t.rect().x + t.rect().w, 1.0 + 1e-9);
        EXPECT_LE(t.rect().y + t.rect().h, 1.0 + 1e-9);
    }
    t.swapOrientation();
    EXPECT_NEAR(t.pixelAspect(), 3.0 / 4.0, 1e-6);
    ASSERT_TRUE(t.aspect().has_value());
    EXPECT_NEAR(*t.aspect(), 0.75, 1e-12);
}

// U-UI-05: LF-KON-09
TEST(GradationPanelTest, U_UI_05_GradeButtonsSetParameter)
{
    ensureApp();
    RecordingController ui;
    GradationPanel panel(ui);
    const std::vector<double> expected{-1.0, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 2.0};
    const std::vector<QString> labels{"00", "0", "½", "1", "1½", "2", "2½", "3", "3½", "4", "4½", "5"};
    for (int i = 0; i <= 12; ++i) {
        ASSERT_NE(panel.button(i), nullptr);
        if (i < 12) {
            EXPECT_EQ(panel.button(i)->text(), labels[static_cast<std::size_t>(i)]);
        }
        panel.button(i)->click();
        const model::EditEntry* g = ui.list.get(model::StepId("autoneg.gradation"));
        ASSERT_NE(g, nullptr);
        EXPECT_DOUBLE_EQ(*g->params.get<double>("grade"), expected[static_cast<std::size_t>(i)]) << i;
    }
    EXPECT_EQ(ui.commands.size(), 13U);
    // Split-Grade 60/40 (LF-KON-12)
    panel.setSplit(0.6, 0.4);
    const model::EditEntry* g = ui.list.get(model::StepId("autoneg.gradation"));
    EXPECT_DOUBLE_EQ(*g->params.get<double>("splitSoft"), 0.6);
    EXPECT_DOUBLE_EQ(*g->params.get<double>("splitHard"), 0.4);
}

} // namespace
} // namespace autoneg::ui
