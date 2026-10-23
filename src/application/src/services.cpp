#include "autoneg/application/services.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <system_error>

namespace autoneg::application {

namespace {

ports::ParamKind kindOf(pipeline::ParamType t)
{
    switch (t) {
    case pipeline::ParamType::Double:
        return ports::ParamKind::Double;
    case pipeline::ParamType::Int:
        return ports::ParamKind::Int;
    case pipeline::ParamType::Bool:
        return ports::ParamKind::Bool;
    case pipeline::ParamType::String:
        return ports::ParamKind::String;
    case pipeline::ParamType::Vec3:
        return ports::ParamKind::Vec3;
    case pipeline::ParamType::Rect:
        return ports::ParamKind::Rect;
    case pipeline::ParamType::Curve:
        return ports::ParamKind::Curve;
    case pipeline::ParamType::Spots:
        return ports::ParamKind::Spots;
    }
    return ports::ParamKind::Double;
}

model::EditEntry automatic(const char* step, model::ParamSet p)
{
    model::EditEntry e;
    e.step = model::StepId(step);
    e.params = std::move(p);
    e.origin = model::Origin::Automatic;
    return e;
}

const char* filmName(model::FilmType f)
{
    switch (f) {
    case model::FilmType::ColorNegative:
        return "Farbnegativ";
    case model::FilmType::BwNegative:
        return "SW";
    case model::FilmType::Slide:
        return "Dia";
    }
    return "";
}

} // namespace

ports::StepCatalog stepCatalog(const pipeline::StepRegistry& registry)
{
    ports::StepCatalog out;
    for (const pipeline::IStep* s : registry.ordered()) {
        const pipeline::StepDescriptor& d = s->descriptor();
        ports::StepSchema schema{d.id.value(), d.rank, d.version, {}};
        for (const pipeline::ParamSpec& p : d.params) {
            schema.params.emplace_back(p.name, kindOf(p.type));
        }
        out.push_back(std::move(schema));
    }
    return out;
}

AnalysisFunctions
AnalysisFunctions::standard(ports::IVision& vision,
                            std::function<base::Expected<image::ImageBuffer>(const model::Frame&)> image)
{
    AnalysisFunctions f;
    f.image = std::move(image);
    f.detect = [&vision](const image::ImageBuffer& img, model::FilmType film) {
        return analysis::FrameDetector(vision).detect(img, film);
    };
    f.measureBase = [](const image::ImageBuffer& img, const analysis::RegionMap& m) {
        return analysis::BaseMeter{}.measure(img, m);
    };
    f.measureRect = [](const image::ImageBuffer& img, const model::NormRect& r) {
        return analysis::BaseMeter{}.measureRect(img, r);
    };
    return f;
}

model::EditList SetAutomaticEntriesCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    model::EditList out = l;
    for (model::EditEntry e : entries_) {
        const model::EditEntry* own = out.get(e.step);
        if (own != nullptr && (own->origin == model::Origin::Manual || own->origin == model::Origin::Copied ||
                               own->origin == model::Origin::Preset)) {
            continue; // Vorrang des Anwenders
        }
        if (const pipeline::IStep* s = c.registry.find(e.step)) {
            e.rank = s->descriptor().rank;
            e.version = s->descriptor().version;
        }
        out = out.with(std::move(e));
    }
    return out;
}

JobHandle AnalysisService::detectFrame(const model::Frame& f, std::shared_ptr<const model::Roll> roll)
{
    class DetectJob final : public Job
    {
    public:
        DetectJob(model::Frame f, std::shared_ptr<const model::Roll> roll, AnalysisFunctions fns,
                  CommandBus& bus, UiDispatcher ui)
            : f_(std::move(f))
            , roll_(std::move(roll))
            , fns_(std::move(fns))
            , bus_(bus)
            , ui_(std::move(ui))
        {}
        std::string name() const override { return "Bildfeld erkennen"; }
        base::Expected<void> run(JobContext& c) override
        {
            auto img = fns_.image(f_);
            if (!img) {
                return std::unexpected(img.error());
            }
            if (c.cancel.cancelled()) {
                return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
            }
            const model::FilmType film = roll_ ? roll_->filmType : model::FilmType::ColorNegative;
            auto d = fns_.detect(*img, film);
            if (!d) {
                return std::unexpected(d.error()); // Oberfläche meldet, Zuschnitt bleibt (LF-GEO-05)
            }
            std::vector<model::EditEntry> entries;
            entries.push_back(automatic("autoneg.crop", model::ParamSet{}.with("rect", d->frame)));
            if (std::abs(d->angleDeg) >= 0.01) {
                entries.push_back(
                    automatic("autoneg.rotation", model::ParamSet{}.with("angle", d->angleDeg)));
            }
            if (film != model::FilmType::Slide) {
                if (auto b = fns_.measureBase(*img, d->regions)) {
                    entries.push_back(
                        automatic("autoneg.filmbase", model::ParamSet{}.with("base", b->transmission)));
                }
            }
            c.progress(1, 1);
            // Ergebnis im UI-Thread über den CommandBus eintragen
            ui_([&bus = bus_, id = f_.id, roll = roll_, entries = std::move(entries)] {
                const std::vector<model::FrameId> target{id};
                bus.execute(SetAutomaticEntriesCommand(entries), target, roll ? *roll : model::Roll{});
            });
            return {};
        }

    private:
        model::Frame f_;
        std::shared_ptr<const model::Roll> roll_;
        AnalysisFunctions fns_;
        CommandBus& bus_;
        UiDispatcher ui_;
    };
    return jobs_.submit(std::make_unique<DetectJob>(f, std::move(roll), fns_, bus_, ui_),
                        ports::Priority::Background);
}

