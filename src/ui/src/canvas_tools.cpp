#include "autoneg/ui/canvas_tools.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace autoneg::ui {

namespace {

constexpr double handlePx = 8.0; ///< Fangbereich der Anfasser in Widget-Pixeln
constexpr double minSize = 0.02; ///< kleinster Rahmen (normiert)

double deg(double rad)
{
    return rad * 180.0 / std::numbers::pi;
}

bool near(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) <= handlePx && std::abs(a.y() - b.y()) <= handlePx;
}

} // namespace

// --- CropRotateTool ----------------------------------------------------------------------------

double CropRotateTool::pixelAspect() const
{
    return (rect_.w * source_.width()) / std::max(1e-9, rect_.h * source_.height());
}

void CropRotateTool::setAspect(std::optional<double> ratio)
{
    aspect_ = ratio;
    enforceAspect(Handle::None);
}

void CropRotateTool::swapOrientation()
{
    if (aspect_) {
        aspect_ = 1.0 / *aspect_;
    }
    // Mittelpunkt bleibt, Breite und Höhe in Pixeln tauschen
    const double cx = rect_.x + rect_.w / 2.0;
    const double cy = rect_.y + rect_.h / 2.0;
    const double wPx = rect_.w * source_.width();
    const double hPx = rect_.h * source_.height();
    rect_.w = hPx / source_.width();
    rect_.h = wPx / source_.height();
    rect_.x = cx - rect_.w / 2.0;
    rect_.y = cy - rect_.h / 2.0;
    enforceAspect(Handle::None);
}

void CropRotateTool::enforceAspect(Handle h)
{
    if (aspect_) {
        // Höhe aus der Breite (in Pixeln); passt es nicht, Breite aus der Höhe
        const double ratio = *aspect_;
        double w = rect_.w;
        double hh = w * source_.width() / ratio / source_.height();
        if (hh > 1.0) {
            hh = 1.0;
            w = hh * source_.height() * ratio / source_.width();
        }
        // Verankerung: gegenüberliegende Ecke bzw. Mitte bleibt fest
        const double right = rect_.x + rect_.w;
        const double bottom = rect_.y + rect_.h;
        switch (h) {
        case Handle::TopLeft:
            rect_ = {right - w, bottom - hh, w, hh};
            break;
        case Handle::TopRight:
            rect_ = {rect_.x, bottom - hh, w, hh};
            break;
        case Handle::BottomLeft:
            rect_ = {right - w, rect_.y, w, hh};
            break;
        case Handle::BottomRight:
        case Handle::Right:
        case Handle::Bottom:
            rect_ = {rect_.x, rect_.y, w, hh};
            break;
        default: {
            const double cx = rect_.x + rect_.w / 2.0;
            const double cy = rect_.y + rect_.h / 2.0;
            rect_ = {cx - w / 2.0, cy - hh / 2.0, w, hh};
        }
        }
    }
    clampToImage();
}

void CropRotateTool::clampToImage()
{
    rect_.w = std::clamp(rect_.w, minSize, 1.0);
    rect_.h = std::clamp(rect_.h, minSize, 1.0);
    rect_.x = std::clamp(rect_.x, 0.0, 1.0 - rect_.w);
    rect_.y = std::clamp(rect_.y, 0.0, 1.0 - rect_.h);
}

