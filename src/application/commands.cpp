#include "application/commands.hpp"

#include "steps/step_ids.hpp"

#include <algorithm>
#include <array>

namespace autoneg::application {

namespace id = steps::id;

namespace {

/// Rang und Version aus der Registry übernehmen (Einträge kommen auch aus Dateien und Vorlagen).
model::EditEntry normalized(model::EditEntry e, const pipeline::StepRegistry& registry)
{
    if (const pipeline::IStep* s = registry.find(e.step)) {
        e.rank = s->descriptor().rank;
        e.version = s->descriptor().version;
    }
    return e;
}

} // namespace

bool isFrameSpecific(const model::StepId& step)
{
    static const std::array<std::string_view, 5> ids{id::crop, id::rotation, id::filmbase, id::densityrange,
                                                     id::retouch};
    return std::ranges::find(ids, step.value()) != ids.end();
}

std::string SetEntryCommand::label() const
{
    return "Setzen: " + entry_.step.value();
}

model::EditList SetEntryCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    return l.with(normalized(entry_, c.registry));
}

namespace {

/// Lage auf dem Einheitsquadrat wie WarpBuilder::orient: erst spiegeln, dann q-mal 90° im
/// Uhrzeigersinn ((x, y) → (1 − y, x)).
struct Orientation
{
    int q = 0;
    bool fh = false;
    bool fv = false;

    [[nodiscard]] model::NormPoint apply(model::NormPoint p) const
    {
        if (fh) {
            p.x = 1.0 - p.x;
        }
        if (fv) {
            p.y = 1.0 - p.y;
        }
        for (int i = 0; i < q; ++i) {
            p = {1.0 - p.y, p.x};
        }
        return p;
    }
    [[nodiscard]] model::NormPoint inverse(model::NormPoint p) const
    {
        for (int i = 0; i < q; ++i) {
            p = {p.y, 1.0 - p.x};
        }
        if (fv) {
            p.y = 1.0 - p.y;
        }
        if (fh) {
            p.x = 1.0 - p.x;
        }
        return p;
    }
    [[nodiscard]] bool mirrored() const { return fh != fv; }
};

Orientation orientationOf(const model::EditList& l)
{
    Orientation o;
    if (const model::EditEntry* e = l.get(model::StepId(std::string(id::orientation)))) {
        o.q = ((e->params.get<int>(steps::key::quarterTurns).value_or(0) % 4) + 4) % 4;
        o.fh = e->params.get<bool>(steps::key::flipH).value_or(false);
        o.fv = e->params.get<bool>(steps::key::flipV).value_or(false);
    }
    return o;
}

} // namespace

model::EditList SetOrientationCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    namespace key = steps::key;
    const Orientation before = orientationOf(l);
    const Orientation after{q_, flipH_, flipV_};
    // Abbildung alte Lage → neue Lage auf dem Einheitsquadrat
    const auto map = [&](model::NormPoint p) { return after.apply(before.inverse(p)); };
    const bool mirror = before.mirrored() != after.mirrored();
    const bool swap = (before.q + after.q) % 2 == 1;
    model::EditList out = l;
    // Lage selbst; neutral: kein Eintrag
    const model::StepId orient{std::string(id::orientation)};
    if (q_ == 0 && !flipH_ && !flipV_) {
        out = out.without(orient);
    } else {
        model::EditEntry e;
        if (const model::EditEntry* old = l.get(orient)) {
            e = *old;
        } else {
            e.step = orient;
        }
        e.params = e.params.with(key::quarterTurns, q_).with(key::flipH, flipH_).with(key::flipV, flipV_);
        e.origin = model::Origin::Manual;
        e.enabled = true;
        out = out.with(normalized(e, c.registry));
    }
    // Feindrehung: gespiegelt dreht sie andersherum
    if (const model::EditEntry* r = l.get(model::StepId(std::string(id::rotation))); r != nullptr && mirror) {
        model::EditEntry e = *r;
        e.params = e.params.with(key::angle, -e.params.get<double>(key::angle).value_or(0.0));
        out = out.with(e);
    }
    // Zuschnitt: Ecken abbilden, achsenparalleles Rechteck darum
    if (const model::EditEntry* cr = l.get(model::StepId(std::string(id::crop)))) {
        if (const auto rect = cr->params.get<model::NormRect>(key::rect)) {
            const model::NormPoint a = map({rect->x, rect->y});
            const model::NormPoint b = map({rect->x + rect->w, rect->y + rect->h});
            model::EditEntry e = *cr;
            e.params = e.params.with(key::rect, model::NormRect{std::min(a.x, b.x), std::min(a.y, b.y),
                                                                std::abs(b.x - a.x), std::abs(b.y - a.y)});
            out = out.with(e);
        }
    }
    // Retusche: Formen liegen im zugeschnittenen Bild; Größen in Bildbreiten (Breite tauscht)
    if (const model::EditEntry* rt = l.get(model::StepId(std::string(id::retouch)))) {
        if (auto shapes = rt->params.get<model::RetouchShapes>(key::shapes); shapes && !shapes->empty()) {
            const double widthScale = swap ? aspect_ : 1.0; // alte Breite / neue Breite (= alte Höhe)
            for (model::RetouchShape& s : *shapes) {
                s.center = map(s.center);
                s.radius *= widthScale;
                for (model::RetouchStroke& st : s.strokes) {
                    st.width *= widthScale;
                    for (model::NormPoint& p : st.points) {
                        p = map(p);
                    }
                }
            }
            model::EditEntry e = *rt;
            e.params = e.params.with(key::shapes, *shapes);
            out = out.with(e);
        }
    }
    return out;
}