JobHandle AnalysisService::measureBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                       model::NormRect rect)
{
    class MeasureJob final : public Job
    {
    public:
        MeasureJob(model::Frame f, std::shared_ptr<const model::Roll> roll, model::NormRect rect,
                   AnalysisFunctions fns, CommandBus& bus, UiDispatcher ui)
            : f_(std::move(f))
            , roll_(std::move(roll))
            , rect_(rect)
            , fns_(std::move(fns))
            , bus_(bus)
            , ui_(std::move(ui))
        {}
        std::string name() const override { return "Basis messen"; }
        base::Expected<void> run(JobContext& c) override
        {
            if (!fns_.measureRect) {
                return base::makeError(base::ErrorCode::Unsupported, "Basismessung nicht verfügbar");
            }
            auto img = fns_.image(f_);
            if (!img) {
                return std::unexpected(img.error());
            }
            auto b = fns_.measureRect(*img, rect_);
            if (!b) {
                return std::unexpected(b.error());
            }
            c.progress(1, 1);
            model::EditEntry e;
            e.step = model::StepId("autoneg.filmbase");
            e.params = model::ParamSet{}.with("base", b->transmission).with("source", std::string("manual"));
            ui_([&bus = bus_, id = f_.id, roll = roll_, e = std::move(e)] {
                const std::vector<model::FrameId> target{id};
                bus.execute(SetEntryCommand(e), target, roll ? *roll : model::Roll{});
            });
            return {};
        }

    private:
        model::Frame f_;
        std::shared_ptr<const model::Roll> roll_;
        model::NormRect rect_;
        AnalysisFunctions fns_;
        CommandBus& bus_;
        UiDispatcher ui_;
    };
    return jobs_.submit(std::make_unique<MeasureJob>(f, std::move(roll), rect, fns_, bus_, ui_),
                        ports::Priority::Interactive);
}

std::string exportName(const std::string& pattern, const model::Roll& roll, const model::Frame& frame)
{
    std::string out;
    char num[16];
    std::snprintf(num, sizeof num, "%03d", frame.frameNo);
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern[i] == '{') {
            const auto end = pattern.find('}', i);
            if (end != std::string::npos) {
                const std::string key = pattern.substr(i + 1, end - i - 1);
                if (key == "roll") {
                    out += roll.name.empty() ? roll.folder.filename().string() : roll.name;
                } else if (key == "frame") {
                    out += num;
                } else if (key == "name") {
                    out += frame.path.stem().string();
                } else if (key == "film") {
                    out += filmName(roll.filmType);
                } else {
                    out += pattern.substr(i, end - i + 1); // unbekannt: unverändert
                }
                i = end + 1;
                continue;
            }
        }
        out += pattern[i++];
    }
    for (char& c : out) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    return out;
}

base::Expected<std::filesystem::path> ExportService::target(const ExportSettings& s, const model::Roll& roll,
                                                            const model::Frame& frame)
{
    const char* ext = s.write.format == ports::FileFormat::Tiff     ? ".tif"
                      : s.write.format == ports::FileFormat::Jpeg   ? ".jpg"
                      : s.write.format == ports::FileFormat::Png    ? ".png"
                      : s.write.format == ports::FileFormat::JpegXl ? ".jxl"
                                                                    : ".dng";
    std::filesystem::path p = s.folder / (exportName(s.pattern, roll, frame) + ext);
    std::error_code ec;
    if (s.overwrite == OverwritePolicy::Never && std::filesystem::exists(p, ec)) {
        return base::makeError(base::ErrorCode::Conflict, p.filename().string() + " existiert bereits");
    }
    return p;
}

