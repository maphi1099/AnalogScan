#include "autoneg/pipeline/pipeline_executor.hpp"

#include "autoneg/base/hash.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace autoneg::pipeline {

namespace {

base::Expected<image::ImageBuffer> cancelled()
{
    return base::makeError(base::ErrorCode::Cancelled, "Berechnung abgebrochen");
}

/// Alle Zwischenbilder sind F32 (LF-KON-07, U-PIP-18).
base::Expected<void> checkF32(const image::ImageBuffer& b, std::size_t stage)
{
    if (b.spec().format != image::PixelFormat::F32) {
        return base::makeError(base::ErrorCode::Internal,
                               "Stufe " + std::to_string(stage) + " liefert kein 32-bit-Gleitkommabild");
    }
    return {};
}

std::uint64_t frameKey(const model::FrameId& f, std::uint64_t key)
{
    return base::Hasher{}.add(std::as_bytes(f.bytes())).add(key).value();
}

bool fullResolution(RenderMode m)
{
    return m == RenderMode::Export || m == RenderMode::Detail;
}

} // namespace

base::Expected<image::ImageBuffer> PipelineExecutor::execute(const RenderRequest& r)
{
    if (!r.list) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Render-Anfrage ohne Bearbeitungsliste");
    }
    const RenderProfile& p = r.profile;
    const int level = fullResolution(p.mode) ? 0 : 1;
    const auto info = sources_.info(r.frame, r.path);
    if (!info) {
        return std::unexpected(info.error());
    }
    auto src = sources_.source(r.frame, r.path, level);
    if (!src) {
        return src;
    }
    if (r.cancel.cancelled()) {
        return cancelled();
    }

    // Proxy auf Bildschirmgröße (Architektur 5.5)
    if (!fullResolution(p.mode) && p.target.width > 0 && p.target.height > 0) {
        const double factor = std::min(static_cast<double>(p.target.width) / src->spec().width,
                                       static_cast<double>(p.target.height) / src->spec().height);
        if (factor < 1.0) {
            const base::Size size{std::max(1, static_cast<int>(std::lround(src->spec().width * factor))),
                                  std::max(1, static_cast<int>(std::lround(src->spec().height * factor)))};
            const std::uint64_t key = frameKey(r.frame, base::Hasher{}
                                                            .add(std::string_view("proxy"))
                                                            .add(level)
                                                            .add(size.width)
                                                            .add(size.height)
                                                            .value());
            std::optional<image::ImageBuffer> hit = p.useCache ? cache_.find(key) : std::nullopt;
            if (hit) {
                src = *hit;
            } else {
                src = backend_.resample(ports::KernelContext{r.cancel}, *src, size);
                if (!src) {
                    return src;
                }
                if (p.useCache) {
                    cache_.put(r.frame, key, *src);
                }
            }
        }
    }

    const double scale = info->width > 0 ? static_cast<double>(src->spec().width) / info->width : 1.0;
    CompileOptions options;
    options.stopAfter = r.stopAfter;
    // Export mit Zielgröße (LF-EXP-06): volle Auflösung rechnen, im Arbeitsfarbraum auf die lange
    // Kante verkleinern (nie vergrößern), erst danach in den Zielfarbraum umrechnen
    const bool shrink = p.mode == RenderMode::Export && p.target.width > 0 && p.target.height > 0;
    RenderProfile compileProfile = p;
    std::shared_ptr<const ports::IColorTransform> lateOutput;
    if (shrink) {
        lateOutput = std::exchange(compileProfile.output.transform, nullptr);
    }
    const CompiledPipeline cp =
        compiler_.compile(*r.list, r.film, compileProfile, src->spec(), scale, options);
    auto out = run(cp, *src, r, Execution::Auto, scale);
    if (out && shrink) {
        const double factor = std::min(static_cast<double>(p.target.width) / out->spec().width,
                                       static_cast<double>(p.target.height) / out->spec().height);
        if (factor < 1.0) {
            const base::Size size{std::max(1, static_cast<int>(std::lround(out->spec().width * factor))),
                                  std::max(1, static_cast<int>(std::lround(out->spec().height * factor)))};
            out = backend_.resample(ports::KernelContext{r.cancel}, *out, size);
        }
    }
    if (out && lateOutput && !r.stopAfter) {
        out = backend_.colorTransform(ports::KernelContext{r.cancel}, *out, *lateOutput);
    }
    // Ganzzahlige Ausgabe für den Export (K-M10-03): parallel im Backend statt im Schreiber
    if (out && (p.output.bits == 8 || p.output.bits == 16) && p.output.transform && !r.stopAfter) {
        return backend_.quantize(ports::KernelContext{r.cancel}, *out, p.output.bits);
    }
    return out;
}

base::Expected<image::ImageBuffer> PipelineExecutor::run(const CompiledPipeline& cp,
                                                         const image::ImageBuffer& src,
                                                         const RenderRequest& r, Execution e, double scale)
{
    if (e == Execution::Auto) {
        if (cp.profile.mode == RenderMode::Export) {
            e = Execution::Tiled;
        } else if (cp.profile.mode == RenderMode::Detail && cp.profile.roi) {
            return runTiled(cp, src, r, scale, cp.profile.roi);
        } else {
            e = Execution::Whole;
        }
    }
    return e == Execution::Tiled ? runTiled(cp, src, r, scale, std::nullopt) : runWhole(cp, src, r, scale);
}

