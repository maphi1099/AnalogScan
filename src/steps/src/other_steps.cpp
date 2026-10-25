#include "autoneg/steps/other_steps.hpp"

#include "describe.hpp"

#include "autoneg/steps/step_ids.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>

namespace autoneg::steps {

namespace {
using pipeline::Phase;
} // namespace

// --- Filmtyp ---------------------------------------------------------------------------------

FilmModeStep::FilmModeStep()
    : d_(describe(id::filmmode, rank::filmmode, "Filmtyp", Phase::Conversion, {stringParam("type", "color")},
                  pipeline::FilmTypeMask::any()))
{}

base::Expected<void> FilmModeStep::validate(const model::ParamSet& p) const
{
    const std::string t = param(p, "type", std::string("color"));
    if (t != "color" && t != "bw" && t != "slide") {
        return base::makeError(base::ErrorCode::InvalidArgument, "Unbekannter Filmtyp '" + t + "'");
    }
    return pipeline::validateParams(d_, p);
}

void FilmModeStep::configure(pipeline::StepContext& ctx, const model::ParamSet& p) const
{
    ctx.film = fromString(param(p, "type", std::string("color")));
}

model::FilmType FilmModeStep::fromString(std::string_view t) noexcept
{
    return t == "bw"      ? model::FilmType::BwNegative
           : t == "slide" ? model::FilmType::Slide
                          : model::FilmType::ColorNegative;
}

model::FilmType FilmModeStep::effective(const model::EditList& l, model::FilmType rollDefault)
{
    const model::EditEntry* e = l.get(model::StepId(std::string(id::filmmode)));
    if (e == nullptr || !e->enabled) {
        return rollDefault;
    }
    return fromString(e->params.get<std::string>("type").value_or("color"));
}

const char* FilmModeStep::toString(model::FilmType t) noexcept
{
    switch (t) {
    case model::FilmType::ColorNegative:
        return "color";
    case model::FilmType::BwNegative:
        return "bw";
    case model::FilmType::Slide:
        return "slide";
    case model::FilmType::Unconverted:
        return "raw";
    }
    return "color";
}

// --- Geometrie -------------------------------------------------------------------------------

OrientationStep::OrientationStep()
    : DescribedGeometric(
          describe(id::orientation, rank::orientation, "Orientierung", Phase::Geometry,
                   {intParam("quarterTurns", 0, 0, 3), boolParam("flipH", false), boolParam("flipV", false)},
                   pipeline::FilmTypeMask::any()))
{}

void OrientationStep::emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
                           const pipeline::StepContext& /*ctx*/) const
{
    b.orient(param(p, "quarterTurns", 0), param(p, "flipH", false), param(p, "flipV", false));
}

RotationStep::RotationStep()
    : DescribedGeometric(describe(id::rotation, rank::rotation, "Feindrehung", Phase::Geometry,
                                  {doubleParam("angle", 0.0, -45.0, 45.0, "°"), boolParam("autoFill", true)},
                                  pipeline::FilmTypeMask::any()))
{}

bool RotationStep::isNeutral(const model::ParamSet& p) const
{
    return param(p, "angle", 0.0) == 0.0;
}

double RotationStep::fillScale(double width, double height, double degrees)
{
    const double t = std::abs(degrees) * std::numbers::pi / 180.0;
    const double c = std::cos(t);
    const double s = std::sin(t);
    return std::min(width / (width * c + height * s), height / (width * s + height * c));
}

void RotationStep::emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
                        const pipeline::StepContext& /*ctx*/) const
{
    const double angle = param(p, "angle", 0.0);
    b.rotate(angle);
    if (param(p, "autoFill", true)) {
        const base::Point2 c = b.canvas();
        const double s = fillScale(c.x, c.y, angle);
        b.crop({(1.0 - s) / 2.0, (1.0 - s) / 2.0, s, s});
    }
}

CropStep::CropStep()
    : DescribedGeometric(
          describe(id::crop, rank::crop, "Zuschnitt", Phase::Geometry,
                   {rectParam("rect"), stringParam("aspect", ""), doubleParam("inset", 0.0, 0.0, 0.1, "")},
                   pipeline::FilmTypeMask::any()))
{}

bool CropStep::isNeutral(const model::ParamSet& p) const
{
    return param(p, "rect", model::NormRect::full()) == model::NormRect::full() &&
           param(p, "inset", 0.0) == 0.0;
}

model::NormRect CropStep::applyInset(const model::NormRect& rect, double inset, base::Point2 canvas)
{
    const double d = inset * std::min(rect.w * canvas.x, rect.h * canvas.y); // Pixel je Kante
    const double dx = d / canvas.x;
    const double dy = d / canvas.y;
    return {rect.x + dx, rect.y + dy, std::max(rect.w - 2 * dx, 1e-6), std::max(rect.h - 2 * dy, 1e-6)};
}

