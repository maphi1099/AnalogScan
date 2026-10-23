#include "autoneg/ui/qt_session_bridge.hpp"

#include "autoneg/analysis/tone.hpp"

#include <QtCore/QMetaObject>

#include <algorithm>
#include <cmath>

namespace autoneg::ui {

namespace {

std::uint8_t to8(float v)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
}

} // namespace

QtSessionBridge::QtSessionBridge(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<autoneg::model::FrameId>();
    qRegisterMetaType<autoneg::ui::PreviewFrame>();
}

void QtSessionBridge::onEditChanged(const model::FrameId& f, const model::Revision& rev)
{
    Q_EMIT editChanged(f, rev.seq); // CommandBus läuft im UI-Thread
}

QImage QtSessionBridge::toQImage(const image::ImageBuffer& b)
{
    const auto& s = b.spec();
    const std::span<const float> px = b.host();
    if (px.empty() || s.width <= 0 || s.height <= 0) {
        return {};
    }
    QImage img(s.width, s.height, QImage::Format_RGBA8888);
    const auto ch = static_cast<std::size_t>(s.channels);
    for (int y = 0; y < s.height; ++y) {
        std::uint8_t* line = img.scanLine(y);
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) * ch;
        for (int x = 0; x < s.width; ++x) {
            const std::size_t i = row + static_cast<std::size_t>(x) * ch;
            const float r = px[i];
            const float g = ch >= 3 ? px[i + 1] : r;
            const float bl = ch >= 3 ? px[i + 2] : r;
            line[4 * x] = to8(r);
            line[4 * x + 1] = to8(g);
            line[4 * x + 2] = to8(bl);
            line[4 * x + 3] = 255;
        }
    }
    return img;
}

QImage QtSessionBridge::clippingMask(const image::ImageBuffer& b)
{
    const auto& s = b.spec();
    const std::span<const float> px = b.host();
    if (px.empty()) {
        return {};
    }
    QImage mask(s.width, s.height, QImage::Format_RGBA8888);
    mask.fill(Qt::transparent);
    const auto ch = static_cast<std::size_t>(s.channels);
    for (int y = 0; y < s.height; ++y) {
        std::uint8_t* line = mask.scanLine(y);
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) * ch;
        for (int x = 0; x < s.width; ++x) {
            const std::size_t i = row + static_cast<std::size_t>(x) * ch;
            float hi = px[i];
            float lo = px[i];
            for (std::size_t c = 1; c < std::min<std::size_t>(ch, 3); ++c) {
                hi = std::max(hi, px[i + c]);
                lo = std::min(lo, px[i + c]);
            }
            if (hi >= 1.0F) { // Lichter beschnitten (LF-EDT-14)
                line[4 * x] = 255;
                line[4 * x + 3] = 200;
            } else if (lo <= 0.0F) { // Tiefen beschnitten
                line[4 * x + 2] = 255;
                line[4 * x + 3] = 200;
            }
        }
    }
    return mask;
}

void QtSessionBridge::deliverPreview(std::uint64_t seq, const model::FrameId& f,
                                     const image::ImageBuffer& display, bool detail)
{
    PreviewFrame p;
    p.seq = seq;
    p.frame = f;
    p.image = toQImage(display);
    p.clipping = clippingMask(display);
    if (const int factor = upscale_.load(); factor > 1 && !p.image.isNull()) {
        p.image = p.image.scaled(p.image.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        p.clipping =
            p.clipping.scaled(p.clipping.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    p.histogram = analysis::HistogramCalculator{}.compute(display);
    p.detail = detail;
    QMetaObject::invokeMethod(
        this, [this, frame = std::move(p)] { Q_EMIT previewReady(frame); }, Qt::QueuedConnection);
}

void QtSessionBridge::post(std::function<void()> f)
{
    QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
}

void QtSessionBridge::deliverThumbnail(const model::FrameId& f, const QByteArray& jpeg)
{
    QImage img = QImage::fromData(jpeg, "JPG");
    QMetaObject::invokeMethod(
        this, [this, f, img = std::move(img)] { Q_EMIT thumbnailReady(f, img); }, Qt::QueuedConnection);
}

void QtSessionBridge::deliverProgress(quint64 job, double progress)
{
    QMetaObject::invokeMethod(
        this, [this, job, progress] { Q_EMIT jobProgress(job, progress); }, Qt::QueuedConnection);
}

void QtSessionBridge::deliverFinished(quint64 job, bool ok, const QString& message)
{
    QMetaObject::invokeMethod(
        this, [this, job, ok, message] { Q_EMIT jobFinished(job, ok, message); }, Qt::QueuedConnection);
}

} // namespace autoneg::ui
