#include "ui/image_canvas.hpp"

#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
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
    void wheelEvent(QWheelEvent* e) override
    {
        if (canvas_->overlayWheel(e)) {
            update();
            e->accept();
        } else {
            e->ignore();
        }
    }

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
    // Lage im Quellbild merken: bei Zoom und Verschieben wird das Bild sofort passend gezeichnet,
    // bis die neue Vorschau da ist
    const ViewMapping m = mapping();
    const QPointF a = m.toNorm(drawRect.topLeft());
    const QPointF b = m.toNorm(drawRect.bottomRight());
    imageNorm_ = QRectF(a, b);
    dirty_ = true;
    refresh();
}

QRectF ImageCanvas::currentRect() const
{
    const ViewMapping m = mapping();
    return {m.toWidget(imageNorm_.topLeft()), m.toWidget(imageNorm_.bottomRight())};
}

bool ImageCanvas::transformed() const
{
    const QRectF r = currentRect();
    return std::abs(r.x() - drawRect_.x()) > 0.5 || std::abs(r.y() - drawRect_.y()) > 0.5 ||
           std::abs(r.width() - drawRect_.width()) > 0.5 || std::abs(r.height() - drawRect_.height()) > 0.5;
}

void ImageCanvas::refresh()
{
    update();
    overlay_->update();
}

void ImageCanvas::clampCenter()
{
    // Bildkanten nicht ins Fenster ziehen: Mitte so begrenzen, dass das Fenster im Bild bleibt
    const ViewMapping m = mapping();
    const double hw = m.imageRect.width() > 0 ? width() / 2.0 / m.imageRect.width() : 0.5;
    const double hh = m.imageRect.height() > 0 ? height() / 2.0 / m.imageRect.height() : 0.5;
    center_ = {hw >= 0.5 ? 0.5 : std::clamp(center_.x(), hw, 1.0 - hw),
               hh >= 0.5 ? 0.5 : std::clamp(center_.y(), hh, 1.0 - hh)};
}

void ImageCanvas::setClipping(const QImage& mask, bool on)
{
    clipping_ = mask;
    showClipping_ = on;
    overlay_->update();
}

void ImageCanvas::setZoom(Zoom z)
{
    switch (z) {
    case Zoom::Fit:
        if (!fit_) {
            fit_ = true;
            center_ = {0.5, 0.5};
            Q_EMIT viewChanged();
            Q_EMIT zoomChanged();
            refresh();
        }
        break;
    case Zoom::Actual:
        setScale(1.0);
        break;
    case Zoom::Double:
        setScale(2.0);
        break;
    case Zoom::Custom:
        break;
    }
}

ImageCanvas::Zoom ImageCanvas::zoom() const
{
    if (fit_) {
        return Zoom::Fit;
    }
    if (std::abs(scale_ - 1.0) < 1e-9) {
        return Zoom::Actual;
    }
    if (std::abs(scale_ - 2.0) < 1e-9) {
        return Zoom::Double;
    }
    return Zoom::Custom;
}

double ImageCanvas::fitScale() const
{
    return std::min(static_cast<double>(width()) / source_.width(),
                    static_cast<double>(height()) / source_.height()) *
           devicePixelRatioF();
}

double ImageCanvas::scale() const
{
    return fit_ ? fitScale() : scale_;
}

void ImageCanvas::setScale(double s, std::optional<QPointF> anchor)
{
    s = std::min(s, kMaxScale);
    if (s <= fitScale() * 1.0001) {
        setZoom(Zoom::Fit); // kleiner als das Fenster geht nicht
        return;
    }
    const QPointF a = anchor.value_or(QPointF(width() / 2.0, height() / 2.0));
    const QPointF norm = mapping().toNorm(a); // Bildstelle unter dem Anker
    if (!fit_ && std::abs(s - scale_) < 1e-12) {
        return;
    }
    fit_ = false;
    {
        scale_ = s;
        // Mitte so legen, dass norm wieder unter dem Anker liegt
        const double logical = s / devicePixelRatioF();
        const double iw = source_.width() * logical;
        const double ih = source_.height() * logical;
        center_ = {norm.x() - (a.x() - width() / 2.0) / iw, norm.y() - (a.y() - height() / 2.0) / ih};
        clampCenter();
    }
    Q_EMIT viewChanged();
    Q_EMIT zoomChanged();
    refresh();
}