void CropRotateTool::drag(Handle h, QPointF n)
{
    const double x = std::clamp(n.x(), 0.0, 1.0);
    const double y = std::clamp(n.y(), 0.0, 1.0);
    double l = rect_.x;
    double t = rect_.y;
    double r = rect_.x + rect_.w;
    double b = rect_.y + rect_.h;
    switch (h) {
    case Handle::TopLeft:
        l = x;
        t = y;
        break;
    case Handle::TopRight:
        r = x;
        t = y;
        break;
    case Handle::BottomLeft:
        l = x;
        b = y;
        break;
    case Handle::BottomRight:
        r = x;
        b = y;
        break;
    case Handle::Left:
        l = x;
        break;
    case Handle::Right:
        r = x;
        break;
    case Handle::Top:
        t = y;
        break;
    case Handle::Bottom:
        b = y;
        break;
    default:
        return;
    }
    if (r - l < minSize) {
        (h == Handle::TopLeft || h == Handle::BottomLeft || h == Handle::Left) ? l = r - minSize
                                                                               : r = l + minSize;
    }
    if (b - t < minSize) {
        (h == Handle::TopLeft || h == Handle::TopRight || h == Handle::Top) ? t = b - minSize
                                                                            : b = t + minSize;
    }
    rect_ = {l, t, r - l, b - t};
    if (aspect_ && (h == Handle::Top || h == Handle::Bottom)) {
        // Kante oben/unten: Breite aus der Höhe
        const double w = rect_.h * source_.height() * *aspect_ / source_.width();
        const double cx = rect_.x + rect_.w / 2.0;
        rect_.x = cx - w / 2.0;
        rect_.w = w;
        clampToImage();
        return;
    }
    enforceAspect(h);
}

double CropRotateTool::straighten(QPointF a, QPointF b)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) {
        return angle_;
    }
    // Linie soll waagerecht (|dx| ≥ |dy|) bzw. senkrecht werden
    angle_ = std::abs(dx) >= std::abs(dy) ? deg(std::atan2(dy, dx)) : -deg(std::atan2(dx, dy));
    if (angle_ > 90.0) {
        angle_ -= 180.0;
    } else if (angle_ < -90.0) {
        angle_ += 180.0;
    }
    if (commit_) {
        commit_(rect_, angle_);
    }
    return angle_;
}

CropRotateTool::Handle CropRotateTool::hit(QPointF w, const ViewMapping& m) const
{
    const QRectF r = m.toWidget(rect_);
    if (near(w, r.topLeft())) {
        return Handle::TopLeft;
    }
    if (near(w, r.topRight())) {
        return Handle::TopRight;
    }
    if (near(w, r.bottomLeft())) {
        return Handle::BottomLeft;
    }
    if (near(w, r.bottomRight())) {
        return Handle::BottomRight;
    }
    if (std::abs(w.x() - r.left()) <= handlePx && w.y() > r.top() && w.y() < r.bottom()) {
        return Handle::Left;
    }
    if (std::abs(w.x() - r.right()) <= handlePx && w.y() > r.top() && w.y() < r.bottom()) {
        return Handle::Right;
    }
    if (std::abs(w.y() - r.top()) <= handlePx && w.x() > r.left() && w.x() < r.right()) {
        return Handle::Top;
    }
    if (std::abs(w.y() - r.bottom()) <= handlePx && w.x() > r.left() && w.x() < r.right()) {
        return Handle::Bottom;
    }
    return r.contains(w) ? Handle::Move : Handle::Rotate;
}

bool CropRotateTool::mouse(QMouseEvent* e, const ViewMapping& m)
{
    const QPointF w = e->position();
    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (e->button() != Qt::LeftButton) {
            return false;
        }
        active_ = hit(w, m);
        pressNorm_ = m.toNorm(w);
        pressRect_ = rect_;
        pressAngle_ = angle_;
        return true;
    case QEvent::MouseMove: {
        if (active_ == Handle::None) {
            return false;
        }
        const QPointF n = m.toNorm(w);
        if (active_ == Handle::Move) {
            rect_ = pressRect_;
            rect_.x += n.x() - pressNorm_.x();
            rect_.y += n.y() - pressNorm_.y();
            clampToImage();
        } else if (active_ == Handle::Rotate) {
            // Feindrehen: Winkel um die Rahmenmitte
            const QPointF c = m.toWidget(QPointF(rect_.x + rect_.w / 2.0, rect_.y + rect_.h / 2.0));
            const QPointF p0 = m.toWidget(pressNorm_);
            const double a0 = std::atan2(p0.y() - c.y(), p0.x() - c.x());
            const double a1 = std::atan2(w.y() - c.y(), w.x() - c.x());
            angle_ = std::clamp(pressAngle_ + deg(a1 - a0), -45.0, 45.0);
        } else {
            drag(active_, n);
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (active_ == Handle::None) {
            return false;
        }
        active_ = Handle::None;
        if (commit_ && (rect_ != pressRect_ || angle_ != pressAngle_)) {
            commit_(rect_, angle_);
        }
        return true;
    default:
        return false;
    }
}

void CropRotateTool::paint(QPainter& p, const ViewMapping& m)
{
    const QRectF r = m.toWidget(rect_);
    // Abdunkeln außerhalb des Rahmens
    QPainterPath outside;
    outside.addRect(m.imageRect);
    QPainterPath inside;
    inside.addRect(r);
    p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
    p.setPen(QPen(Qt::white, 1.0));
    p.drawRect(r);
    // Drittelraster
    p.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    for (int i = 1; i < 3; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 3.0, r.top()),
                   QPointF(r.left() + r.width() * i / 3.0, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 3.0),
                   QPointF(r.right(), r.top() + r.height() * i / 3.0));
    }
    p.setBrush(Qt::white);
    for (const QPointF c : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()}) {
        p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    }
    p.setBrush(Qt::NoBrush);
}