JobHandle ExportService::exportFrames(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                                      ExportSettings s)
{
    class ExportJob final : public Job
    {
    public:
        ExportJob(std::vector<Item> items, std::shared_ptr<const model::Roll> roll, ExportSettings s,
                  JobScheduler& jobs, IRenderer& renderer, ports::IColorEngine& color,
                  ports::IImageWriter& writer, Release release)
            : items_(std::move(items))
            , roll_(std::move(roll))
            , s_(std::move(s))
            , jobs_(jobs)
            , renderer_(renderer)
            , color_(color)
            , writer_(writer)
            , release_(std::move(release))
        {}
        std::string name() const override { return "Export"; }
        base::Expected<void> run(JobContext& c) override
        {
            const model::Roll roll = roll_ ? *roll_ : model::Roll{};
            auto working = color_.profile(ports::ProfileId::WorkingRec2020Linear);
            auto dst = color_.profile(s_.grayscale ? ports::ProfileId::GrayGamma22 : s_.profile);
            if (!working || !dst) {
                return base::makeError(base::ErrorCode::Internal, "Farbprofile nicht verfügbar");
            }
            auto xf = color_.transform(*working, *dst, ports::Intent::RelativeColorimetric);
            if (!xf) {
                return std::unexpected(xf.error());
            }
            // Zieldateien vorab: ein Konflikt bricht ab, bevor ein Bild geschrieben ist
            std::vector<std::filesystem::path> paths;
            for (const Item& item : items_) {
                auto path = target(s_, roll, item.frame);
                if (!path) {
                    return std::unexpected(path.error());
                }
                paths.push_back(std::move(*path));
            }
            // Mehrere Bilder gleichzeitig (Ebene 3, NF-PERF-11): so viele, wie Threads und
            // Speicherbudget zulassen; Dekodieren und Schreiben eines Bildes überlappen dabei
            // mit dem Rechnen der anderen
            std::vector<std::size_t> bytes(items_.size(), 0);
            std::size_t largest = 0;
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (auto info = renderer_.info(items_[i].frame.id, items_[i].frame.path); info) {
                    bytes[i] = static_cast<std::size_t>(info->width) *
                               static_cast<std::size_t>(info->height) * exportBytesPerPixel;
                    largest = std::max(largest, bytes[i]);
                }
            }
            const std::size_t threads = static_cast<std::size_t>(std::max(1, jobs_.concurrency()));
            const std::size_t byBudget =
                largest > 0 ? std::max<std::size_t>(1, jobs_.budget().total() / largest) : threads;
            const std::size_t lanes = std::min({items_.size(), threads, byBudget});

            std::vector<std::optional<base::Error>> errors(items_.size());
            std::atomic<bool> failed{false};
            std::mutex progressMutex;
            int done = 0;
            jobs_.parallelFor(c.priority, items_.size(), lanes, [&](std::size_t i) {
                if (failed || c.cancel.cancelled()) {
                    return;
                }
                const MemoryBudget::Lease lease = jobs_.budget().acquireUpTo(bytes[i]);
                auto r = exportOne(items_[i], paths[i], roll, *xf, *dst, c.cancel);
                if (!r) {
                    errors[i] = r.error();
                    failed = true;
                    return;
                }
                const std::scoped_lock lock(progressMutex);
                c.progress(++done, static_cast<int>(items_.size()));
            });
            if (c.cancel.cancelled()) {
                return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
            }
            for (const auto& e : errors) { // erster Fehler in Bildreihenfolge
                if (e) {
                    return std::unexpected(*e);
                }
            }
            return {};
        }

    private:
        base::Expected<void> exportOne(const Item& item, const std::filesystem::path& path,
                                       const model::Roll& roll,
                                       const std::shared_ptr<const ports::IColorTransform>& xf,
                                       const ports::IccProfile& icc, const image::CancelToken& cancel)
        {
            pipeline::RenderRequest r;
            r.frame = item.frame.id;
            r.path = item.frame.path;
            r.list = std::make_shared<const model::EditList>(item.edits);
            r.film = roll.filmType;
            r.profile.mode = pipeline::RenderMode::Export;
            if (s_.longEdge > 0) {
                r.profile.target = {s_.longEdge, s_.longEdge}; // lange Kante, Seitenverhältnis bleibt
            }
            r.profile.output.transform = xf; // im Backend datenparallel (D-17); Grau einkanalig (LF-EXP-09)
            if (s_.write.bitDepth == 8 || s_.write.bitDepth == 16) {
                r.profile.output.bits = s_.write.bitDepth; // ganzzahlig aus dem Backend (K-M10-03)
            }
            r.cancel = cancel;
            auto out = renderer_.render(r);
            if (release_) {
                release_(item.frame.id); // Quelle in voller Auflösung nicht behalten (NF-PERF-07)
            }
            if (!out) {
                return std::unexpected(out.error());
            }
            ports::WriteOptions o = s_.write;
            o.icc = icc;
            return writer_.write(path, *out, o);
        }

        std::vector<Item> items_;
        std::shared_ptr<const model::Roll> roll_;
        ExportSettings s_;
        JobScheduler& jobs_;
        IRenderer& renderer_;
        ports::IColorEngine& color_;
        ports::IImageWriter& writer_;
        Release release_;
    };
    return jobs_.submit(std::make_unique<ExportJob>(std::move(items), std::move(roll), std::move(s), jobs_,
                                                    renderer_, color_, writer_, release_),
                        ports::Priority::Batch);
}

base::Expected<std::unique_ptr<ports::IInferenceModel>>
NoInferenceRuntime::load(const std::filesystem::path& model)
{
    return base::makeError(base::ErrorCode::Unsupported,
                           "KI-Modelle (" + model.filename().string() + ") folgen mit Version 1.1");
}

base::Expected<model::JsonValue> NoFilmProfiles::load(const std::string& id)
{
    return base::makeError(base::ErrorCode::NotFound,
                           "Filmprofil " + id + ": Filmprofile folgen mit Version 1.1");
}

} // namespace autoneg::application
