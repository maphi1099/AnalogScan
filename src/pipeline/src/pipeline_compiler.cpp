#include "autoneg/pipeline/pipeline_compiler.hpp"

#include "autoneg/base/hash.hpp"
#include "autoneg/pipeline/pixel_program_builder.hpp"
#include "autoneg/pipeline/warp_builder.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace autoneg::pipeline {

namespace {

struct Active
{
    const IStep* step;
    model::ParamSet params;
};

std::uint64_t seedKey(const RenderProfile& p, const image::ImageSpec& source, double scale)
{
    base::Hasher h;
    h.add(static_cast<int>(p.mode)).add(p.target.width).add(p.target.height);
    h.add(p.roi.has_value());
    if (p.roi) {
        h.add(p.roi->x).add(p.roi->y).add(p.roi->w).add(p.roi->h);
    }
    h.add(source.width).add(source.height).add(source.channels).add(static_cast<int>(source.format));
    h.add(static_cast<int>(source.space)).add(scale);
    return h.value();
}

/// Liegt zwischen den Rängen prev und cur (prev <= Rang < cur) ein Cache-Punkt?
bool cachePointBetween(const StepRegistry& reg, int prev, int cur)
{
    for (const IStep* s : reg.ordered()) {
        const auto& d = s->descriptor();
        if (d.cacheAfter && d.rank >= prev && d.rank < cur) {
            return true;
        }
    }
    return false;
}

} // namespace

CompiledPipeline PipelineCompiler::compile(const model::EditList& l, model::FilmType f,
                                           const RenderProfile& p, const image::ImageSpec& source,
                                           double scale, const CompileOptions& options) const
{
    CompiledPipeline cp;
    cp.profile = p;

    // Einträge unbekannter Schritte: überspringen und melden (NF-ARC-09, U-PIP-17)
    for (const model::EditEntry& e : l.entries()) {
        if (registry_.find(e.step) == nullptr) {
            cp.warnings.push_back("Unbekannter Schritt " + e.step.value() + " (Version " +
                                  std::to_string(e.version) + ") wird übersprungen");
        }
    }

    // 1. aktive Schritte in Pipeline-Reihenfolge
    std::vector<Active> active;
    for (const IStep* step : registry_.ordered()) {
        const auto& d = step->descriptor();
        const model::EditEntry* e = l.get(d.id);
        if (e == nullptr && d.visible) {
            continue; // ohne Eintrag keine Stufe
        }
        if (e != nullptr && !e->enabled) {
            continue;
        }
        model::ParamSet params = e != nullptr ? e->params : model::ParamSet{};
        if (step->isNeutral(params)) {
            continue;
        }
        if (auto ok = step->validate(params); !ok) {
            cp.warnings.push_back(ok.error().message + " – Eintrag wird übersprungen");
            continue;
        }
        active.push_back(Active{step, std::move(params)});
    }

    // 2. Steuerstufen legen Varianten fest (z. B. Filmtyp)
    StepContext ctx{f, scale, source, std::nullopt};
    for (const Active& a : active) {
        if (a.step->nature() == StepNature::Control) {
            static_cast<const ControlStep*>(a.step)->configure(ctx, a.params);
        }
    }
    std::erase_if(active, [&](const Active& a) {
        return a.step->nature() == StepNature::Control || !a.step->descriptor().films.contains(ctx.film);
    });

    // Teilausführung: alles nach dem Abgriffpunkt entfällt
    if (options.stopAfter) {
        if (const IStep* stop = registry_.find(*options.stopAfter)) {
            const int stopRank = stop->descriptor().rank;
            std::erase_if(active, [&](const Active& a) { return a.step->descriptor().rank > stopRank; });
        }
    }

    // 3./4. Gruppieren
    base::Size size{source.width, source.height};
    std::optional<PixelProgramBuilder> pixel;
    std::optional<WarpBuilder> warp;
    std::vector<model::StepId> groupSteps;
    std::vector<std::unique_ptr<Stage>> stages;

    const auto flush = [&] {
        std::unique_ptr<Stage> stage;
        if (pixel && !pixel->empty()) {
            stage = std::make_unique<PixelPass>(pixel->build());
        } else if (warp && !warp->isIdentity()) {
            const image::WarpPlan plan = warp->build();
            size = plan.out;
            stage = std::make_unique<WarpPass>(plan);
        }
        pixel.reset();
        warp.reset();
        if (stage) {
            stage->steps_ = std::move(groupSteps);
            stages.push_back(std::move(stage));
        }
        groupSteps.clear();
    };

    int prevRank = -1;
    StepNature prevNature = StepNature::Control;
    for (const Active& a : active) {
        const auto& d = a.step->descriptor();
        const StepNature nature = a.step->nature();
        const bool cacheBoundary = prevRank >= 0 && cachePointBetween(registry_, prevRank, d.rank);
        const bool newGroup = !options.fuse || nature != prevNature || nature == StepNature::Region ||
                              (p.useCache && cacheBoundary);
        if (newGroup) {
            flush();
            if (p.useCache && cacheBoundary && !stages.empty()) {
                stages.back()->cacheAfter_ = true;
            }
        }
        switch (nature) {
        case StepNature::Pointwise:
            if (!pixel) {
                pixel.emplace();
            }
            static_cast<const PointwiseStep*>(a.step)->emit(*pixel, a.params, ctx);
            groupSteps.push_back(d.id);
            break;
        case StepNature::Geometric:
            if (!warp) {
                warp.emplace(size);
            }
            static_cast<const GeometricStep*>(a.step)->emit(*warp, a.params, ctx);
            groupSteps.push_back(d.id);
            break;
        case StepNature::Region: {
            auto stage =
                std::make_unique<RegionPass>(static_cast<const RegionStep*>(a.step), a.params, scale);
            stage->steps_ = {d.id};
            stages.push_back(std::move(stage));
            break;
        }
        case StepNature::Control:
            break;
        }
        prevRank = d.rank;
        prevNature = nature;
    }
    flush();

    if (p.output.transform && !options.stopAfter) {
        stages.push_back(std::make_unique<OutputPass>(p.output.transform));
    }

    // Schlüsselkette: jede Stufe hängt von allen vorherigen ab
    std::uint64_t key = seedKey(p, source, scale);
    for (auto& s : stages) {
        key = base::Hasher{}.add(key).add(s->contentHash()).value();
        s->key_ = key;
        cp.keys.push_back(key);
    }
    cp.stages = std::move(stages);
    return cp;
}

} // namespace autoneg::pipeline
