#pragma once

// Gemeinsame Kompositionswurzel von autoneg und autoneg-cli (Architektur 4, Regel 4): erzeugt
// alle Adapter und Dienste ohne Oberfläche und verdrahtet sie über Konstruktor-Injektion.
// Beide Programme nutzen damit dieselbe Befehls- und Dienstschicht (K-17). Ohne Qt.

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/adapter/json/file_thumbnail_cache.hpp"
#include "autoneg/adapter/json/json_codec.hpp"
#include "autoneg/adapter/json/json_config_store.hpp"
#include "autoneg/adapter/json/json_sidecar_store.hpp"
#include "autoneg/adapter/json/schema_migrator.hpp"
#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/adapter/libraw/libraw_decoder.hpp"
#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/adapter/opencv/opencv_vision.hpp"
#include "autoneg/adapter/spdlog/spd_logger.hpp"
#include "autoneg/adapter/tbb/tbb_task_executor.hpp"
#include "autoneg/adapter/xxhash/xx_hasher.hpp"
#include "autoneg/application/command_bus.hpp"
#include "autoneg/application/commands.hpp"
#include "autoneg/application/contact_sheet_service.hpp"
#include "autoneg/application/jobs.hpp"
#include "autoneg/application/persistence_queue.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/application/roll_service.hpp"
#include "autoneg/application/services.hpp"
#include "autoneg/application/thumbnail_service.hpp"
#include "autoneg/backend/cpu/cpu_backend.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/pipeline/source_provider.hpp"
#include "autoneg/pipeline/stage_cache.hpp"
#include "autoneg/pipeline/step_registry.hpp"
#include "autoneg/steps/registry.hpp"

#include <filesystem>
#include <mutex>
#include <string>

namespace autoneg::app {

struct Directories
{
    std::filesystem::path config; ///< Einstellungen, Vorlagen, Ersatzablage der Begleitdateien
    std::filesystem::path cache;  ///< Miniaturen, Protokoll
};

/// Benutzerverzeichnisse nach Architektur 8: Windows %APPDATA%\AutoNeg bzw.
/// %LOCALAPPDATA%\AutoNeg, Linux $XDG_CONFIG_HOME/autoneg (~/.config/autoneg) bzw.
/// $XDG_CACHE_HOME/autoneg (~/.cache/autoneg). Plattformabhängig (PROGRESS.md).
[[nodiscard]] Directories userDirectories();

class Composition
{
public:
    /// sink: fertige Vorschauen (GUI); dispatcher: Aufgaben im Thread, der den CommandBus
    /// besitzt; document: PDF/Druck (QtDocumentRenderer oder NoDocumentRenderer).
    Composition(Directories dirs, application::RenderService::ReadyHandler sink,
                application::UiDispatcher dispatcher, ports::IDocumentRenderer& document);
    ~Composition();
    Composition(const Composition&) = delete;
    Composition& operator=(const Composition&) = delete;

    /// Gespeicherte Sprache der Oberfläche (LF-UI-03), Standard "de".
    [[nodiscard]] std::string language();
    /// Aktives Bild (UI-Thread): seine Quelle bleibt beim Rendern von Miniaturen erhalten.
    void setCurrent(const model::FrameId& f);
    [[nodiscard]] base::Expected<model::SourceInfo> sourceInfo(const model::Frame& f);
    [[nodiscard]] model::JsonValue encodeEdits(const model::EditList& l) const;
    [[nodiscard]] model::EditList decodeEdits(const model::JsonValue& j) const;

    [[nodiscard]] const Directories& directories() const { return dirs_; }
    [[nodiscard]] const pipeline::StepRegistry& registry() const { return registry_; }
    [[nodiscard]] application::JobScheduler& jobs() { return jobs_; }
    [[nodiscard]] application::CommandBus& bus() { return bus_; }
    [[nodiscard]] application::PersistenceQueue& queue() { return queue_; }
    [[nodiscard]] application::Session& session() { return session_; }
    [[nodiscard]] application::SyncController& sync() { return sync_; }
    [[nodiscard]] application::EditClipboard& clipboard() { return clipboard_; }
    [[nodiscard]] application::RenderService& render() { return render_; }
    [[nodiscard]] application::IRenderer& renderer() { return renderer_; }
    [[nodiscard]] application::RollService& rolls() { return rolls_; }
    [[nodiscard]] application::ThumbnailService& thumbnails() { return thumbnails_; }
    [[nodiscard]] application::AnalysisService& analysis() { return analysis_; }
    [[nodiscard]] application::ExportService& exports() { return exports_; }
    [[nodiscard]] application::ContactSheetService& contactSheet() { return contactSheet_; }
    [[nodiscard]] ports::IColorEngine& color() { return color_; }
    [[nodiscard]] ports::IImageWriter& writer() { return writer_; }
    [[nodiscard]] ports::IImageReader& reader() { return reader_; }
    [[nodiscard]] ports::IPresetRepository& presets() { return config_; }
    [[nodiscard]] ports::ISettingsStore& settings() { return config_; }
    [[nodiscard]] ports::ILogger& logger() { return logger_; }
    [[nodiscard]] ports::ITaskExecutor& executor() { return executor_; }
    [[nodiscard]] pipeline::SourceProvider& sources() { return sources_; }

private:
    base::Expected<image::ImageBuffer> analysisImage(const model::Frame& f);
    void releaseUnlessCurrent(const model::FrameId& f);

    Directories dirs_;
    adapter::spdlog::SpdLogger logger_;
    // Adapter
    adapter::lcms::LcmsColorEngine color_;
    adapter::libraw::LibRawDecoder decoder_;
    adapter::oiio::OiioImageReader reader_;
    adapter::oiio::OiioImageWriter writer_;
    adapter::opencv::OpenCvVision vision_;
    adapter::xxhash::XxHasher hasher_;
    adapter::tbb::TbbTaskExecutor executor_;
    backend::cpu::CpuBackend backend_;
    // Pipeline
    steps::FlatFieldCache flatFields_;
    pipeline::StepRegistry registry_;
    pipeline::PipelineCompiler compiler_;
    pipeline::StageCache cache_;
    pipeline::SourceProvider sources_;
    pipeline::PipelineExecutor pipeline_;
    application::PipelineRenderer renderer_;
    // Datenhaltung
    adapter::json::AtomicFileWriter fileWriter_;
    adapter::json::SchemaMigrator migrator_;
    adapter::json::JsonSidecarStore store_;
    adapter::json::JsonConfigStore config_;
    adapter::json::FileThumbnailCache thumbnailCache_;
    adapter::json::EditCodec codec_;
    // Anwendung
    application::PersistenceQueue queue_;
    application::CommandBus bus_;
    application::Session session_;
    application::SyncController sync_;
    application::EditClipboard clipboard_;
    application::JobScheduler jobs_;
    application::RenderService render_;
    application::RollService rolls_;
    application::ThumbnailService thumbnails_;
    application::AnalysisService analysis_;
    application::ExportService exports_;
    application::ContactSheetService contactSheet_;

    std::mutex currentMutex_;
    model::FrameId current_; ///< aktives Bild (UI-Thread schreibt, Jobs lesen)
};

} // namespace autoneg::app
