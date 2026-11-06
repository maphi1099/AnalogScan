#include "ui/canvas_tools.hpp"

#include "ui/retouch_geometry.hpp"

#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QWheelEvent>

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
    w = m.toWidget(toRect(m.toNorm(w))); // Anfasser im gedrehten Bild prüfen
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

QPointF CropRotateTool::toScan(QPointF q) const
{
    // Drehung im Uhrzeigersinn um die Bildmitte (WarpBuilder::rotate), in Pixeln des Scans
    const double a = -angle_ * 3.14159265358979323846 / 180.0;
    const double w = source_.width();
    const double h = source_.height();
    const double x = (q.x() - 0.5) * w;
    const double y = (q.y() - 0.5) * h;
    return {(x * std::cos(a) - y * std::sin(a)) / w + 0.5, (x * std::sin(a) + y * std::cos(a)) / h + 0.5};
}

QPointF CropRotateTool::toRect(QPointF p) const
{
    const double a = angle_ * 3.14159265358979323846 / 180.0;
    const double w = source_.width();
    const double h = source_.height();
    const double x = (p.x() - 0.5) * w;
    const double y = (p.y() - 0.5) * h;
    return {(x * std::cos(a) - y * std::sin(a)) / w + 0.5, (x * std::sin(a) + y * std::cos(a)) / h + 0.5};
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
        pressNorm_ = toRect(m.toNorm(w));
        pressRect_ = rect_;
        pressAngle_ = angle_;
        return true;
    case QEvent::MouseMove: {
        if (active_ == Handle::None) {
            return false;
        }
        const QPointF n = toRect(m.toNorm(w));
        if (active_ == Handle::Move) {
            rect_ = pressRect_;
            rect_.x += n.x() - pressNorm_.x();
            rect_.y += n.y() - pressNorm_.y();
            clampToImage();
        } else if (active_ == Handle::Rotate) {
            // Feindrehen: Winkel um die Rahmenmitte
            const QPointF c = m.toWidget(toScan(QPointF(rect_.x + rect_.w / 2.0, rect_.y + rect_.h / 2.0)));
            const QPointF p0 = m.toWidget(toScan(pressNorm_));
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
    // Rahmen schräg auf dem Scan: Ecken aus dem gedrehten Bild zurückrechnen
    const auto at = [&](double u, double v) {
        return m.toWidget(toScan(QPointF(rect_.x + u * rect_.w, rect_.y + v * rect_.h)));
    };
    const QPolygonF frame({at(0, 0), at(1, 0), at(1, 1), at(0, 1)});
    // Abdunkeln außerhalb des Rahmens
    QPainterPath outside;
    outside.addRect(m.imageRect);
    QPainterPath inside;
    inside.addPolygon(frame);
    inside.closeSubpath();
    p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 1.0));
    p.drawPolygon(frame);
    // Drittelraster
    p.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    for (int i = 1; i < 3; ++i) {
        p.drawLine(at(i / 3.0, 0), at(i / 3.0, 1));
        p.drawLine(at(0, i / 3.0), at(1, i / 3.0));
    }
    p.setBrush(Qt::white);
    for (const QPointF c : frame) {
        p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    }
    p.setBrush(Qt::NoBrush);
}

// --- RectPickerTool ----------------------------------------------------------------------------