base::Expected<image::ImageBuffer> PipelineExecutor::runWhole(const CompiledPipeline& cp,
                                                              const image::ImageBuffer& src,
                                                              const RenderRequest& r, double scale)
{
    const bool useCache = cp.profile.useCache;
    const std::size_t n = cp.stages.size();
    std::size_t start = 0;
    image::ImageBuffer cur = src;
    if (useCache) {
        // spätesten vorhandenen Cache-Punkt suchen; ab dort neu rechnen (AE-06)
        for (std::size_t i = n; i-- > 0;) {
            if (!cp.stages[i]->cacheAfter()) {
                continue;
            }
            if (auto hit = cache_.find(frameKey(r.frame, cp.stages[i]->key()))) {
                cur = *hit;
                start = i + 1;
                break;
            }
        }
    }
    ExecContext x{backend_, r.cancel, scale, std::nullopt, {}};
    for (std::size_t i = start; i < n; ++i) {
        if (r.cancel.cancelled()) {
            return cancelled();
        }
        const Stage& stage = *cp.stages[i];
        x.imageSize = {cur.spec().width, cur.spec().height};
        auto out = stage.run(x, cur);
        if (!out) {
            return out;
        }
        if (auto ok = checkF32(*out, i); !ok) {
            return std::unexpected(ok.error());
        }
        if (useCache && stage.cacheAfter()) {
            cache_.put(r.frame, frameKey(r.frame, stage.key()), *out);
        }
        cur = std::move(*out);
    }
    return cur;
}

base::Expected<image::ImageBuffer> PipelineExecutor::runTiled(const CompiledPipeline& cp,
                                                              const image::ImageBuffer& src,
                                                              const RenderRequest& r, double scale,
                                                              const std::optional<image::Roi>& region)
{
    const std::size_t n = cp.stages.size();
    std::vector<base::Size> sizes(n + 1);
    sizes[0] = {src.spec().width, src.spec().height};
    for (std::size_t i = 0; i < n; ++i) {
        sizes[i + 1] = cp.stages[i]->outputSize(sizes[i]);
    }
    const image::Roi full{0, 0, sizes[n].width, sizes[n].height};
    const image::Roi area = region ? region->intersect(full) : full;
    if (area.empty()) {
        return base::makeError(base::ErrorCode::OutOfRange, "Ausschnitt liegt außerhalb des Bildes");
    }
    if (n == 0) {
        return area == full ? base::Expected<image::ImageBuffer>(src) : src.copyRegion(area);
    }

    const image::ImageSpec areaSpec{area.w, area.h, 1, image::PixelFormat::F32,
                                    image::ColorSpace::WorkingLinear};
    const image::TileGrid grid = tiles_.plan(areaSpec, 0);
    std::shared_ptr<image::HostStorage> storage;
    image::ImageSpec outSpec;
    ExecContext x{backend_, r.cancel, scale, std::nullopt, {}};
    std::vector<image::Roi> rois(n + 1);

    for (const image::Tile& t : grid.tiles()) {
        if (r.cancel.cancelled()) {
            return cancelled();
        }
        // benötigte Ausschnitte rückwärts durch alle Stufen
        rois[n] = image::Roi{t.core.x + area.x, t.core.y + area.y, t.core.w, t.core.h};
        for (std::size_t i = n; i-- > 0;) {
            rois[i] = cp.stages[i]->requiredInput(rois[i + 1], sizes[i]);
        }
        auto cur = rois[0] == image::Roi{0, 0, sizes[0].width, sizes[0].height}
                       ? base::Expected<image::ImageBuffer>(src)
                       : src.copyRegion(rois[0]);
        if (!cur) {
            return cur;
        }
        for (std::size_t i = 0; i < n; ++i) {
            x.tile = image::Tile{rois[i + 1], rois[i]};
            x.imageSize = sizes[i];
            auto out = cp.stages[i]->runRegion(x, *cur, rois[i], rois[i + 1]);
            if (!out) {
                return out;
            }
            if (auto ok = checkF32(*out, i); !ok) {
                return std::unexpected(ok.error());
            }
            if (out->spec().width != rois[i + 1].w || out->spec().height != rois[i + 1].h) {
                return base::makeError(base::ErrorCode::Internal,
                                       "Stufe " + std::to_string(i) + " liefert eine falsche Kachelgröße");
            }
            cur = std::move(out);
        }
        // Kachel in das Ergebnis kopieren
        const image::ImageSpec& ts = cur->spec();
        if (!storage) {
            outSpec = ts;
            outSpec.width = area.w;
            outSpec.height = area.h;
            storage = std::make_shared<image::HostStorage>(outSpec.sizeBytes());
        }
        const auto from = cur->hostBytes();
        const auto to = storage->bytes();
        const std::size_t pixelBytes =
            static_cast<std::size_t>(ts.channels) * image::bytesPerSample(ts.format);
        for (int y = 0; y < ts.height; ++y) {
            const std::size_t dst = static_cast<std::size_t>(rois[n].y - area.y + y) * outSpec.rowBytes() +
                                    static_cast<std::size_t>(rois[n].x - area.x) * pixelBytes;
            std::ranges::copy(from.subspan(static_cast<std::size_t>(y) * ts.rowBytes(), ts.rowBytes()),
                              to.subspan(dst).begin());
        }
    }
    return image::ImageBuffer::wrap(outSpec, std::move(storage));
}

} // namespace autoneg::pipeline
