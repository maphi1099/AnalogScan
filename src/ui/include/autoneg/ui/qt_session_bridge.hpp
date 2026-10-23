#pragma once

#include "autoneg/application/command_bus.hpp"
#include "autoneg/image/histogram.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/ids.hpp"

#include <QtCore/QObject>
#include <QtGui/QImage>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>

namespace autoneg::ui {

/// Fertige Vorschau für die Arbeitsfläche, im Render-Thread vorbereitet.
struct PreviewFrame
{
    std::uint64_t seq = 0;
    model::FrameId frame;
    QImage image;    ///< RGBA8888, Monitorfarbraum
    QImage clipping; ///< Maske beschnittener Pixel (rot: Lichter, blau: Tiefen), gleiche Größe
    image::Histogram histogram;
    bool detail = false; ///< Detailansicht (Ausschnitt in Originalauflösung)
};

/// Verbindet Anwendung und Oberfläche (Architektur 9.14): realisiert ISessionObserver und
/// reicht Ergebnisse aus Render- und Job-Threads als Qt-Signale (queued) in den UI-Thread.
/// Die Umwandlung in QImage, die Beschnittmaske und das Histogramm entstehen im aufrufenden
/// Thread, nie im UI-Thread.
class QtSessionBridge final : public QObject, public application::ISessionObserver
{
    Q_OBJECT

public:
    explicit QtSessionBridge(QObject* parent = nullptr);

    void onEditChanged(const model::FrameId& f, const model::Revision& rev) override;

    /// Aus dem Render-Thread: Bild (Werte 0…1 im Monitorfarbraum) aufbereiten und melden.
    void deliverPreview(std::uint64_t seq, const model::FrameId& f, const image::ImageBuffer& display,
                        bool detail);
    /// Aus einem Job-Thread: Miniatur (JPEG) dekodieren und melden.
    void deliverThumbnail(const model::FrameId& f, const QByteArray& jpeg);
    /// Aus einem Job-Thread: Fortschritt bzw. Ende eines Jobs.
    void deliverProgress(quint64 job, double progress);
    void deliverFinished(quint64 job, bool ok, const QString& message);

    /// Aus jedem Thread: f im UI-Thread ausführen.
    void post(std::function<void()> f);

    /// Vergrößerung der Vorschau im Render-Thread (200 %: jedes Pixel 2 × 2, pixelgenau).
    void setUpscale(int factor) { upscale_.store(std::max(1, factor)); }

    /// Umwandlung (öffentlich für Tests): 1 oder 3 Kanäle F32 → RGBA8888.
    [[nodiscard]] static QImage toQImage(const image::ImageBuffer& b);
    [[nodiscard]] static QImage clippingMask(const image::ImageBuffer& b);

Q_SIGNALS:
    void editChanged(const autoneg::model::FrameId& f, qint64 rev);
    void previewReady(const autoneg::ui::PreviewFrame& p);
    void thumbnailReady(const autoneg::model::FrameId& f, const QImage& img);
    void jobProgress(quint64 job, double progress);
    void jobFinished(quint64 job, bool ok, const QString& message);

private:
    std::atomic<int> upscale_{1};
};

} // namespace autoneg::ui

Q_DECLARE_METATYPE(autoneg::model::FrameId)
Q_DECLARE_METATYPE(autoneg::ui::PreviewFrame)