void ImageCanvas::setCenter(QPointF c)
{
    center_ = c;
    clampCenter();
    Q_EMIT viewChanged();
    refresh();
}

ViewMapping ImageCanvas::mapping() const
{
    const double w = width();
    const double h = height();
    // logische Widget-Pixel je Quellpixel; 100 %: ein Quellpixel je Gerätepixel
    const double scale = fit_ ? std::min(w / source_.width(), h / source_.height()) : scale_ / devicePixelRatioF();
    const double iw = source_.width() * scale;
    const double ih = source_.height() * scale;
    if (fit_) {
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
    if (texture_ && colorTexture() != nullptr && !image_.isNull() && !transformed()) {
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
    if (!fit_) {
        clampCenter();
    }
    Q_EMIT viewChanged();
    if (fit_) {
        Q_EMIT zoomChanged(); // Prozentwert des Einpassens ändert sich
    }
}

void ImageCanvas::overlayPaint(QPainter& p)
{
    const ViewMapping m = mapping();
    if (!image_.isNull() && transformed()) {
        // Zoom/Verschieben läuft: das letzte Bild sofort an der neuen Stelle (skaliert), bis die
        // passende Vorschau kommt
        p.fillRect(rect(), QColor(40, 40, 40));
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(currentRect(), image_);
    }
    if (showClipping_ && !clipping_.isNull()) {
        p.drawImage(currentRect(), clipping_);
    }
    if (tool_ != nullptr) {
        tool_->paint(p, m);
    }
}

Qt::MouseButton ImageCanvas::effectivePanButton() const
{
    // Ein Werkzeug belegt die linke Taste: dann verschiebt die mittlere
    return tool_ != nullptr && panButton_ == Qt::LeftButton ? Qt::MiddleButton : panButton_;
}

bool ImageCanvas::overlayMouse(QMouseEvent* e)
{
    const ViewMapping m = mapping();
    const Qt::MouseButton pan = effectivePanButton();
    // Verschieben hat Vorrang vor dem Werkzeug (nur mit seiner Taste)
    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (e->button() == pan) {
            panFrom_ = e->position();
            panWith_ = pan;
            panCenter_ = center_;
            if (!fit_) {
                overlay_->setCursor(Qt::SizeAllCursor); // Pfeil in vier Richtungen
            }
            return true;
        }
        break;
    case QEvent::MouseMove:
        if (panFrom_) {
            if (!fit_) {
                const QPointF d = e->position() - *panFrom_;
                center_ = {panCenter_.x() - d.x() / m.imageRect.width(), panCenter_.y() - d.y() / m.imageRect.height()};
                clampCenter();
                update(); // Bildfläche: altes Bild nicht mehr an der alten Stelle
            }
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (panFrom_ && e->button() == panWith_) {
            const QPointF from = *panFrom_;
            const bool moved = (e->position() - from).manhattanLength() > 3.0;
            panFrom_.reset();
            overlay_->unsetCursor();
            if (moved && !fit_) {
                Q_EMIT viewChanged();
                return true;
            }
            if (!moved) {
                if (tool_ != nullptr) {
                    // Klick ohne Ziehen: an das Werkzeug weitergeben (z. B. Rechtsklick hebt Auswahl auf)
                    QMouseEvent press(QEvent::MouseButtonPress, from, e->globalPosition(), e->button(),
                                      e->button(), e->modifiers());
                    tool_->mouse(&press, m);
                    tool_->mouse(e, m);
                } else if (e->button() == Qt::LeftButton) {
                    Q_EMIT picked(m.toNorm(e->position())); // Pipetten
                }
            }
            return true;
        }
        break;
    default:
        break;
    }
    if (tool_ != nullptr && tool_->mouse(e, m)) {
        return true;
    }
    // Ohne Werkzeug: Klick links für Pipetten, auch wenn eine andere Taste verschiebt
    if (tool_ == nullptr && e->button() == Qt::LeftButton) {
        if (e->type() == QEvent::MouseButtonPress) {
            return true;
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            Q_EMIT picked(m.toNorm(e->position()));
            return true;
        }
    }
    return false;
}

bool ImageCanvas::overlayWheel(QWheelEvent* e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0) {
        return false;
    }
    if ((e->modifiers() & Qt::ControlModifier) != 0) {
        zoomBy(std::pow(1.25, delta / 120.0), e->position());
        return true;
    }
    return tool_ != nullptr && tool_->wheel(e, mapping());
}

} // namespace autoneg::ui
