#pragma once

#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/commands.hpp"
#include "autoneg/pipeline/step_registry.hpp"

#include <functional>
#include <memory>
#include <string>

namespace autoneg::ui {

/// Vermittler zwischen Bedienelementen und Anwendung (Architektur 9.14). Nur im UI-Thread.
///
/// Bedienelemente erzeugen Befehle und geben sie hier ab; der Controller führt sie über den
/// CommandBus auf den Zielen aus (aktives Bild oder, bei Synchronisieren, die Auswahl) und
/// stößt die Vorschau an. Während ein Regler gezogen wird, zeigt preview() einen Stand ohne
/// Eintrag in den Verlauf; erst das Loslassen erzeugt den Befehl (LF-DAT-03: eine
/// abgeschlossene Änderung).
/// Virtuell, damit Tests der Bedienelemente ihn ersetzen können (U-UI-02).
class UiController
{
public:
    /// Fordert eine Vorschau mit der gegebenen Liste für das aktive Bild an.
    using PreviewRequest = std::function<void(const model::EditList&)>;

    UiController(application::CommandBus& bus, application::Session& session,
                 application::SyncController& sync, const pipeline::StepRegistry& registry,
                 PreviewRequest preview = {})
        : bus_(&bus)
        , session_(&session)
        , sync_(&sync)
        , registry_(&registry)
        , preview_(std::move(preview))
    {}
    virtual ~UiController() = default;
    UiController(const UiController&) = delete;
    UiController& operator=(const UiController&) = delete;

    /// Führt den Befehl aus; Rückgabe: ob sich etwas geändert hat.
    virtual bool apply(std::unique_ptr<application::ICommand> c);
    /// Zwischenstand während einer Geste (ohne Verlauf).
    virtual void preview(const model::EditList& l);
    /// Aktuelle Liste des aktiven Bildes (leer ohne Bild).
    [[nodiscard]] virtual model::EditList currentList() const;
    [[nodiscard]] virtual const pipeline::StepRegistry& registry() const { return *registry_; }
    virtual bool undo();
    virtual bool redo();

    [[nodiscard]] application::Session& session() { return *session_; }
    void setPreviewRequest(PreviewRequest p) { preview_ = std::move(p); }

protected:
    /// Für Testattrappen ohne Anwendung.
    explicit UiController(const pipeline::StepRegistry& registry)
        : registry_(&registry)
    {}

private:
    application::CommandBus* bus_ = nullptr;
    application::Session* session_ = nullptr;
    application::SyncController* sync_ = nullptr;
    const pipeline::StepRegistry* registry_;
    PreviewRequest preview_;
};

/// Anzeigename eines Schritts (aus der StepRegistry, sonst die Kennung).
[[nodiscard]] std::string stepName(const pipeline::StepRegistry& r, const model::StepId& id);
/// Kurzform der Parameter eines Eintrags für die Bearbeitungsliste, z. B. "ev +0,50".
[[nodiscard]] std::string describeParams(const model::EditEntry& e);
/// Anzeigename der Herkunft (LF-LST-05).
[[nodiscard]] std::string originName(model::Origin o);

} // namespace autoneg::ui
