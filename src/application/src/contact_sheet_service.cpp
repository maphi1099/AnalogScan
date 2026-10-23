#include "autoneg/application/contact_sheet_service.hpp"

#include "autoneg/steps/step_ids.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>

namespace autoneg::application {

namespace {

/// Bildbezogene Schritte, die beim einheitlichen Inhalt je Bild bleiben.
bool keepsPerFrame(const model::StepId& s)
{
    static const std::array<std::string_view, 8> ids{
        steps::id::flatfield, steps::id::orientation, steps::id::rotation, steps::id::crop,
        steps::id::retouch,   steps::id::filmmode,    steps::id::bwmix,    steps::id::filmbase};
    return std::ranges::find(ids, s.value()) != ids.end();
}

} // namespace

std::vector<layout::PageLayout> ContactSheetService::build(const layout::ContactSheetSpec& s,
                                                           std::span<const Item> items,
                                                           const layout::FilmFormat& fmt) const
{
    std::vector<layout::SheetFrame> frames;
    for (const Item& i : items) {
        frames.push_back(i.frame);
    }
    return layout::ContactSheetLayouter{}.layout(s, frames, fmt);
}

model::EditList ContactSheetService::slotEdits(const Item& item, const layout::ContactSheetSpec& s,
                                               const layout::FilmFormat& fmt, const model::Roll& roll)
{
    model::EditList l;
    if (s.content == layout::SheetContent::Uniform) {
        for (const model::EditEntry& e : roll.defaults.entries()) {
            l = l.with(e);
        }
        for (const model::EditEntry& e : item.edits.entries()) {
            if (keepsPerFrame(e.step)) {
                l = l.with(e);
            }
        }
    } else {
        l = item.edits;
    }
    if (s.showRebate) {
        // Zuschnitt auf das ungeschnittene Feld erweitern (Bildabstand × Filmbreite)
        if (const model::EditEntry* crop = l.get(model::StepId(std::string(steps::id::crop)))) {
            if (const auto r = crop->params.get<model::NormRect>("rect")) {
                const double fx = fmt.pitchMm / fmt.frameMm.w;
                const double fy = fmt.filmWidthMm / fmt.frameMm.h;
                const double cx = r->x + r->w / 2.0;
                const double cy = r->y + r->h / 2.0;
                model::NormRect wide{cx - r->w * fx / 2.0, cy - r->h * fy / 2.0, r->w * fx, r->h * fy};
                wide.x = std::clamp(wide.x, 0.0, 1.0);
                wide.y = std::clamp(wide.y, 0.0, 1.0);
                wide.w = std::min(wide.w, 1.0 - wide.x);
                wide.h = std::min(wide.h, 1.0 - wide.y);
                model::EditEntry e = *crop;
                e.params = e.params.with("rect", wide).with("inset", 0.0);
                l = l.with(e);
            }
        }
    }
    return l;
}

base::Expected<std::vector<ports::RenderedPage>>
ContactSheetService::render(const layout::ContactSheetSpec& s, std::span<const Item> items,
                            const layout::FilmFormat& fmt, const model::Roll& roll,
                            const image::CancelToken& cancel,
                            const std::function<void(int done, int total)>& progress) const
{
    auto working = color_.profile(ports::ProfileId::WorkingRec2020Linear);
    auto srgb = color_.profile(ports::ProfileId::Srgb);
    if (!working || !srgb) {
        return base::makeError(base::ErrorCode::Internal, "Farbprofile nicht verfügbar");
    }
    auto xf = color_.transform(*working, *srgb, ports::Intent::Perceptual);
    if (!xf) {
        return std::unexpected(xf.error());
    }
    const auto pages = build(s, items, fmt);
    // Alle Felder aller Seiten; sie werden parallel gerendert (Ebene 3, wie der Stapelexport)
    // und danach in fester Reihenfolge den Seiten zugeordnet
    struct Task
    {
        std::size_t page = 0;
        const layout::ImageSlot* slot = nullptr;
        const Item* item = nullptr;
    };
    std::vector<Task> tasks;
    for (std::size_t pi = 0; pi < pages.size(); ++pi) {
        for (const layout::LayoutElement& e : pages[pi].elements) {
            const auto* slot = std::get_if<layout::ImageSlot>(&e);
            if (slot == nullptr) {
                continue;
            }
            const auto item = std::ranges::find(items, slot->frame, [](const Item& i) { return i.frame.id; });
            if (item != items.end()) {
                tasks.push_back({pi, slot, &*item});
            }
        }
    }
    std::vector<std::size_t> bytes(tasks.size(), 0);
    std::size_t largest = 0;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (auto info = renderer_.info(tasks[i].item->source.id, tasks[i].item->source.path); info) {
            bytes[i] = static_cast<std::size_t>(info->width) * static_cast<std::size_t>(info->height) *
                       contactBytesPerPixel;
            largest = std::max(largest, bytes[i]);
        }
    }
    const std::size_t threads = static_cast<std::size_t>(std::max(1, jobs_.concurrency()));
    const std::size_t byBudget =
        largest > 0 ? std::max<std::size_t>(1, jobs_.budget().total() / largest) : threads;
    const std::size_t lanes = std::min({tasks.size(), threads, byBudget});

