#pragma once

#include "autoneg/model/edit_list.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/model/roll.hpp"
#include "autoneg/pipeline/step_registry.hpp"

#include <optional>
#include <set>
#include <string>

namespace autoneg::application {

/// Umgebung eines Befehls (Architektur 9.12).
struct CommandContext
{
    model::FrameId frame;
    const model::Roll& roll;
    const pipeline::StepRegistry& registry;
};

/// Änderung an der Bearbeitungsliste eines Bildes (NF-ARC-10): reine Funktion von alter auf
/// neue Liste, ohne Nebenwirkungen; der CommandBus wendet sie an.
class ICommand
{
public:
    virtual ~ICommand() = default;
    [[nodiscard]] virtual std::string label() const = 0;
    [[nodiscard]] virtual model::EditList apply(const model::EditList& l, const CommandContext& c) const = 0;
};

/// Eintrag setzen bzw. ersetzen (LF-LST-01). Rang und Version kommen aus der StepRegistry.
class SetEntryCommand final : public ICommand
{
public:
    explicit SetEntryCommand(model::EditEntry entry)
        : entry_(std::move(entry))
    {}
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    model::EditEntry entry_;
};

/// Eintrag entfernen: das Bild ist danach im Neutralzustand dieses Schritts (LF-LST-03).
class RemoveEntryCommand final : public ICommand
{
public:
    explicit RemoveEntryCommand(model::StepId step)
        : step_(std::move(step))
    {}
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    model::StepId step_;
};

/// Eintrag vorübergehend aus- oder einschalten; Werte bleiben erhalten (LF-LST-06).
class ToggleEntryCommand final : public ICommand
{
public:
    ToggleEntryCommand(model::StepId step, bool on)
        : step_(std::move(step))
        , on_(on)
    {}
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    model::StepId step_;
    bool on_;
};

/// Einfügen: Werte übernehmen oder für bildbezogene Schritte die Erkennung neu anstoßen.
enum class PasteMode
{
    Values,  ///< Werte kopieren
    Redetect ///< bildbezogene Schritte (Bildfeld, Basis) nicht kopieren, sondern neu erkennen
};

/// Einfügen gewählter Einträge eines Quellbilds (LF-BAT-02, -03, LF-LST-05).
/// Übernommene Einträge erhalten Herkunft Copied und copiedFrom = Quellbild.
class PasteCommand final : public ICommand
{
public:
    PasteCommand(model::EditList source, std::optional<model::FrameId> sourceFrame,
                 std::set<model::StepId> selection, PasteMode mode);
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;
    /// Bei PasteMode::Redetect: gewählte bildbezogene Schritte, für die die Anwendung die
    /// Erkennung anstoßen muss (AnalysisService); deren Werte werden nicht kopiert.
    [[nodiscard]] std::set<model::StepId> redetect() const;

private:
    model::EditList source_;
    std::optional<model::FrameId> sourceFrame_;
    std::set<model::StepId> selection_;
    PasteMode mode_;
};

/// Bildbezogene Schritte (Bildfeld, Basis, Retusche): beim Einfügen standardmäßig abgewählt.
[[nodiscard]] bool isFrameSpecific(const model::StepId& step);

/// Zwischenablage für Bearbeitungen (Architektur 9.12).
class EditClipboard
{
public:
    void copy(model::EditList l, std::optional<model::FrameId> from = std::nullopt);
    [[nodiscard]] bool empty() const noexcept { return !source_.has_value(); }
    /// Standardauswahl: alle Schritte der Quelle außer den bildbezogenen (U-APP-04).
    [[nodiscard]] std::set<model::StepId> defaultSelection() const;
    [[nodiscard]] PasteCommand paste(std::set<model::StepId> selection, PasteMode mode) const;

private:
    std::optional<model::EditList> source_;
    std::optional<model::FrameId> from_;
};

/// Vorlage anwenden (LF-BAT-07): Einträge der Vorlage ersetzen gleichartige, Herkunft Preset.
class ApplyPresetCommand final : public ICommand
{
public:
    ApplyPresetCommand(std::string name, model::EditList preset)
        : name_(std::move(name))
        , preset_(std::move(preset))
    {}
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    std::string name_;
    model::EditList preset_;
};

/// Rollen-Standard setzen (LF-BAT-01, LF-BAS-06). Einträge mit Herkunft RollDefault folgen dem
/// neuen Standard; eigene Abweichungen eines Bildes (andere Herkunft) bleiben, außer der Schritt
/// ist für die Rolle gesperrt (z. B. die Basis): dann gilt der Standard für alle Bilder.
class SetRollDefaultsCommand final : public ICommand
{
public:
    SetRollDefaultsCommand(model::EditList defaults, std::set<model::StepId> locked)
        : defaults_(std::move(defaults))
        , locked_(std::move(locked))
    {}
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] model::EditList apply(const model::EditList& l, const CommandContext& c) const override;

private:
    model::EditList defaults_;
    std::set<model::StepId> locked_;
};

} // namespace autoneg::application
