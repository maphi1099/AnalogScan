// Unit-Tests ContactSheetService (Architektur 9.13, LF-KTA-03, LF-KTA-04, LF-KTA-06): Bearbeitungs-
// liste je Slot, Auflösung der Slots, Ausgabe über IDocumentRenderer. Ports durch Fakes/Attrappen.

#include "autoneg/application/contact_sheet_service.hpp"
#include "autoneg/mocks/fake_task_executor.hpp"
#include "autoneg/mocks/fakes.hpp"
#include "autoneg/mocks/ports.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <span>
#include <thread>

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

/// Kennzeichnet jedes Feld mit seiner Bildnummer als Pixelwert, braucht etwas Zeit und zählt
/// gleichzeitige Aufrufe; schlägt für gewählte Bildnummern fehl.
class ParallelRenderer final : public IRenderer
{
public:
    base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        const int now = ++running;
        int p = peak.load();
        while (now > p && !peak.compare_exchange_weak(p, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        --running;
        const int n = r.frame.bytes()[15];
        if (std::ranges::find(failing, n) != failing.end()) {
            return base::makeError(base::ErrorCode::Corrupt, "Feld " + std::to_string(n) + " defekt");
        }
        return image::ImageBuffer::fromFloats(
            {4, 3, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear},
            std::vector<float>(36, static_cast<float>(n) / 100.0F));
    }
    base::Expected<model::SourceInfo> info(const model::FrameId&, const std::filesystem::path&) override
    {
        model::SourceInfo i;
        i.width = 1000;
        i.height = 1000;
        return i;
    }
    std::atomic<int> running{0};
    std::atomic<int> peak{0};
    std::vector<int> failing;
};

// NF-PERF-09, NF-PERF-11: Felder parallel, jedes Bild landet im richtigen Feld
TEST(ContactSheetServiceTest, M10_SlotsRenderInParallelAndStayInPlace)
{
    mocks::FakeTaskExecutor executor(8);
    JobScheduler jobs(executor);
    ParallelRenderer renderer;
    mocks::FakeColorEngine color;
    mocks::MockDocumentRenderer doc;
    ContactSheetService service(jobs, renderer, color, doc);
    std::vector<ContactSheetService::Item> items;
    for (int i = 1; i <= 36; ++i) {
        items.push_back(item(i));
    }
    const auto pages = service.render({}, items, kb(), model::Roll{}, {});
    ASSERT_TRUE(pages.has_value());
    EXPECT_GE(renderer.peak.load(), 2);
    EXPECT_LE(renderer.peak.load(), 8);
    std::size_t slots = 0;
    for (const ports::RenderedPage& page : *pages) {
        for (const layout::LayoutElement& e : page.layout.elements) {
            if (const auto* slot = std::get_if<layout::ImageSlot>(&e)) {
                ++slots;
                const int n = slot->frame.bytes()[15];
                ASSERT_TRUE(page.images.contains(slot->frame));
                // F32-Wert durch die Ausgabe-Transformation: monoton, also Reihenfolge prüfbar
                EXPECT_EQ(page.images.at(slot->frame).spec().width, 4) << n;
            }
        }
    }
    EXPECT_EQ(slots, 36U);
    // gleiche Werte je Bildnummer wie beim Rendern eines einzelnen Felds
    const auto single = service.render({}, std::span(items).subspan(4, 1), kb(), model::Roll{}, {});
    ASSERT_TRUE(single.has_value());
    const auto& one = single->front().images.at(items[4].frame.id);
    for (const ports::RenderedPage& page : *pages) {
        if (page.images.contains(items[4].frame.id)) {
            EXPECT_TRUE(std::ranges::equal(page.images.at(items[4].frame.id).hostBytes(), one.hostBytes()));
        }
    }
    // Budget nur für ein Feld: nacheinander
    ThreadConfig tight;
    tight.workers = 8;
    tight.memoryBudget = ContactSheetService::contactBytesPerPixel * 1000 * 1000;
    jobs.configure(tight);
    renderer.peak = 0;
    ASSERT_TRUE(service.render({}, items, kb(), model::Roll{}, {}).has_value());
    EXPECT_EQ(renderer.peak.load(), 1);
    EXPECT_EQ(jobs.budget().used(), 0U);
}

// LF-KTA-01: erster Fehler in Feldreihenfolge
TEST(ContactSheetServiceTest, M10_FirstSlotErrorInOrder)
{
    mocks::FakeTaskExecutor executor(8);
    JobScheduler jobs(executor);
    ParallelRenderer renderer;
    renderer.failing = {30, 7};
    mocks::FakeColorEngine color;
    mocks::MockDocumentRenderer doc;
    ContactSheetService service(jobs, renderer, color, doc);
    std::vector<ContactSheetService::Item> items;
    for (int i = 1; i <= 36; ++i) {
        items.push_back(item(i));
    }
    const auto pages = service.render({}, items, kb(), model::Roll{}, {});
    ASSERT_FALSE(pages.has_value());
    EXPECT_EQ(pages.error().message, "Feld 7 defekt");
}

} // namespace
} // namespace autoneg::application
