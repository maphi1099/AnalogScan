#pragma once

#include "model/values.hpp"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>

#include <functional>
#include <optional>
#include <string>

class QPainter;
class QMouseEvent;
class QWheelEvent;

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
    /// Mausrad ohne Strg (Strg+Rad zoomt immer); true: verbraucht.
    virtual bool wheel(QWheelEvent* /*e*/, const ViewMapping& /*m*/) { return false; }
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
    /// Rahmen liegt im gedrehten Bild (hinter autoneg.rotation, ohne Auffüllen); die Fläche zeigt
    /// den ungedrehten Scan. toScan: Punkt des gedrehten Bilds → Scan, toRect: umgekehrt.
    [[nodiscard]] QPointF toScan(QPointF q) const;
    [[nodiscard]] QPointF toRect(QPointF p) const;
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
    /// Nur anzeigen (Messbereich), nicht neu aufziehen.
    void setReadOnly(bool on) { readOnly_ = on; }
    [[nodiscard]] const std::optional<model::NormRect>& rect() const { return rect_; }

    void paint(QPainter& p, const ViewMapping& m) override;
    bool mouse(QMouseEvent* e, const ViewMapping& m) override;

private:
    std::optional<model::NormRect> rect_;
    std::optional<QPointF> start_;
    Picked picked_;
    bool readOnly_ = false;
};

/// Staubretusche mit Formen (LF-EDT-12, K-M10-23). Werkzeuge: Auswählen, Kreis (Klick setzt,
/// Mausrad ändert die Größe), Pinsel (malt eine Fläche), Radierer (nimmt Fläche weg; Mausrad
/// ändert die Breite). Klick auf eine Form wählt sie aus, Rechtsklick hebt die Auswahl auf.
/// Jede abgeschlossene Geste übergibt die neue Formenliste (ein Befehl je Geste).
class RetouchTool final : public ICanvasTool
{
public:
    enum class Mode
    {
        Select,
        Circle,
        Brush,
        Erase
    };
    using Commit = std::function<void(const model::RetouchShapes&)>;
    using Selected = std::function<void(std::optional<std::size_t>)>;
    using SizeChanged = std::function<void()>;

    void setCommit(Commit c) { commit_ = std::move(c); }
    void setSelectedCallback(Selected s) { selected_ = std::move(s); }
    /// Kreisgröße bzw. Pinselbreite per Mausrad geändert (Regler nachziehen).
    void setSizeChanged(SizeChanged s) { sizeChanged_ = std::move(s); }
    void setMode(Mode m) { mode_ = m; }
    [[nodiscard]] Mode mode() const { return mode_; }
    /// Formen und Seitenverhältnis (Höhe/Breite) des Bilds.
    void setShapes(model::RetouchShapes s, double aspect);
    [[nodiscard]] const model::RetouchShapes& shapes() const { return shapes_; }
    void setSelection(std::optional<std::size_t> i) { selection_ = i; }
    [[nodiscard]] std::optional<std::size_t> selection() const { return selection_; }
    /// Radius neuer Kreise bzw. Breite des Pinsels, normiert auf die Bildbreite.
    void setCircleRadius(double r) { circleRadius_ = r; }
    [[nodiscard]] double circleRadius() const { return circleRadius_; }
    void setBrushWidth(double w) { brushWidth_ = w; }
    [[nodiscard]] double brushWidth() const { return brushWidth_; }
    /// Name für die nächste Form (Kreis n / Fläche n).
    [[nodiscard]] std::string nextName(model::RetouchShape::Kind k) const;

    void paint(QPainter& p, const ViewMapping& m) override;
    bool mouse(QMouseEvent* e, const ViewMapping& m) override;
    bool wheel(QWheelEvent* e, const ViewMapping& m) override;

private:
    void select(std::optional<std::size_t> i);

    Mode mode_ = Mode::Circle;
    model::RetouchShapes shapes_;
    double aspect_ = 1.0;
    std::optional<std::size_t> selection_;
    double circleRadius_ = 0.01;
    double brushWidth_ = 0.01;
    std::optional<model::RetouchStroke> stroke_; ///< laufender Pinselstrich
    std::optional<QPointF> hover_;               ///< Mausposition (Vorschau der Größe)
    Commit commit_;
    Selected selected_;
    SizeChanged sizeChanged_;
};

} // namespace autoneg::ui
