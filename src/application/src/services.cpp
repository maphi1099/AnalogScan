#include "autoneg/application/services.hpp"

#include <cmath>
#include <cstdio>
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
                  IRenderer& renderer, ports::IColorEngine& color, ports::IImageWriter& writer)
            : items_(std::move(items))
            , roll_(std::move(roll))
            , s_(std::move(s))
            , renderer_(renderer)
            , color_(color)
            , writer_(writer)
        {}
        std::string name() const override { return "Export"; }
        base::Expected<void> run(JobContext& c) override
        {
            const model::Roll roll = roll_ ? *roll_ : model::Roll{};
            auto working = color_.profile(ports::ProfileId::WorkingRec2020Linear);
            auto dst = color_.profile(s_.profile);
            if (!working || !dst) {
                return base::makeError(base::ErrorCode::Internal, "Farbprofile nicht verfügbar");
            }
            auto xf = color_.transform(*working, *dst, ports::Intent::RelativeColorimetric);
            if (!xf) {
                return std::unexpected(xf.error());
            }
            int done = 0;
            for (const Item& item : items_) {
                if (c.cancel.cancelled()) {
                    return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
                }
                auto path = target(s_, roll, item.frame);
                if (!path) {
                    return std::unexpected(path.error());
                }
                pipeline::RenderRequest r;
                r.frame = item.frame.id;
                r.path = item.frame.path;
                r.list = std::make_shared<const model::EditList>(item.edits);
                r.film = roll.filmType;
                r.profile.mode = pipeline::RenderMode::Export;
                r.cancel = c.cancel;
                auto img = renderer_.render(r);
                if (!img) {
                    return std::unexpected(img.error());
                }
                auto out = (*xf)->apply(*img);
                if (!out) {
                    return std::unexpected(out.error());
                }
                ports::WriteOptions o = s_.write;
                o.icc = *dst;
                if (auto ok = writer_.write(*path, *out, o); !ok) {
                    return ok;
                }
                c.progress(++done, static_cast<int>(items_.size()));
            }
            return {};
        }

    private:
        std::vector<Item> items_;
        std::shared_ptr<const model::Roll> roll_;
        ExportSettings s_;
        IRenderer& renderer_;
        ports::IColorEngine& color_;
        ports::IImageWriter& writer_;
    };
    return jobs_.submit(std::make_unique<ExportJob>(std::move(items), std::move(roll), std::move(s),
                                                    renderer_, color_, writer_),
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