    std::vector<std::optional<image::ImageBuffer>> images(tasks.size());
    std::vector<std::optional<base::Error>> errors(tasks.size());
    std::atomic<bool> failed{false};
    std::mutex progressMutex;
    int done = 0;
    jobs_.parallelFor(ports::Priority::Batch, tasks.size(), lanes, [&](std::size_t i) {
        if (failed || cancel.cancelled()) {
            return;
        }
        const MemoryBudget::Lease lease = jobs_.budget().acquireUpTo(bytes[i]);
        auto r = renderSlot(*tasks[i].item, *tasks[i].slot, s, fmt, roll, *xf, cancel);
        if (!r) {
            errors[i] = r.error();
            failed = true;
            return;
        }
        images[i] = std::move(*r);
        if (progress) {
            const std::scoped_lock lock(progressMutex);
            progress(++done, static_cast<int>(items.size()));
        }
    });
    if (cancel.cancelled()) {
        return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
    }
    for (const auto& e : errors) { // erster Fehler in Feldreihenfolge
        if (e) {
            return std::unexpected(*e);
        }
    }
    std::vector<ports::RenderedPage> out;
    for (const layout::PageLayout& page : pages) {
        ports::RenderedPage rp;
        rp.layout = page;
        rp.outputProfile = *srgb;
        rp.dpi = s.dpi;
        out.push_back(std::move(rp));
    }
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        out[tasks[i].page].images.insert_or_assign(tasks[i].slot->frame, std::move(*images[i]));
    }
    return out;
}

base::Expected<image::ImageBuffer> ContactSheetService::renderSlot(
    const Item& item, const layout::ImageSlot& slot, const layout::ContactSheetSpec& s,
    const layout::FilmFormat& fmt, const model::Roll& roll,
    const std::shared_ptr<const ports::IColorTransform>& xf, const image::CancelToken& cancel) const
{
    // Pixel nach Slotgröße und Auflösung. Die Zielgröße gilt für das Quellbild vor Zuschnitt
    // und Drehung: lange Slotseite durch den kleineren Zuschnittanteil, auf beiden Achsen.
    const model::EditList edits = slotEdits(item, s, fmt, roll);
    double cropFraction = 1.0;
    if (const model::EditEntry* crop = edits.get(model::StepId(std::string(steps::id::crop)))) {
        if (const auto rect = crop->params.get<model::NormRect>("rect")) {
            cropFraction = std::clamp(std::min(rect->w, rect->h), 0.05, 1.0);
        }
    }
    const double longMm = std::max(slot.rectMm.w, slot.rectMm.h);
    const int px = static_cast<int>(std::ceil(longMm / 25.4 * s.dpi / cropFraction));
    pipeline::RenderRequest r;
    r.frame = item.source.id;
    r.path = item.source.path;
    r.list = std::make_shared<const model::EditList>(edits);
    r.film = roll.filmType;
    r.profile.mode = pipeline::RenderMode::Thumbnail;
    r.profile.target = {px, px};
    r.profile.priority = ports::Priority::Batch;
    r.profile.output.transform = xf; // im Backend datenparallel (D-17)
    r.cancel = cancel;
    auto converted = renderer_.render(r);
    if (release_) {
        release_(item.source.id);
    }
    return converted;
}

