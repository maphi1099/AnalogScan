#include "autoneg/pipeline/compiled_pipeline.hpp"

#include "autoneg/base/hash.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace autoneg::pipeline {

namespace {

image::Roi clip(const image::Roi& r, base::Size s)
{
    return r.intersect(image::Roi{0, 0, s.width, s.height});
}

void addVec(base::Hasher& h, const base::Vec3& v)
{
    h.add(v.x).add(v.y).add(v.z);
}

struct OpHasher
{
    base::Hasher& h;
    void operator()(const image::Matrix3& m) const
    {
        for (const double v : m.m.m) {
            h.add(v);
        }
    }
    void operator()(const image::Offset3& o) const { addVec(h, o.v); }
    void operator()(const image::Scale3& s) const { addVec(h, s.v); }
    void operator()(const image::Log10& /*op*/) const {}
    void operator()(const image::Exp10& /*op*/) const {}
    void operator()(const image::LutRef& l) const
    {
        h.add(static_cast<double>(l->inMin)).add(static_cast<double>(l->inMax)).add(l->channel);
        h.add(static_cast<std::uint64_t>(l->values.size()));
        for (const float v : l->values) {
            h.add(static_cast<double>(v));
        }
    }
    void operator()(const image::Clamp& c) const
    {
        h.add(static_cast<double>(c.lo)).add(static_cast<double>(c.hi));
    }
    void operator()(const image::Saturate& s) const
    {
        h.add(static_cast<double>(s.saturation)).add(static_cast<double>(s.vibrance));
    }
};

/// Stützweite der Filter in Pixeln (für die Kachelausführung).
int filterSupport(image::Filter f)
{
    switch (f) {
    case image::Filter::Nearest:
    case image::Filter::Bilinear:
        return 1;
    case image::Filter::Bicubic:
        return 2;
    case image::Filter::Lanczos3:
        return 3;
    }
    return 3;
}

} // namespace

base::Expected<image::ImageBuffer> Stage::runRegion(ExecContext& x, const image::ImageBuffer& in,
                                                    const image::Roi& inRoi, const image::Roi& outRoi) const
{
    auto out = run(x, in);
    if (!out) {
        return out;
    }
    const image::Roi rel{outRoi.x - inRoi.x, outRoi.y - inRoi.y, outRoi.w, outRoi.h};
    if (rel == image::Roi{0, 0, out->spec().width, out->spec().height}) {
        return out;
    }
    return out->copyRegion(rel);
}

image::Roi Stage::requiredInput(const image::Roi& out, base::Size inSize) const
{
    return clip(out, inSize);
}

std::uint64_t PixelPass::contentHash() const
{
    base::Hasher h;
    h.add(static_cast<std::uint64_t>(program_.ops.size()));
    for (const image::PixelOp& op : program_.ops) {
        h.add(static_cast<std::uint64_t>(op.index()));
        std::visit(OpHasher{h}, op);
    }
    return h.value();
}

base::Expected<image::ImageBuffer> PixelPass::run(ExecContext& x, const image::ImageBuffer& in) const
{
    return x.backend.runPixel(x.kernel(), program_, in);
}

std::uint64_t WarpPass::contentHash() const
{
    base::Hasher h;
    for (const double v : plan_.affine.m) {
        h.add(v);
    }
    h.add(plan_.out.width).add(plan_.out.height).add(static_cast<int>(plan_.filter));
    return h.value();
}

base::Expected<image::ImageBuffer> WarpPass::run(ExecContext& x, const image::ImageBuffer& in) const
{
    return x.backend.warp(x.kernel(), in, plan_);
}

base::Expected<image::ImageBuffer> WarpPass::runRegion(ExecContext& x, const image::ImageBuffer& in,
                                                       const image::Roi& inRoi,
                                                       const image::Roi& outRoi) const
{
    // Ausgabeausschnitt -> Eingabeausschnitt: Verschiebungen vor und nach der Abbildung
    image::WarpPlan local = plan_;
    local.affine = base::Mat2x3::translation(-inRoi.x, -inRoi.y) * plan_.affine *
                   base::Mat2x3::translation(outRoi.x, outRoi.y);
    local.out = {outRoi.w, outRoi.h};
    return x.backend.warp(x.kernel(), in, local);
}

image::Roi WarpPass::requiredInput(const image::Roi& out, base::Size inSize) const
{
    double x0 = 1e300;
    double y0 = 1e300;
    double x1 = -1e300;
    double y1 = -1e300;
    for (const auto& corner :
         {base::Point2{static_cast<double>(out.x), static_cast<double>(out.y)},
          base::Point2{static_cast<double>(out.x + out.w), static_cast<double>(out.y)},
          base::Point2{static_cast<double>(out.x), static_cast<double>(out.y + out.h)},
          base::Point2{static_cast<double>(out.x + out.w), static_cast<double>(out.y + out.h)}}) {
        const base::Point2 p = plan_.affine.apply(corner);
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    const int support = filterSupport(plan_.filter) + 1;
    const image::Roi box{static_cast<int>(std::floor(x0)) - support,
                         static_cast<int>(std::floor(y0)) - support,
                         static_cast<int>(std::ceil(x1 - x0)) + 2 * support + 1,
                         static_cast<int>(std::ceil(y1 - y0)) + 2 * support + 1};
    const image::Roi clipped = clip(box, inSize);
    return clipped.empty() ? image::Roi{0, 0, std::min(1, inSize.width), std::min(1, inSize.height)}
                           : clipped;
}

std::uint64_t RegionPass::contentHash() const
{
    const auto& d = step_->descriptor();
    return base::Hasher{}
        .add(std::string_view(d.id.value()))
        .add(d.version)
        .add(params_.hash())
        .add(scale_)
        .value();
}

base::Expected<image::ImageBuffer> RegionPass::run(ExecContext& x, const image::ImageBuffer& in) const
{
    return step_->process(x, in, params_);
}

image::Roi RegionPass::requiredInput(const image::Roi& out, base::Size inSize) const
{
    const int h = halo();
    return clip(image::Roi{out.x - h, out.y - h, out.w + 2 * h, out.h + 2 * h}, inSize);
}

std::uint64_t OutputPass::contentHash() const
{
    // Transformationen sind unveränderlich; der Cache lebt nur im Arbeitsspeicher
    return base::Hasher{}
        .add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(xf_.get())))
        .value();
}

base::Expected<image::ImageBuffer> OutputPass::run(ExecContext& /*x*/, const image::ImageBuffer& in) const
{
    return xf_->apply(in);
}

int CompiledPipeline::totalHalo() const noexcept
{
    int sum = 0;
    for (const auto& s : stages) {
        sum += s->halo();
    }
    return sum;
}

std::size_t CompiledPipeline::firstDiff(const CompiledPipeline& o) const noexcept
{
    const std::size_t n = std::min(keys.size(), o.keys.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (keys[i] != o.keys[i]) {
            return i;
        }
    }
    return n;
}

} // namespace autoneg::pipeline
