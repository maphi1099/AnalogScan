#pragma once

// Bewusst ohne Qt-Header: die Kompositionswurzel (apps/autoneg) bindet keine Fremdbibliothek ein.

#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/commands.hpp"
#include "autoneg/application/contact_sheet_service.hpp"
#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/application/roll_service.hpp"
#include "autoneg/application/services.hpp"
#include "autoneg/application/thumbnail_service.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/repositories.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace autoneg::ui {

class MainWindow;

/// Dienste für das Hauptfenster, von der Kompositionswurzel erzeugt (ohne die Qt-Brücke,
/// die GuiApplication selbst besitzt).
struct GuiServices
{
    const pipeline::StepRegistry& registry;
    application::JobScheduler& jobs;
    application::CommandBus& bus;
    application::Session& session;
    application::SyncController& sync;
    application::EditClipboard& clipboard;
    application::RenderService& render;
    application::RollService& rolls;
    application::ThumbnailService& thumbnails;
    application::AnalysisService& analysis;
    application::ExportService& exports;
    application::ContactSheetService& contactSheet;
    ports::IColorEngine& color;
    ports::IPresetRepository& presets;
    ports::ISettingsStore& settings;
    std::function<base::Expected<model::SourceInfo>(const model::Frame&)> sourceInfo;
    /// Meldet das aktive Bild (z. B. damit Miniaturen dessen Quelle nicht freigeben).
    std::function<void(const model::FrameId&)> currentChanged;
    /// Bearbeitungsliste ↔ JSON für Bearbeitungsvorlagen (EditCodec der Datenhaltung).
    std::function<model::JsonValue(const model::EditList&)> encodeEdits;
    std::function<model::EditList(const model::JsonValue&)> decodeEdits;
};

/// Qt-Anwendung der Oberfläche: besitzt QApplication, die Brücke (QtSessionBridge) und das
/// Hauptfenster. Reihenfolge in der Kompositionswurzel: GuiApplication zuerst (liefert
/// previewSink und dispatcher für Render- und Analysedienst), dann die Dienste, dann run().
class GuiApplication
{
public:
    GuiApplication(int& argc, char** argv);
    ~GuiApplication();
    GuiApplication(const GuiApplication&) = delete;
    GuiApplication& operator=(const GuiApplication&) = delete;

    /// Sprache der Oberfläche ("de" oder "en", LF-UI-03); vor createWindow() aufrufen.
    void setLanguage(const std::string& language);

    /// Für den RenderService: bereitet fertige Vorschauen im Render-Thread auf.
    [[nodiscard]] application::RenderService::ReadyHandler previewSink();
    /// Für den AnalysisService: führt Aufgaben im UI-Thread aus.
    [[nodiscard]] application::UiDispatcher dispatcher();

    /// Benutzerverzeichnisse (Architektur 8.1): Einstellungen/Vorlagen bzw. Cache.
    [[nodiscard]] static std::filesystem::path configDir();
    [[nodiscard]] static std::filesystem::path cacheDir();

    /// Erzeugt das Hauptfenster (für Tests ohne exec()).
    MainWindow& createWindow(const GuiServices& s);
    /// Schließt und zerstört das Fenster (vor dem Abbau der Dienste).
    void destroyWindow();
    /// Zeigt das Fenster, öffnet ggf. einen Ordner und läuft bis zum Beenden.
    int run(const GuiServices& s, const std::optional<std::filesystem::path>& folder);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace autoneg::ui