JobHandle ContactSheetService::exportPdf(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                                         layout::ContactSheetSpec s, layout::FilmFormat fmt,
                                         std::filesystem::path pdf)
{
    class PdfJob final : public Job
    {
    public:
        PdfJob(const ContactSheetService& svc, std::vector<Item> items,
               std::shared_ptr<const model::Roll> roll, layout::ContactSheetSpec s, layout::FilmFormat fmt,
               std::filesystem::path pdf, ports::IDocumentRenderer& doc)
            : svc_(svc)
            , items_(std::move(items))
            , roll_(std::move(roll))
            , s_(std::move(s))
            , fmt_(std::move(fmt))
            , pdf_(std::move(pdf))
            , doc_(doc)
        {}
        std::string name() const override { return "Kontaktabzug (PDF)"; }
        base::Expected<void> run(JobContext& c) override
        {
            auto pages = svc_.render(s_, items_, fmt_, roll_ ? *roll_ : model::Roll{}, c.cancel, c.progress);
            if (!pages) {
                return std::unexpected(pages.error());
            }
            return doc_.writePdf(*pages, pdf_);
        }

    private:
        const ContactSheetService& svc_;
        std::vector<Item> items_;
        std::shared_ptr<const model::Roll> roll_;
        layout::ContactSheetSpec s_;
        layout::FilmFormat fmt_;
        std::filesystem::path pdf_;
        ports::IDocumentRenderer& doc_;
    };
    return jobs_.submit(std::make_unique<PdfJob>(*this, std::move(items), std::move(roll), std::move(s),
                                                 std::move(fmt), std::move(pdf), document_),
                        ports::Priority::Batch);
}

JobHandle ContactSheetService::print(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                                     layout::ContactSheetSpec s, layout::FilmFormat fmt,
                                     ports::PrintSettings ps)
{
    class PrintJob final : public Job
    {
    public:
        PrintJob(const ContactSheetService& svc, std::vector<Item> items,
                 std::shared_ptr<const model::Roll> roll, layout::ContactSheetSpec s, layout::FilmFormat fmt,
                 ports::PrintSettings ps, ports::IDocumentRenderer& doc)
            : svc_(svc)
            , items_(std::move(items))
            , roll_(std::move(roll))
            , s_(std::move(s))
            , fmt_(std::move(fmt))
            , ps_(ps)
            , doc_(doc)
        {}
        std::string name() const override { return "Kontaktabzug (Druck)"; }
        base::Expected<void> run(JobContext& c) override
        {
            auto pages = svc_.render(s_, items_, fmt_, roll_ ? *roll_ : model::Roll{}, c.cancel, c.progress);
            if (!pages) {
                return std::unexpected(pages.error());
            }
            return doc_.print(*pages, ps_);
        }

    private:
        const ContactSheetService& svc_;
        std::vector<Item> items_;
        std::shared_ptr<const model::Roll> roll_;
        layout::ContactSheetSpec s_;
        layout::FilmFormat fmt_;
        ports::PrintSettings ps_;
        ports::IDocumentRenderer& doc_;
    };
    return jobs_.submit(std::make_unique<PrintJob>(*this, std::move(items), std::move(roll), std::move(s),
                                                   std::move(fmt), ps, document_),
                        ports::Priority::Batch);
}

} // namespace autoneg::application
