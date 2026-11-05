#include "application/services.hpp"

#include "analysis/tone.hpp"
#include "steps/other_steps.hpp"
#include "steps/step_ids.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <system_error>
#include <vector>

namespace autoneg::application {

namespace id = steps::id;
namespace key = steps::key;

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

model::EditEntry automatic(std::string_view step, model::ParamSet p)
{
    model::EditEntry e;
    e.step = steps::stepId(step);
    e.params = std::move(p);
    e.origin = model::Origin::Automatic;
    return e;
}

/// Tonwertgrenzen eines Negativs (LF-KON-05): Dichte D = −log10(T / Basis) je Kanal im
/// Bildfeld (Rand 3 % weggelassen), davon 0,1 % und 99,9 %. Jeder Kanal bekommt so seinen
/// eigenen Bereich; das gleicht die Farbschichten des Negativs aus.
std::optional<analysis::DensityRange> negativeLevels(const image::ImageBuffer& img,
                                                     const model::NormRect& frame, const base::Vec3& base)
{
    const auto& spec = img.spec();
    if (spec.channels < 3 || img.host().empty()) {
        return std::nullopt;
    }
    const int x0 = static_cast<int>((frame.x + 0.03 * frame.w) * spec.width);
    const int x1 = static_cast<int>((frame.x + 0.97 * frame.w) * spec.width);
    const int y0 = static_cast<int>((frame.y + 0.03 * frame.h) * spec.height);
    const int y1 = static_cast<int>((frame.y + 0.97 * frame.h) * spec.height);
    const std::array<double, 3> b{base.x, base.y, base.z};
    const auto v = img.host();
    const auto ch = static_cast<std::size_t>(spec.channels);
    std::vector<float> density;
    for (int y = std::max(y0, 0); y < std::min(y1, spec.height); ++y) {
        for (int x = std::max(x0, 0); x < std::min(x1, spec.width); ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(spec.width) +
                                   static_cast<std::size_t>(x)) *
                                  ch;
            for (std::size_t c = 0; c < 3; ++c) {
                const double t = std::max(static_cast<double>(v[i + c]) / std::max(b[c], 1e-6), 1e-4);
                density.push_back(static_cast<float>(-std::log10(t)));
            }
        }
    }
    const int pixels = static_cast<int>(density.size() / 3);
    if (pixels < 100) {
        return std::nullopt;
    }
    auto d = image::ImageBuffer::fromFloats(
        image::ImageSpec{pixels, 1, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, density);
    if (!d) {
        return std::nullopt;
    }
    return analysis::AutoLevels{}.compute(*d, 0.002);
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
    case model::FilmType::Unconverted:
        return "Scan";
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
    f.detect = [&vision](const image::ImageBuffer& img, model::FilmType film, const analysis::FrameHint& h) {
        return analysis::FrameDetector(vision).detect(img, film, h);
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

AnalysisCache::Entry& AnalysisCache::entry(const model::FrameId& f)
{
    const auto it = std::ranges::find(entries_, f, &Entry::frame);
    if (it != entries_.end()) {
        entries_.splice(entries_.begin(), entries_, it);
    } else {
        entries_.emplace_front();
        entries_.front().frame = f;
        if (entries_.size() > capacity_) {
            entries_.pop_back();
        }
    }
    return entries_.front();
}

std::shared_ptr<const image::ImageBuffer> AnalysisCache::image(const model::FrameId& f)
{
    const std::scoped_lock lock(mtx_);
    return entry(f).image;
}

void AnalysisCache::putImage(const model::FrameId& f, std::shared_ptr<const image::ImageBuffer> img)
{
    const std::scoped_lock lock(mtx_);
    entry(f).image = std::move(img);
}

std::optional<analysis::FrameDetection>
AnalysisCache::detection(const model::FrameId& f, model::FilmType film, const analysis::FrameHint& hint)
{
    const std::scoped_lock lock(mtx_);
    const Entry& e = entry(f);
    if (e.detection && e.film == film && e.hint == hint) {
        return e.detection;
    }
    return std::nullopt;
}

void AnalysisCache::putDetection(const model::FrameId& f, model::FilmType film,
                                 const analysis::FrameHint& hint, analysis::FrameDetection d)
{
    const std::scoped_lock lock(mtx_);
    Entry& e = entry(f);
    e.detection = std::move(d);
    e.film = film;
    e.hint = hint;
}

namespace {

/// Analysebild aus dem Zwischenspeicher, sonst dekodiert und dort abgelegt.
base::Expected<std::shared_ptr<const image::ImageBuffer>>
analysisImage(AnalysisCache& cache, const AnalysisFunctions& fns, const model::Frame& f)
{
    if (auto img = cache.image(f.id)) {
        return img;
    }
    auto decoded = fns.image(f);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    auto img = std::make_shared<const image::ImageBuffer>(std::move(*decoded));
    cache.putImage(f.id, img);
    return img;
}

} // namespace

JobHandle AnalysisService::autoFrame(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                     model::FilmType film, model::EditList current)
{
    return run(f, std::move(roll), film, Frame, std::move(current));
}

JobHandle AnalysisService::autoBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                    model::FilmType film, model::EditList current)
{
    return run(f, std::move(roll), film, Base, std::move(current));
}

JobHandle AnalysisService::autoLevels(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                      model::FilmType film, model::EditList current)
{
    return run(f, std::move(roll), film, Levels, std::move(current));
}

JobHandle AnalysisService::convert(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                   model::FilmType film, model::EditList current)
{
    return run(f, std::move(roll), film, Levels | Convert, std::move(current));
}

JobHandle AnalysisService::autoConvert(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                       model::FilmType film, model::EditList current)
{
    return run(f, std::move(roll), film, All, std::move(current));
}

JobHandle AnalysisService::redetect(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                    model::FilmType film, model::EditList current,
                                    const std::set<model::StepId>& steps)
{
    unsigned parts = 0;
    for (const model::StepId& s : steps) {
        if (s.value() == id::crop || s.value() == id::rotation) {
            parts |= Frame;
        } else if (s.value() == id::filmbase) {
            parts |= Base;
        } else if (s.value() == id::densityrange) {
            parts |= Levels;
        }
    }
    return run(f, std::move(roll), film, parts, std::move(current));
}

JobHandle AnalysisService::run(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                               model::FilmType film, unsigned parts, model::EditList current)
{
    class DetectJob final : public Job
    {
    public:
        DetectJob(model::Frame f, std::shared_ptr<const model::Roll> roll, model::FilmType film,
                  unsigned parts, model::EditList current, analysis::FrameHint hint, Report report,
                  AnalysisFunctions fns, std::shared_ptr<AnalysisCache> cache, CommandBus& bus,
                  UiDispatcher ui)
            : f_(std::move(f))
            , hint_(hint)
            , report_(std::move(report))
            , roll_(std::move(roll))
            , film_(film)
            , parts_(parts)
            , current_(std::move(current))
            , fns_(std::move(fns))
            , cache_(std::move(cache))
            , bus_(bus)
            , ui_(std::move(ui))
        {}
        std::string name() const override { return "Bildfeld erkennen"; }
        base::Expected<void> run(JobContext& c) override
        {
            const model::FilmType film = film_;
            if (film == model::FilmType::Unconverted) {
                return base::makeError(base::ErrorCode::InvalidArgument,
                                       "Filmtyp nicht festgelegt (Farbnegativ, S/W-Negativ oder Dia wählen)");
            }
            auto shared = analysisImage(*cache_, fns_, f_);
            if (!shared) {
                return std::unexpected(shared.error());
            }
            const image::ImageBuffer* img = shared->get();
            if (c.cancel.cancelled()) {
                return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
            }
            auto d = cache_->detection(f_.id, film, hint_);
            if (!d) {
                auto found = fns_.detect(*img, film, hint_);
                if (!found) {
                    return std::unexpected(found.error()); // Oberfläche meldet, Zuschnitt bleibt (LF-GEO-05)
                }
                cache_->putDetection(f_.id, film, hint_, *found);
                d = std::move(*found);
            }
            std::vector<model::EditEntry> entries;
            std::vector<std::string> hints;
            // Zuschnitt 1 % innerhalb der Bildfeldkanten: kein Filmrand und kein Nachbarbild im Bild
            const model::NormRect& r = d->frame;
            const model::NormRect crop{r.x + 0.01 * r.w, r.y + 0.01 * r.h, 0.98 * r.w, 0.98 * r.h};
            if ((parts_ & Frame) != 0) {
                entries.push_back(automatic(id::crop, model::ParamSet{}.with(key::rect, crop)));
                if (std::abs(d->angleDeg) >= 0.01) {
                    entries.push_back(automatic(
                        id::rotation,
                        model::ParamSet{}.with(key::angle, d->angleDeg).with(key::autoFill, false)));
                }
            }
            if (film != model::FilmType::Slide && (parts_ & (Base | Levels)) != 0) {
                // Basis: neu messen oder die vorhandene des Bilds
                std::optional<base::Vec3> base;
                const model::EditEntry* own = current_.get(steps::stepId(id::filmbase));
                if ((parts_ & Base) == 0 && own != nullptr) {
                    if (auto v = own->params.get<base::Vec3>(key::base)) {
                        base = *v;
                    }
                }
                if (!base) {
                    auto b = fns_.measureBase(*img, d->regions);
                    if (!b) {
                        // ohne Filmrand: Basis aus den hellsten Stellen des Negativs (LF-BAS-05 c)
                        b = analysis::BaseEstimator{}.estimate(*img);
                        hints.emplace_back("Kein Filmrand gefunden, Basis aus dem Bild geschätzt");
                    }
                    if (b) {
                        base = b->transmission;
                        if (b->rejected > 0.5) {
                            hints.emplace_back("Filmrand ungleichmäßig (Fehlfarben), Basis prüfen");
                        }
                        if ((parts_ & Base) != 0) {
                            model::ParamSet p = model::ParamSet{}.with(key::base, b->transmission);
                            if (!d->regions.rebate().empty()) {
                                p = p.with(key::area, d->regions.rebate().front()); // Messbereich
                            }
                            entries.push_back(automatic(id::filmbase, std::move(p)));
                        }
                    }
                }
                // Tonwerte im Bildfeld: vorhandener Zuschnitt des Bilds, sonst der erkannte
                model::NormRect area = crop;
                if (const model::EditEntry* c2 = current_.get(steps::stepId(id::crop));
                    c2 != nullptr && (parts_ & Frame) == 0) {
                    area = c2->params.get<model::NormRect>(key::rect).value_or(crop);
                }
                if ((parts_ & Levels) != 0 && base) {
                    if (auto levels = negativeLevels(*img, area, *base)) {
                        entries.push_back(automatic(
                            id::densityrange,
                            model::ParamSet{}.with(key::dMin, levels->dMin).with(key::dMax, levels->dMax)));
                    }
                }
            }
            if ((parts_ & Convert) != 0) {
                // "auto": aus der Autokonvertierung, nicht vom Schalter Konvertieren
                entries.push_back(automatic(
                    id::convert, model::ParamSet{}.with(key::on, true).with(key::automatic, parts_ == All)));
            }
            c.progress(1, 1);
            // Ergebnis im UI-Thread über den CommandBus eintragen
            ui_([&bus = bus_, id = f_.id, roll = roll_, entries = std::move(entries), report = report_,
                 r = FrameReport{d->quality, d->notes, std::move(hints)}] {
                const std::vector<model::FrameId> target{id};
                bus.execute(SetAutomaticEntriesCommand(entries), target, roll ? *roll : model::Roll{});
                if (report) {
                    report(id, r);
                }
            });
            return {};
        }

    private:
        model::Frame f_;
        analysis::FrameHint hint_;
        Report report_;
        std::shared_ptr<const model::Roll> roll_;
        model::FilmType film_;
        unsigned parts_;
        model::EditList current_;
        AnalysisFunctions fns_;
        std::shared_ptr<AnalysisCache> cache_;
        CommandBus& bus_;
        UiDispatcher ui_;
    };
    return jobs_.submit(std::make_unique<DetectJob>(f, std::move(roll), film, parts, std::move(current),
                                                    hint_, report_, fns_, cache_, bus_, ui_),
                        ports::Priority::Batch);
}

JobHandle AnalysisService::measureBase(const model::Frame& f, std::shared_ptr<const model::Roll> roll,
                                       model::NormRect rect)
{
    class MeasureJob final : public Job
    {
    public:
        MeasureJob(model::Frame f, std::shared_ptr<const model::Roll> roll, model::NormRect rect,
                   AnalysisFunctions fns, std::shared_ptr<AnalysisCache> cache, CommandBus& bus,
                   UiDispatcher ui)
            : f_(std::move(f))
            , roll_(std::move(roll))
            , rect_(rect)
            , fns_(std::move(fns))
            , cache_(std::move(cache))
            , bus_(bus)
            , ui_(std::move(ui))
        {}
        std::string name() const override { return "Basis messen"; }
        base::Expected<void> run(JobContext& c) override
        {
            if (!fns_.measureRect) {
                return base::makeError(base::ErrorCode::Unsupported, "Basismessung nicht verfügbar");
            }
            auto img = analysisImage(*cache_, fns_, f_);
            if (!img) {
                return std::unexpected(img.error());
            }
            auto b = fns_.measureRect(**img, rect_);
            if (!b) {
                return std::unexpected(b.error());
            }
            c.progress(1, 1);
            model::EditEntry e;
            e.step = steps::stepId(id::filmbase);
            e.params = model::ParamSet{}
                           .with(key::base, b->transmission)
                           .with(key::source, std::string("manual"))
                           .with(key::area, rect_); // Messbereich
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
        std::shared_ptr<AnalysisCache> cache_;
        CommandBus& bus_;
        UiDispatcher ui_;
    };
    return jobs_.submit(std::make_unique<MeasureJob>(f, std::move(roll), rect, fns_, cache_, bus_, ui_),
                        ports::Priority::Interactive);
}

std::string exportName(const std::string& pattern, const model::Roll& roll, const model::Frame& frame,
                       std::optional<model::FilmType> film)
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
                    out += filmName(film.value_or(roll.filmType));
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
    // Windows: keine Punkte/Leerzeichen am Ende, keine Gerätenamen
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.find_first_not_of(" .") == std::string::npos) {
        return frame.path.stem().string();
    }
    std::string upper;
    for (char c : out) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const bool device = upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
                        (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
                         upper[3] >= '1' && upper[3] <= '9');
    if (device) {
        out += '_';
    }
    return out;
}

base::Expected<std::filesystem::path> ExportService::target(const ExportSettings& s, const model::Roll& roll,
                                                            const model::Frame& frame,
                                                            std::optional<model::FilmType> film)
{
    const char* ext = s.write.format == ports::FileFormat::Tiff     ? ".tif"
                      : s.write.format == ports::FileFormat::Jpeg   ? ".jpg"
                      : s.write.format == ports::FileFormat::Png    ? ".png"
                      : s.write.format == ports::FileFormat::JpegXl ? ".jxl"
                                                                    : ".dng";
    std::filesystem::path p = s.folder / (exportName(s.pattern, roll, frame, film) + ext);
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
                auto path =
                    target(s_, roll, item.frame, steps::FilmModeStep::effective(item.edits, roll.filmType));
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
