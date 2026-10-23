#pragma once

#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/pipeline/render.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace autoneg::application {

/// Geschätzter Spitzenbedarf je Quellpixel beim Rendern aus der halben Auflösung (Miniaturen,
/// Kontaktabzug): Dekodieren, Quelle, Zwischenbilder. Gemessen etwa 8–9 Byte (61-MP-RAW);
/// bestimmt mit dem Speicherbudget, wie viele solcher Aufträge gleichzeitig laufen.
inline constexpr std::size_t proxyRenderBytesPerPixel = 16;

/// Rechnet eine Render-Anfrage (Ergänzung K-M6-08: Naht für Tests mit einem Fake).
class IRenderer
{
public:
    virtual ~IRenderer() = default;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) = 0;
    /// Größe der Quelle ohne Dekodieren (für das Speicherbudget im Stapel); Fakes dürfen
    /// Unsupported liefern.
    [[nodiscard]] virtual base::Expected<model::SourceInfo> info(const model::FrameId&,
                                                                 const std::filesystem::path&)
    {
        return base::makeError(base::ErrorCode::Unsupported, "keine Quelleninformation");
    }
};

/// IRenderer über den PipelineExecutor.
class PipelineRenderer final : public IRenderer
{
public:
    explicit PipelineRenderer(pipeline::PipelineExecutor& executor)
        : executor_(executor)
    {}
    [[nodiscard]] base::Expected<image::ImageBuffer> render(const pipeline::RenderRequest& r) override
    {
        return executor_.execute(r);
    }
    [[nodiscard]] base::Expected<model::SourceInfo> info(const model::FrameId& f,
                                                         const std::filesystem::path& path) override
    {
        return executor_.info(f, path);
    }

private:
    pipeline::PipelineExecutor& executor_;
};

/// Vorschau und Detailansicht im eigenen Render-Thread (Architektur 6, 9.13, NF-PERF-01).
///
/// Die neueste Anfrage gewinnt: eine neue Anfrage ersetzt eine noch wartende und bricht eine
/// laufende über ihr CancelToken ab. Fertige Bilder gehen an onReady (im Render-Thread; die
/// Oberfläche reicht sie über ihre Brücke in den UI-Thread weiter). Threadsicher.
class RenderService
{
public:
    using ReadyHandler =
        std::function<void(std::uint64_t seq, const pipeline::RenderRequest&, image::ImageBuffer)>;

    RenderService(IRenderer& renderer, ReadyHandler onReady);
    ~RenderService();
    RenderService(const RenderService&) = delete;
    RenderService& operator=(const RenderService&) = delete;
    RenderService(RenderService&&) = delete;
    RenderService& operator=(RenderService&&) = delete;

    /// Neue Anfrage; liefert ihre fortlaufende Nummer.
    std::uint64_t request(pipeline::RenderRequest r);
    /// Wartet, bis keine Anfrage mehr wartet oder läuft (für Tests und das Beenden).
    void idle();

private:
    void loop(const std::stop_token& stop);

    IRenderer& renderer_;
    ReadyHandler onReady_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::optional<std::pair<std::uint64_t, pipeline::RenderRequest>> pending_;
    std::optional<image::CancelToken> running_;
    std::uint64_t nextSeq_ = 1;
    bool busy_ = false;
    std::jthread thread_; ///< zuletzt
};

} // namespace autoneg::application