model::NormRect CropStep::fitAspect(const model::NormRect& rect, double aw, double ah, base::Size canvas)
{
    const double w = rect.w * canvas.width;
    const double h = rect.h * canvas.height;
    const double target = aw / ah;
    double nw = w;
    double nh = h;
    if (w / h > target) {
        nw = h * target;
    } else {
        nh = w / target;
    }
    const double nx = rect.x * canvas.width + (w - nw) / 2.0;
    const double ny = rect.y * canvas.height + (h - nh) / 2.0;
    return {nx / canvas.width, ny / canvas.height, nw / canvas.width, nh / canvas.height};
}

void CropStep::emit(pipeline::WarpBuilder& b, const model::ParamSet& p,
                    const pipeline::StepContext& /*ctx*/) const
{
    const model::NormRect rect = param(p, "rect", model::NormRect::full());
    b.crop(applyInset(rect, param(p, "inset", 0.0), b.canvas()));
}

// --- Leuchtfläche ------------------------------------------------------------------------------

void FlatFieldCache::add(const std::string& id, image::ImageBuffer blank)
{
    const std::unique_lock lock(mtx_);
    blanks_[id] = std::move(blank);
    std::erase_if(gains_, [&](const auto& kv) { return kv.first.first == id; });
}

bool FlatFieldCache::contains(const std::string& id) const
{
    const std::shared_lock lock(mtx_);
    return blanks_.contains(id);
}

base::Expected<std::shared_ptr<const image::ImageBuffer>>
FlatFieldCache::gainMap(const std::string& id, base::Size size, ports::IComputeBackend& backend,
                        const ports::KernelContext& c)
{
    const auto key = std::make_pair(id, std::make_pair(size.width, size.height));
    image::ImageBuffer blank;
    {
        const std::shared_lock lock(mtx_);
        if (const auto it = gains_.find(key); it != gains_.end()) {
            return it->second;
        }
        const auto b = blanks_.find(id);
        if (b == blanks_.end()) {
            return base::makeError(base::ErrorCode::NotFound,
                                   "Leuchtflächen-Kalibrierung '" + id + "' fehlt");
        }
        blank = b->second;
    }
    // auf Zielgröße bringen und glätten (Staub und Rauschen der Leeraufnahme entfernen)
    auto scaled = blank.spec().width == size.width && blank.spec().height == size.height
                      ? base::Expected<image::ImageBuffer>(blank)
                      : backend.resample(c, blank, size);
    if (!scaled) {
        return std::unexpected(scaled.error());
    }
    auto smooth = backend.gaussian(c, *scaled, static_cast<float>(0.01 * size.width));
    if (!smooth) {
        return std::unexpected(smooth.error());
    }
    const image::ImageSpec& s = smooth->spec();
    const auto src = smooth->host();
    // Mittel aller Kanäle im mittleren Zehntel
    double sum = 0.0;
    std::size_t count = 0;
    const int x0 = s.width * 45 / 100;
    const int x1 = std::max(x0 + 1, s.width * 55 / 100);
    const int y0 = s.height * 45 / 100;
    const int y1 = std::max(y0 + 1, s.height * 55 / 100);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            for (int k = 0; k < s.channels; ++k) {
                sum += src[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) +
                            static_cast<std::size_t>(x)) *
                               static_cast<std::size_t>(s.channels) +
                           static_cast<std::size_t>(k)];
                ++count;
            }
        }
    }
    const float m = static_cast<float>(sum / static_cast<double>(std::max<std::size_t>(count, 1)));
    auto gain = image::ImageBuffer::allocateHost(s, [&](std::span<std::byte> bytes) {
        auto* dst =
            reinterpret_cast<float*>(bytes.data()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        for (std::size_t i = 0; i < src.size(); ++i) {
            dst[i] = m / std::max(src[i], 1e-6F);
        }
    });
    if (!gain) {
        return std::unexpected(gain.error());
    }
    auto shared = std::make_shared<const image::ImageBuffer>(std::move(*gain));
    const std::unique_lock lock(mtx_);
    gains_[key] = shared;
    return shared;
}

FlatFieldStep::FlatFieldStep(FlatFieldCache& cache)
    : DescribedRegion(describe(id::flatfield, rank::flatfield, "Leuchtflächenkorrektur", Phase::Source,
                               {stringParam("profile", "")}, pipeline::FilmTypeMask::any(),
                               Options{.cacheAfter = true}))
    , cache_(cache)
{}

int FlatFieldStep::halo(const model::ParamSet& /*p*/, double /*scale*/, base::Size /*image*/) const
{
    return 0; // punktweise Multiplikation mit der Karte
}

base::Expected<image::ImageBuffer>
FlatFieldStep::process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const
{
    const std::string profile = param(p, "profile", std::string());
    auto gain = cache_.gainMap(profile, x.imageSize, x.backend, x.kernel());
    if (!gain) {
        return std::unexpected(gain.error());
    }
    if (x.tile) {
        auto part = (*gain)->copyRegion(x.tile->padded);
        if (!part) {
            return part;
        }
        return x.backend.multiply(x.kernel(), in, *part);
    }
    return x.backend.multiply(x.kernel(), in, **gain);
}

// --- Retusche ----------------------------------------------------------------------------------