bool RectPickerTool::mouse(QMouseEvent* e, const ViewMapping& m)
{
    if (readOnly_) {
        return false;
    }
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

void RetouchTool::setShapes(model::RetouchShapes s, double aspect)
{
    shapes_ = std::move(s);
    aspect_ = aspect > 0.0 ? aspect : 1.0;
    if (selection_ && *selection_ >= shapes_.size()) {
        selection_.reset();
    }
}

std::string RetouchTool::nextName(model::RetouchShape::Kind k) const
{
    const std::string prefix = k == model::RetouchShape::Kind::Circle ? "Kreis " : "Fläche ";
    int n = 0;
    for (const model::RetouchShape& s : shapes_) {
        if (s.name.starts_with(prefix)) {
            n = std::max(n, std::atoi(s.name.c_str() + prefix.size()));
        }
    }
    return prefix + std::to_string(n + 1);
}

void RetouchTool::select(std::optional<std::size_t> i)
{
    selection_ = i;
    if (selected_) {
        selected_(i);
    }
}

bool RetouchTool::mouse(QMouseEvent* e, const ViewMapping& m)
{
    const QPointF n = m.toNorm(e->position());
    const model::NormPoint np{std::clamp(n.x(), 0.0, 1.0), std::clamp(n.y(), 0.0, 1.0)};
    const bool inside = n.x() >= 0.0 && n.y() >= 0.0 && n.x() <= 1.0 && n.y() <= 1.0;
    const RetouchGeometry geo(aspect_);
    switch (e->type()) {
    case QEvent::MouseMove:
        hover_ = e->position();
        if (stroke_ && (e->buttons() & Qt::LeftButton) != 0) {
            stroke_->points.push_back(np);
        }
        return true; // neu zeichnen (Vorschau der Größe)
    case QEvent::MouseButtonPress:
        if (e->button() == Qt::RightButton) {
            return true; // Auswahl aufheben beim Loslassen
        }
        if (e->button() != Qt::LeftButton || !inside) {
            return false;
        }
        if (mode_ == Mode::Brush || mode_ == Mode::Erase) {
            stroke_ = model::RetouchStroke{mode_ == Mode::Erase, brushWidth_, {np}};
            return true;
        }
        if (const auto hit = geo.hit(shapes_, np)) {
            select(hit); // vorhandene Form anklicken: auswählen
            return true;
        }
        if (mode_ == Mode::Circle) {
            model::RetouchShape c;
            c.kind = model::RetouchShape::Kind::Circle;
            c.name = nextName(c.kind);
            c.center = np;
            c.radius = circleRadius_;
            shapes_.push_back(c);
            select(shapes_.size() - 1);
            if (commit_) {
                commit_(shapes_);
            }
        } else {
            select(std::nullopt);
        }
        return true;
    case QEvent::MouseButtonRelease:
        if (e->button() == Qt::RightButton) {
            select(std::nullopt);
            return true;
        }
        if (e->button() == Qt::LeftButton && stroke_) {
            const model::RetouchStroke st = std::move(*stroke_);
            stroke_.reset();
            const std::size_t before = shapes_.size();
            shapes_ = geo.addStroke(shapes_, st, [this, counter = 0]() mutable {
                // fortlaufend, damit mehrere neue Flächen einer Geste verschiedene Namen haben
                const std::string base = nextName(model::RetouchShape::Kind::Brush);
                const int n = std::atoi(base.c_str() + std::string("Fläche ").size()) + counter++;
                return "Fläche " + std::to_string(n);
            });
            if (!st.erase && !shapes_.empty() && shapes_.size() >= before) {
                // die Fläche, zu der der neue Strich gehört, auswählen
                select(geo.hit(shapes_, st.points.front()));
            } else {
                select(std::nullopt);
            }
            if (commit_) {
                commit_(shapes_);
            }
            return true;
        }
        return e->button() == Qt::LeftButton;
    default:
        return false;
    }
}

bool RetouchTool::wheel(QWheelEvent* e, const ViewMapping& /*m*/)
{
    const double f = std::pow(1.15, e->angleDelta().y() / 120.0);
    if (mode_ == Mode::Circle || mode_ == Mode::Select) {
        if (selection_ && *selection_ < shapes_.size() &&
            shapes_[*selection_].kind == model::RetouchShape::Kind::Circle) {
            shapes_[*selection_].radius = std::clamp(shapes_[*selection_].radius * f, 0.0005, 0.5);
            circleRadius_ = shapes_[*selection_].radius;
            if (commit_) {
                commit_(shapes_);
            }
        } else {
            circleRadius_ = std::clamp(circleRadius_ * f, 0.0005, 0.5);
        }
    } else {
        brushWidth_ = std::clamp(brushWidth_ * f, 0.0005, 0.5);
    }
    if (sizeChanged_) {
        sizeChanged_();
    }
    return true;
}

void RetouchTool::paint(QPainter& p, const ViewMapping& m)
{
    p.setRenderHint(QPainter::Antialiasing);
    const double w = m.imageRect.width();
    const auto pt = [&m](const model::NormPoint& n) { return m.toWidget(QPointF(n.x, n.y)); };
    // Flächen als halbdurchsichtige Ebene: Pinsel malt, Radierer löscht (wie die Retusche)
    const QSize size = p.device() != nullptr ? QSize(p.device()->width(), p.device()->height()) : QSize();
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const model::RetouchShape& s = shapes_[i];
        const bool sel = selection_ == i;
        const QColor color = !s.enabled ? QColor(160, 160, 160) : sel ? QColor(255, 200, 0) : QColor(0, 200, 255);
        if (s.kind == model::RetouchShape::Kind::Circle) {
            p.setPen(QPen(color, sel ? 2.5 : 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pt(s.center), s.radius * w, s.radius * w);
            continue;
        }
        if (size.isEmpty()) {
            continue;
        }
        QImage layer(size, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::Antialiasing);
        for (const model::RetouchStroke& st : s.strokes) {
            lp.setCompositionMode(st.erase ? QPainter::CompositionMode_Clear : QPainter::CompositionMode_SourceOver);
            QColor fill = color;
            fill.setAlpha(sel ? 110 : 70);
            lp.setPen(QPen(fill, std::max(1.0, st.width * w), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (st.points.size() == 1) {
                lp.setBrush(fill);
                lp.drawEllipse(pt(st.points.front()), st.width * w / 2.0, st.width * w / 2.0);
                lp.setBrush(Qt::NoBrush);
            } else {
                QPolygonF line;
                for (const model::NormPoint& q : st.points) {
                    line << pt(q);
                }
                lp.drawPolyline(line);
            }
        }
        lp.end();
        p.drawImage(0, 0, layer);
    }
    // laufender Strich
    if (stroke_) {
        QColor c = stroke_->erase ? QColor(255, 80, 80, 120) : QColor(0, 200, 255, 120);
        p.setPen(QPen(c, std::max(1.0, stroke_->width * w), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF line;
        for (const model::NormPoint& q : stroke_->points) {
            line << pt(q);
        }
        p.drawPolyline(line);
    }
    // Größe des Werkzeugs an der Maus
    if (hover_ && mode_ != Mode::Select) {
        const double r = (mode_ == Mode::Circle ? circleRadius_ : brushWidth_ / 2.0) * w;
        p.setPen(QPen(mode_ == Mode::Erase ? QColor(255, 80, 80) : QColor(255, 255, 255), 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(*hover_, r, r);
    }
}

} // namespace autoneg::ui
