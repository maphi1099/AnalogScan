// Unit-Tests ContactSheetService (Architektur 9.13, LF-KTA-03, LF-KTA-04, LF-KTA-06): Bearbeitungs-
// liste je Slot, Auflösung der Slots, Ausgabe über IDocumentRenderer. Ports durch Fakes/Attrappen.

#include "autoneg/application/contact_sheet_service.hpp"
#include "autoneg/mocks/fake_task_executor.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/mocks/ports.hpp"

#include <gtest/gtest.h>

#include <mutex>

namespace autoneg::application {
namespace {

using model::EditEntry;
using model::EditList;
using model::ParamSet;
using model::StepId;
using ::testing::_;

EditEntry entry(const std::string& id, ParamSet p)
{
    EditEntry e;
    e.step = StepId(id);
    e.params = std::move(p);
    return e;
}

ContactSheetService::Item item(int n, EditList edits = {})
{
    ContactSheetService::Item i;
    std::array<std::uint8_t, 16> b{};
    b[15] = static_cast<std::uint8_t>(n);
    i.source.id = model::FrameId(b);
    i.source.frameNo = n;
    i.source.path = "rolle/DSC0" + std::to_string(7000 + n) + ".ARW";
    i.frame = {i.source.id, n, 1.5};
    i.edits = std::move(edits);
    return i;
}

const layout::FilmFormat& kb()
{
    return *layout::FilmFormat::find("Kleinbild 24×36");
}

/// Merkt sich die Anfragen und liefert ein Bild in der Zielgröße.
class RecordingRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        const std::scoped_lock lock(mutex);
        requests.push_back(r);
        const int w = std::max(1, r.profile.target.width);
        const int h = std::max(1, r.profile.target.height * 2 / 3);
        return image::ImageBuffer::fromFloats(
            {w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3, 0.5F));
    }
    std::mutex mutex;
    std::vector<pipeline::RenderRequest> requests;
};

// LF-KTA-04
TEST(ContactSheetServiceTest, M7_UniformUsesRollToneAndFrameGeometry)
{
    model::Roll roll;
    roll.defaults = EditList{}.with(entry("autoneg.exposure", ParamSet{}.with("ev", 0.0)));
    const EditList own =
        EditList{}
            .with(entry("autoneg.crop", ParamSet{}.with("rect", model::NormRect{0.1, 0.1, 0.8, 0.8})))
            .with(entry("autoneg.exposure", ParamSet{}.with("ev", 1.5)))
            .with(entry("autoneg.contrast", ParamSet{}.with("amount", 0.4)));
    layout::ContactSheetSpec s;
    s.showRebate = false;
    s.content = layout::SheetContent::Uniform;
    const EditList u = ContactSheetService::slotEdits(item(1, own), s, kb(), roll);
    ASSERT_NE(u.get(StepId("autoneg.crop")), nullptr); // Geometrie bleibt je Bild
    EXPECT_EQ(u.get(StepId("autoneg.contrast")), nullptr);
    ASSERT_NE(u.get(StepId("autoneg.exposure")), nullptr);
    EXPECT_DOUBLE_EQ(*u.get(StepId("autoneg.exposure"))->params.get<double>("ev"), 0.0); // Rollen-Standard
    s.content = layout::SheetContent::AsEdited;
    EXPECT_EQ(ContactSheetService::slotEdits(item(1, own), s, kb(), roll), own);
}

// LF-KTA-03
TEST(ContactSheetServiceTest, M7_RebateWidensCropToUncutField)
{
    const model::NormRect rect{0.2, 0.2, 0.6, 0.6};
    const EditList own =
        EditList{}.with(entry("autoneg.crop", ParamSet{}.with("rect", rect).with("inset", 0.02)));
    layout::ContactSheetSpec s;
    s.showRebate = true;
    const EditList l = ContactSheetService::slotEdits(item(1, own), s, kb(), model::Roll{});
    const auto wide = l.get(StepId("autoneg.crop"))->params.get<model::NormRect>("rect");
    ASSERT_TRUE(wide.has_value());
    EXPECT_NEAR(wide->w, 0.6 * 38.0 / 36.0, 1e-9);
    EXPECT_NEAR(wide->h, 0.6 * 35.0 / 24.0, 1e-9);
    EXPECT_NEAR(wide->x + wide->w / 2.0, 0.5, 1e-9); // um die Mitte erweitert
    EXPECT_DOUBLE_EQ(*l.get(StepId("autoneg.crop"))->params.get<double>("inset"), 0.0);
}

// LF-KTA-06
TEST(ContactSheetServiceTest, M7_PdfJobRendersAllSlotsAtLayoutResolution)
{
    mocks::FakeTaskExecutor executor(2);
    JobScheduler jobs(executor);
    RecordingRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::MockDocumentRenderer doc;
    ContactSheetService service(jobs, renderer, color, doc);
    std::vector<ContactSheetService::Item> items;
    for (int i = 1; i <= 36; ++i) {
        items.push_back(item(i));
    }
    layout::ContactSheetSpec s;
    std::vector<ports::RenderedPage> written;
    EXPECT_CALL(doc, writePdf(_, std::filesystem::path("kontakt.pdf")))
        .WillOnce([&](std::span<const ports::RenderedPage> pages, const std::filesystem::path&) {
            written.assign(pages.begin(), pages.end());
            return base::Expected<void>{};
        });
    const JobHandle h = service.exportPdf(items, std::make_shared<model::Roll>(), s, kb(), "kontakt.pdf");
    ASSERT_TRUE(h.wait().has_value());
    EXPECT_DOUBLE_EQ(h.progress(), 1.0);
    ASSERT_EQ(written.size(), 1U);
    EXPECT_EQ(written[0].images.size(), 36U);
    EXPECT_EQ(written[0].dpi, 300);
    EXPECT_FALSE(written[0].outputProfile.name.empty());
    ASSERT_EQ(renderer.requests.size(), 36U);
    for (const layout::LayoutElement& e : written[0].layout.elements) {
        if (const auto* slot = std::get_if<layout::ImageSlot>(&e)) {
            const double needPx = std::max(slot->rectMm.w, slot->rectMm.h) / 25.4 * 300.0;
            EXPECT_GE(written[0].images.at(slot->frame).spec().width, static_cast<int>(needPx));
        }
    }
    for (const pipeline::RenderRequest& r : renderer.requests) {
        EXPECT_EQ(r.profile.priority, ports::Priority::Batch);
    }
}

// LF-KTA-06
TEST(ContactSheetServiceTest, M7_PrintCancelledByUserIsReported)
{
    mocks::FakeTaskExecutor executor(1);
    JobScheduler jobs(executor);
    RecordingRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::MockDocumentRenderer doc;
    ContactSheetService service(jobs, renderer, color, doc);
    EXPECT_CALL(doc, print(_, _)).WillOnce([](auto, const ports::PrintSettings&) -> base::Expected<void> {
        return base::makeError(base::ErrorCode::Cancelled, "Druckdialog abgebrochen");
    });
    const auto r = service.print({item(1), item(2)}, nullptr, {}, kb(), {}).wait();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::Cancelled);
}

} // namespace
} // namespace autoneg::application