RetouchStep::RetouchStep()
    : DescribedRegion(describe(id::retouch, rank::retouch, "Staubretusche", Phase::Geometry,
                               {spotsParam("spots")}, pipeline::FilmTypeMask::any(),
                               Options{.cacheAfter = true}))
{}

int RetouchStep::halo(const model::ParamSet& p, double /*scale*/, base::Size image) const
{
    int h = 0;
    for (const model::Spot& s : param(p, "spots", model::SpotList{})) {
        const double r = s.radius * image.width;
        const double reach =
            r + std::max(std::abs(s.offset.x) * image.width, std::abs(s.offset.y) * image.height);
        // + Ring für den Helligkeitsausgleich (2 px) + Rundung
        h = std::max(h, static_cast<int>(std::ceil(reach)) + 4);
    }
    return h;
}

model::SpotList RetouchStep::toTile(const model::SpotList& spots, base::Size image, const image::Roi& tile)
{
    model::SpotList out;
    out.reserve(spots.size());
    const double w = image.width;
    const double h = image.height;
    for (const model::Spot& s : spots) {
        model::Spot t;
        t.center = {(s.center.x * w - tile.x) / tile.w, (s.center.y * h - tile.y) / tile.h};
        t.offset = {s.offset.x * w / tile.w, s.offset.y * h / tile.h};
        t.radius = s.radius * w / tile.w;
        out.push_back(t);
    }
    return out;
}

base::Expected<image::ImageBuffer>
RetouchStep::process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const
{
    const model::SpotList spots = param(p, "spots", model::SpotList{});
    if (x.tile) {
        return x.backend.inpaint(x.kernel(), in, toTile(spots, x.imageSize, x.tile->padded));
    }
    return x.backend.inpaint(x.kernel(), in, spots);
}

// --- Lichter und Tiefen, Rauschen, Schärfen ----------------------------------------------

ShadowsHighlightsStep::ShadowsHighlightsStep()
    : DescribedRegion(
          describe(id::shadowshighlights, rank::shadowshighlights, "Lichter und Tiefen", Phase::Finishing,
                   {doubleParam("shadows", 0.0, 0.0, 1.0, ""), doubleParam("highlights", 0.0, 0.0, 1.0, ""),
                    doubleParam("radius", 30.0, 1.0, 200.0, "px")}))
{}

bool ShadowsHighlightsStep::isNeutral(const model::ParamSet& p) const
{
    return param(p, "shadows", 0.0) == 0.0 && param(p, "highlights", 0.0) == 0.0;
}

int ShadowsHighlightsStep::halo(const model::ParamSet& p, double scale, base::Size /*image*/) const
{
    const int q = std::max(1, static_cast<int>(std::lround(param(p, "radius", 30.0) * scale)));
    return 2 * q + 1; // Guided Filter: zweimal Box mit Radius q
}

base::Expected<image::ImageBuffer> ShadowsHighlightsStep::process(pipeline::ExecContext& x,
                                                                  const image::ImageBuffer& in,
                                                                  const model::ParamSet& p) const
{
    return x.backend.localTone(x.kernel(), in, static_cast<float>(param(p, "shadows", 0.0)),
                               static_cast<float>(param(p, "highlights", 0.0)),
                               static_cast<float>(param(p, "radius", 30.0) * x.scale));
}

DenoiseStep::DenoiseStep()
    : DescribedRegion(describe(id::denoise, rank::denoise, "Rauschminderung", Phase::Output,
                               {doubleParam("strength", 0.0, 0.0, 1.0, "")}))
{}

int DenoiseStep::halo(const model::ParamSet& /*p*/, double /*scale*/, base::Size /*image*/) const
{
    return 2; // bilateral 5 × 5
}

base::Expected<image::ImageBuffer>
DenoiseStep::process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const
{
    return x.backend.denoise(x.kernel(), in, static_cast<float>(param(p, "strength", 0.0)));
}

SharpenStep::SharpenStep()
    : DescribedRegion(
          describe(id::sharpen, rank::sharpen, "Schärfen", Phase::Output,
                   {doubleParam("amount", 0.0, 0.0, 3.0, ""), doubleParam("radius", 1.0, 0.3, 5.0, "px"),
                    doubleParam("threshold", 0.0, 0.0, 0.1, "")}))
{}

bool SharpenStep::isNeutral(const model::ParamSet& p) const
{
    return param(p, "amount", 0.0) == 0.0;
}

int SharpenStep::halo(const model::ParamSet& p, double scale, base::Size /*image*/) const
{
    return std::max(1, static_cast<int>(std::ceil(3.0 * param(p, "radius", 1.0) * scale)));
}

base::Expected<image::ImageBuffer>
SharpenStep::process(pipeline::ExecContext& x, const image::ImageBuffer& in, const model::ParamSet& p) const
{
    return x.backend.unsharp(x.kernel(), in, static_cast<float>(param(p, "amount", 0.0)),
                             static_cast<float>(param(p, "radius", 1.0) * x.scale),
                             static_cast<float>(param(p, "threshold", 0.0)));
}

} // namespace autoneg::steps
