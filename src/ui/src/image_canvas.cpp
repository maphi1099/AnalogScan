#include "autoneg/ui/image_canvas.hpp"

#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/rhi/qrhi.h>

#include <algorithm>
#include <cmath>

namespace autoneg::ui {

/// Durchsichtige Ebene für Werkzeuge, Beschnittmaske und Verschieben.
class ImageCanvas::Overlay final : public QWidget
{
public:
    explicit Overlay(ImageCanvas* canvas)
        : QWidget(canvas)
        , canvas_(canvas)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent* /*e*/) override
    {
        QPainter p(this);
        canvas_->overlayPaint(p);
    }
    void mousePressEvent(QMouseEvent* e) override { handle(e); }
    void mouseMoveEvent(QMouseEvent* e) override { handle(e); }
    void mouseReleaseEvent(QMouseEvent* e) override { handle(e); }

private:
    void handle(QMouseEvent* e)
    {
        if (canvas_->overlayMouse(e)) {
            update();
            e->accept();
        } else {
            e->ignore();
        }
    }
    ImageCanvas* canvas_;
};

ImageCanvas::ImageCanvas(QWidget* parent)
    : QRhiWidget(parent)
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
        setApi(QRhiWidget::Api::Null); // ohne Grafikschnittstelle (Tests)
    }
    setMinimumSize(200, 150);
    overlay_ = new Overlay(this);
    overlay_->setGeometry(rect());
}

ImageCanvas::~ImageCanvas() = default;

void ImageCanvas::setSourceSize(QSize px)
{
    if (px.isValid() && !px.isEmpty() && px != source_) {
        source_ = px;
        Q_EMIT viewChanged();
    }
}

void ImageCanvas::setImage(const QImage& img, const QRectF& drawRect)
{
    image_ = img.format() == QImage::Format_RGBA8888 ? img : img.convertToFormat(QImage::Format_RGBA8888);
    drawRect_ = drawRect;
    dirty_ = true;
    update();
    overlay_->update();
}

void ImageCanvas::setClipping(const QImage& mask, bool on)
{
    clipping_ = mask;
    showClipping_ = on;
    overlay_->update();
}

void ImageCanvas::setZoom(Zoom z)
{
    if (z != zoom_) {
        zoom_ = z;
        if (z == Zoom::Fit) {
            center_ = {0.5, 0.5};
        }
        Q_EMIT viewChanged();
        overlay_->update();
    }
}

void ImageCanvas::setCenter(QPointF c)
{
    center_ = {std::clamp(c.x(), 0.0, 1.0), std::clamp(c.y(), 0.0, 1.0)};
    Q_EMIT viewChanged();
    overlay_->update();
}

ViewMapping ImageCanvas::mapping() const
{
    const double w = width();
    const double h = height();
    const double dpr = devicePixelRatioF();
    double scale = 1.0; // logische Widget-Pixel je Quellpixel
    switch (zoom_) {
    case Zoom::Fit:
        scale = std::min(w / source_.width(), h / source_.height());
        break;
    case Zoom::Actual:
        scale = 1.0 / dpr; // 100 %: ein Quellpixel je Gerätepixel
        break;
    case Zoom::Double:
        scale = 2.0 / dpr;
        break;
    }
    const double iw = source_.width() * scale;
    const double ih = source_.height() * scale;
    if (zoom_ == Zoom::Fit) {
        return {QRectF((w - iw) / 2.0, (h - ih) / 2.0, iw, ih)};
    }
    // Ausschnitt um center_; kleiner als das Fenster: mittig
    const double x = iw <= w ? (w - iw) / 2.0 : w / 2.0 - center_.x() * iw;
    const double y = ih <= h ? (h - ih) / 2.0 : h / 2.0 - center_.y() * ih;
    return {QRectF(x, y, iw, ih)};
}

QRectF ImageCanvas::visibleNorm() const
{
    const ViewMapping m = mapping();
    const QPointF a = m.toNorm(QPointF(0, 0));
    const QPointF b = m.toNorm(QPointF(width(), height()));
    const QRectF r(QPointF(std::clamp(a.x(), 0.0, 1.0), std::clamp(a.y(), 0.0, 1.0)),
                   QPointF(std::clamp(b.x(), 0.0, 1.0), std::clamp(b.y(), 0.0, 1.0)));
    return r;
}