std::string RemoveEntryCommand::label() const
{
    return "Entfernen: " + step_.value();
}

model::EditList RemoveEntryCommand::apply(const model::EditList& l, const CommandContext& /*c*/) const
{
    return l.without(step_);
}

std::string ToggleEntryCommand::label() const
{
    return (on_ ? "Einschalten: " : "Ausschalten: ") + step_.value();
}

model::EditList ToggleEntryCommand::apply(const model::EditList& l, const CommandContext& /*c*/) const
{
    return l.enabled(step_, on_);
}

PasteCommand::PasteCommand(model::EditList source, std::optional<model::FrameId> sourceFrame,
                           std::set<model::StepId> selection, PasteMode mode)
    : source_(std::move(source))
    , sourceFrame_(sourceFrame)
    , selection_(std::move(selection))
    , mode_(mode)
{}

std::string PasteCommand::label() const
{
    return "Einfügen";
}

std::set<model::StepId> PasteCommand::redetect() const
{
    std::set<model::StepId> out;
    if (mode_ == PasteMode::Redetect) {
        for (const model::StepId& s : selection_) {
            if (s.value() == id::crop || s.value() == id::filmbase) {
                out.insert(s);
            }
        }
    }
    return out;
}

model::EditList PasteCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    std::set<model::StepId> copy = selection_;
    for (const model::StepId& s : redetect()) {
        copy.erase(s); // wird neu erkannt statt kopiert
    }
    model::EditList marked;
    for (model::EditEntry e : source_.entries()) {
        if (!copy.contains(e.step)) {
            continue;
        }
        e.origin = model::Origin::Copied;
        e.copiedFrom = sourceFrame_;
        marked = marked.with(normalized(std::move(e), c.registry));
    }
    return l.merge(marked, copy);
}

void EditClipboard::copy(model::EditList l, std::optional<model::FrameId> from)
{
    source_ = std::move(l);
    from_ = from;
}

std::set<model::StepId> EditClipboard::defaultSelection() const
{
    std::set<model::StepId> out;
    if (source_) {
        for (const model::EditEntry& e : source_->entries()) {
            if (!isFrameSpecific(e.step)) {
                out.insert(e.step);
            }
        }
    }
    return out;
}

PasteCommand EditClipboard::paste(std::set<model::StepId> selection, PasteMode mode) const
{
    return PasteCommand(source_.value_or(model::EditList{}), from_, std::move(selection), mode);
}

std::string ApplyPresetCommand::label() const
{
    return "Vorlage: " + name_;
}

model::EditList ApplyPresetCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    model::EditList out = l;
    for (model::EditEntry e : preset_.entries()) {
        e.origin = model::Origin::Preset;
        e.copiedFrom.reset();
        out = out.with(normalized(std::move(e), c.registry));
    }
    return out;
}

std::string SetRollDefaultsCommand::label() const
{
    return "Rollen-Standard";
}

model::EditList SetRollDefaultsCommand::apply(const model::EditList& l, const CommandContext& c) const
{
    model::EditList out = l;
    // Einträge, die dem alten Standard folgten und im neuen fehlen, entfallen
    for (const model::EditEntry& e : l.entries()) {
        if (e.origin == model::Origin::RollDefault && defaults_.get(e.step) == nullptr) {
            out = out.without(e.step);
        }
    }
    for (model::EditEntry d : defaults_.entries()) {
        const model::EditEntry* own = out.get(d.step);
        const bool keepOwn =
            own != nullptr && own->origin != model::Origin::RollDefault && !locked_.contains(d.step);
        if (keepOwn) {
            continue; // Einzelabweichung des Bildes bleibt (U-APP-07)
        }
        d.origin = model::Origin::RollDefault;
        d.copiedFrom.reset();
        out = out.with(normalized(std::move(d), c.registry));
    }
    return out;
}

} // namespace autoneg::application
