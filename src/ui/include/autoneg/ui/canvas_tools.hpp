#pragma once

#include "autoneg/model/values.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>

#include <functional>
#include <optional>

class QPainter;
class QMouseEvent;

namespace autoneg::ui {

/// Abbildung zwischen normierten Quellkoordinaten (0…1, AE-10) und Widget-Pixeln: imageRect
/// ist die Lage des ganzen Quellbildes im Widget (bei Zoom auch über den Rand hinaus).
struct ViewMapping
{
    QRectF imageRect;

    [[nodiscard]] QPointF toWidget(QPointF norm) const
    {
        return {imageRect.x() + norm.x() * imageRect.width(), imageRect.y() + norm.y() * imageRect.height()};
    }
    [[nodiscard]] QPointF toNorm(QPointF widget) const
    {
        return {(widget.x() - imageRect.x()) / imageRect.width(),
                (widget.y() - imageRect.y()) / imageRect.height()};
    }
    [[nodiscard]] QRectF toWidget(const model::NormRect& r) const
    {
        return {toWidget(QPointF(r.x, r.y)), toWidget(QPointF(r.x + r.w, r.y + r.h))};
    }
};

/// Werkzeug auf der Arbeitsfläche (Architektur 9.14): zeichnet sein Overlay und verarbeitet
/// die Maus. Ergebnisse gibt es über Rückrufe als Parameter ab, nie als Bilddaten.
class ICanvasTool
{
public:
    virtual ~ICanvasTool() = default;
    virtual void paint(QPainter& p, const ViewMapping& m) = 0;
    /// true: Ereignis verbraucht.
    virtual bool mouse(QMouseEvent* e, const ViewMapping& m) = 0;
};

/// Zuschneiden und Drehen (LF-EDT-01, LF-EDT-02, LF-GEO-03).
///
/// Rahmen in normierten Koordinaten; ein festes Seitenverhältnis gilt in Pixeln des Quellbildes
/// (deshalb kennt das Werkzeug dessen Größe). Ziehen außerhalb des Rahmens dreht fein,
/// begradigen() setzt den Winkel aus einer Linie, die waagerecht oder senkrecht werden soll.
class CropRotateTool final : public ICanvasTool
{
public:
    enum class Handle
    {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Left,
        Right,
        Top,
        Bottom,
        Move,
        Rotate
    };
    /// Wird beim Loslassen mit dem neuen Rahmen und Winkel aufgerufen (ein Befehl je Geste).
    using Commit = std::function<void(const model::NormRect&, double angleDeg)>;

    void setSourceSize(QSizeF px) { source_ = px; }
    void setState(const model::NormRect& r, double angleDeg)
    {
        rect_ = r;
        angle_ = angleDeg;
    }
    void setCommit(Commit c) { commit_ = std::move(c); }

    /// Festes Seitenverhältnis Breite/Höhe in Pixeln; leer = frei. Passt den Rahmen sofort an.
    void setAspect(std::optional<double> ratio);
    [[nodiscard]] std::optional<double> aspect() const { return aspect_; }
    /// Tauscht Hoch- und Querformat (4:3 ↔ 3:4).
    void swapOrientation();
    /// Zieht einen Anfasser auf die normierte Position (Seitenverhältnis bleibt erhalten).
    void drag(Handle h, QPointF norm);
    /// Winkel aus einer Linie in Quellpixeln: die Linie wird waagerecht bzw. senkrecht.
    double straighten(QPointF a, QPointF b);

    [[nodiscard]] const model::NormRect& rect() const { return rect_; }
    [[nodiscard]] double angle() const { return angle_; }
    /// Seitenverhältnis des Rahmens in Pixeln.
    [[nodiscard]] double pixelAspect() const;

    void paint(QPainter& p, const ViewMapping& m) override;
    bool mouse(QMouseEvent* e, const ViewMapping& m) override;

private:
    [[nodiscard]] Handle hit(QPointF widget, const ViewMapping& m) const;
    void enforceAspect(Handle anchorFrom);
    void clampToImage();

    QSizeF source_{3, 2};
    model::NormRect rect_;
    double angle_ = 0.0;
    std::optional<double> aspect_;
    Handle active_ = Handle::None;
    QPointF pressNorm_;
    model::NormRect pressRect_;
    double pressAngle_ = 0.0;
    Commit commit_;
};

/// Rechteck aufziehen, z. B. Messbereich der Filmbasis (LF-BAS-04).
class RectPickerTool final : public ICanvasTool
{
public:
    using Picked = std::function<void(const model::NormRect&)>;
    void setPicked(Picked p) { picked_ = std::move(p); }
    void setRect(std::optional<model::NormRect> r) { rect_ = r; }
    [[nodiscard]] const std::optional<model::NormRect>& rect() const { return rect_; }

    void paint(QPainter& p, const ViewMapping& m) override;
    bool mouse(QMouseEvent* e, const ViewMapping& m) override;

private:
    std::optional<model::NormRect> rect_;
    std::optional<QPointF> start_;
    Picked picked_;
};

/// Reparaturpinsel für Staub (LF-EDT-12): jeder Klick setzt eine Stelle.
class RetouchTool final : public ICanvasTool
{
public:
    using Spot = std::function<void(const model::Spot&)>;
    void setSpot(Spot s) { spot_ = std::move(s); }
    void setRadius(double norm) { radius_ = norm; }
    [[nodiscard]] double radius() const { return radius_; }
    void setSpots(model::SpotList s) { spots_ = std::move(s); }

    void paint(QPainter& p, const ViewMapping& m) override;
    bool mouse(QMouseEvent* e, const ViewMapping& m) override;

private:
    double radius_ = 0.005;
    model::SpotList spots_;
    Spot spot_;
};

} // namespace autoneg::ui
