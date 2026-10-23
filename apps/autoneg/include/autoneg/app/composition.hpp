#pragma once

// Kompositionswurzel der grafischen Anwendung (Architektur 4, Regel 4): erzeugt alle Adapter
// und Dienste und verdrahtet sie über Konstruktor-Injektion. Auch von den E2E-Tests genutzt,
// damit sie die echte Anwendung prüfen.

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
#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"
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
#include "autoneg/ui/gui_application.hpp"

#include <filesystem>
#include <mutex>

namespace autoneg::app {

struct Directories
{
    std::filesystem::path config; ///< Einstellungen, Vorlagen, Ersatzablage der Begleitdateien
    std::filesystem::path cache;  ///< Miniaturen
};

class Composition
{
public:
    Composition(ui::GuiApplication& gui, Directories dirs);
    ~Composition();
    Composition(const Composition&) = delete;
    Composition& operator=(const Composition&) = delete;

    [[nodiscard]] ui::GuiServices services();
    /// Gespeicherte Sprache der Oberfläche (LF-UI-03), Standard "de".
    [[nodiscard]] std::string language();

private:
    base::Expected<image::ImageBuffer> analysisImage(const model::Frame& f);

    Directories dirs_;
    // Adapter
    adapter::lcms::LcmsColorEngine color_;
    adapter::libraw::LibRawDecoder decoder_;
    adapter::oiio::OiioImageReader reader_;
    adapter::oiio::OiioImageWriter writer_;
    adapter::opencv::OpenCvVision vision_;
    adapter::xxhash::XxHasher hasher_;
    adapter::tbb::TbbTaskExecutor executor_;
    backend::cpu::CpuBackend backend_;
    adapter::qtprint::QtDocumentRenderer document_;
    // Pipeline
    steps::FlatFieldCache flatFields_;
    pipeline::StepRegistry registry_;
    pipeline::PipelineCompiler compiler_;
    pipeline::StageCache cache_;
    pipeline::SourceProvider sources_;
    pipeline::PipelineExecutor executor2_;
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
