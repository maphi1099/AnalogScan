#include "autoneg/application/commands.hpp"

#include <algorithm>
#include <array>

namespace autoneg::application {

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
    static const std::array<std::string_view, 3> ids{"autoneg.crop", "autoneg.filmbase", "autoneg.retouch"};
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
            if (s.value() == "autoneg.crop" || s.value() == "autoneg.filmbase") {
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