// --- RectPickerTool ----------------------------------------------------------------------------

bool RectPickerTool::mouse(QMouseEvent* e, const ViewMapping& m)
{
    const QPointF n = m.toNorm(e->position());
    const auto rectFrom = [](QPointF a, QPointF b) {
        const double l = std::clamp(std::min(a.x(), b.x()), 0.0, 1.0);
        const double t = std::clamp(std::min(a.y(), b.y()), 0.0, 1.0);
        const double r = std::clamp(std::max(a.x(), b.x()), 0.0, 1.0);
        const double bt = std::clamp(std::max(a.y(), b.y()), 0.0, 1.0);
        return model::NormRect{l, t, r - l, bt - t};
    };
    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (e->button() != Qt::LeftButton) {
            return false;
        }
        start_ = n;
        rect_ = rectFrom(n, n);
        return true;
    case QEvent::MouseMove:
        if (!start_) {
            return false;
        }
        rect_ = rectFrom(*start_, n);
        return true;
    case QEvent::MouseButtonRelease:
        if (!start_) {
            return false;
        }
        rect_ = rectFrom(*start_, n);
        start_.reset();
        if (picked_ && rect_->w > 0.0 && rect_->h > 0.0) {
            picked_(*rect_);
        }
        return true;
    default:
        return false;
    }
}

void RectPickerTool::paint(QPainter& p, const ViewMapping& m)
{
    if (rect_) {
        p.setPen(QPen(QColor(255, 200, 0), 1.5, Qt::DashLine));
        p.drawRect(m.toWidget(*rect_));
    }
}

// --- RetouchTool -------------------------------------------------------------------------------

bool RetouchTool::mouse(QMouseEvent* e, const ViewMapping& m)
{
    if (e->type() != QEvent::MouseButtonPress || e->button() != Qt::LeftButton) {
        return false;
    }
    const QPointF n = m.toNorm(e->position());
    if (n.x() < 0.0 || n.y() < 0.0 || n.x() > 1.0 || n.y() > 1.0) {
        return false;
    }
    model::Spot s;
    s.center = {n.x(), n.y()};
    // Quelle: rechts daneben im Abstand von 2,5 Radien (bzw. links am rechten Rand)
    const double off = 2.5 * radius_;
    s.offset = {n.x() + off <= 1.0 ? off : -off, 0.0};
    s.radius = radius_;
    spots_.push_back(s);
    if (spot_) {
        spot_(s);
    }
    return true;
}

void RetouchTool::paint(QPainter& p, const ViewMapping& m)
{
    p.setPen(QPen(QColor(0, 200, 255), 1.0));
    for (const model::Spot& s : spots_) {
        const QPointF c = m.toWidget(QPointF(s.center.x, s.center.y));
        const double r = s.radius * m.imageRect.width();
        p.drawEllipse(c, r, r);
    }
}

} // namespace autoneg::ui