void ImageCanvas::setTool(ICanvasTool* t)
{
    tool_ = t;
    overlay_->update();
}

void ImageCanvas::initialize(QRhiCommandBuffer* /*cb*/)
{
    dirty_ = true;
}

void ImageCanvas::render(QRhiCommandBuffer* cb)
{
    QRhi* r = rhi();
    QRhiResourceUpdateBatch* u = r->nextResourceUpdateBatch();
    const QSize target = colorTexture() != nullptr ? colorTexture()->pixelSize() : QSize();
    if (dirty_ && !image_.isNull()) {
        if (!texture_ || texture_->pixelSize() != image_.size()) {
            texture_.reset(r->newTexture(QRhiTexture::RGBA8, image_.size()));
            texture_->create();
        }
        u->uploadTexture(texture_.get(), image_);
        dirty_ = false;
    }
    // Hintergrund löschen, danach das Bild 1:1 an seine Stelle kopieren (kein Shader nötig)
    cb->beginPass(renderTarget(), QColor(40, 40, 40), {1.0F, 0}, u);
    QRhiResourceUpdateBatch* copy = nullptr;
    if (texture_ && colorTexture() != nullptr && !image_.isNull()) {
        const double dpr = devicePixelRatioF();
        QPoint dst(static_cast<int>(std::lround(drawRect_.x() * dpr)),
                   static_cast<int>(std::lround(drawRect_.y() * dpr)));
        QPoint src(0, 0);
        if (dst.x() < 0) {
            src.setX(-dst.x());
            dst.setX(0);
        }
        if (dst.y() < 0) {
            src.setY(-dst.y());
            dst.setY(0);
        }
        const int cw = std::min(image_.width() - src.x(), target.width() - dst.x());
        const int ch = std::min(image_.height() - src.y(), target.height() - dst.y());
        if (cw > 0 && ch > 0) {
            QRhiTextureCopyDescription d;
            d.setPixelSize(QSize(cw, ch));
            d.setSourceTopLeft(src);
            d.setDestinationTopLeft(dst);
            copy = r->nextResourceUpdateBatch();
            copy->copyTexture(colorTexture(), texture_.get(), d);
        }
    }
    cb->endPass(copy);
}

void ImageCanvas::releaseResources()
{
    texture_.reset();
    dirty_ = true;
}

void ImageCanvas::resizeEvent(QResizeEvent* e)
{
    QRhiWidget::resizeEvent(e);
    overlay_->setGeometry(rect());
    Q_EMIT viewChanged();
}

void ImageCanvas::overlayPaint(QPainter& p)
{
    const ViewMapping m = mapping();
    if (showClipping_ && !clipping_.isNull()) {
        p.drawImage(drawRect_, clipping_);
    }
    if (tool_ != nullptr) {
        tool_->paint(p, m);
    }
}

bool ImageCanvas::overlayMouse(QMouseEvent* e)
{
    const ViewMapping m = mapping();
    if (tool_ != nullptr && tool_->mouse(e, m)) {
        return true;
    }
    // Ohne Werkzeug: Verschieben im Zoom, Klick für Pipetten
    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (e->button() == Qt::LeftButton) {
            panFrom_ = e->position();
            panCenter_ = center_;
            return true;
        }
        return false;
    case QEvent::MouseMove:
        if (panFrom_ && zoom_ != Zoom::Fit) {
            const QPointF d = e->position() - *panFrom_;
            center_ = {std::clamp(panCenter_.x() - d.x() / m.imageRect.width(), 0.0, 1.0),
                       std::clamp(panCenter_.y() - d.y() / m.imageRect.height(), 0.0, 1.0)};
            return true;
        }
        return false;
    case QEvent::MouseButtonRelease:
        if (panFrom_) {
            const bool moved = (e->position() - *panFrom_).manhattanLength() > 3.0;
            panFrom_.reset();
            if (moved && zoom_ != Zoom::Fit) {
                Q_EMIT viewChanged();
            } else if (!moved) {
                Q_EMIT picked(m.toNorm(e->position()));
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

} // namespace autoneg::ui
