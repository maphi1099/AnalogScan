#pragma once

#include "ui/canvas_tools.hpp"

#include <QtGui/QImage>
#include <QtWidgets/QRhiWidget>

#include <memory>
#include <optional>

class QRhiTexture;

namespace autoneg::ui {

/// Arbeitsfläche (Architektur 9.14, LF-UI-02): zeigt die fertige Vorschau als Textur.
///
/// Die Vorschau kommt aus dem Render-Thread bereits in Monitorgröße und -farbraum; die
/// Fläche kopiert sie nur in ihr Ziel (ohne Shader, ohne Rechnen im UI-Thread). Zoom stufenlos
/// von Einpassen bis 800 % (Strg+Mausrad um die Stelle unter der Maus), Ausschnitt mit der Maus
/// verschiebbar; jede Änderung meldet viewChanged, worauf das Hauptfenster eine passende
/// Vorschau bzw. Detailansicht anfordert.
/// Werkzeuge zeichnen auf einer durchsichtigen Ebene darüber.
/// Ohne GPU-Schnittstelle (Tests, offscreen) läuft QRhi im Null-Backend.
class ImageCanvas final : public QRhiWidget
{
    Q_OBJECT

public:
    enum class Zoom
    {
        Fit,
        Actual, ///< 100 %
        Double, ///< 200 %
        Custom  ///< jeder andere Faktor
    };
    static constexpr double kMaxScale = 8.0;  ///< 800 %
    static constexpr double kMinScale = 0.02; ///< Untergrenze des Reglers; kleiner als Einpassen geht nicht

    explicit ImageCanvas(QWidget* parent = nullptr);
    ~ImageCanvas() override;

    /// Größe des Quellbildes in Pixeln (nach Geometrie: Drehung, Zuschnitt), für Zoom und Werkzeuge.
    void setSourceSize(QSize px);
    [[nodiscard]] QSize sourceSize() const { return source_; }
    /// Neues Bild. drawRect: wohin es im Widget gehört (logische Pixel); bei Einpassen das
    /// ganze Bild, in der Detailansicht der sichtbare Ausschnitt. Das Bild hat bereits die
    /// Pixelgröße von drawRect × Gerätepixelverhältnis.
    void setImage(const QImage& img, const QRectF& drawRect);
    void setClipping(const QImage& mask, bool on);
    [[nodiscard]] const QImage& image() const { return image_; }

    void setZoom(Zoom z);
    [[nodiscard]] Zoom zoom() const;
    /// Vergrößerung: Gerätepixel je Quellpixel (1 = 100 %); bei Einpassen der passende Wert.
    [[nodiscard]] double scale() const;
    /// Vergrößerung setzen (Einpassen … 800 %); anchor (Widget-Pixel) bleibt an derselben
    /// Bildstelle. Werte bis zum Einpassen schalten auf Einpassen (kleiner als das Fenster nicht).
    void setScale(double s, std::optional<QPointF> anchor = std::nullopt);
    void zoomBy(double factor, std::optional<QPointF> anchor = std::nullopt) { setScale(scale() * factor, anchor); }
    [[nodiscard]] double fitScale() const;
    /// Maustaste zum Verschieben (Einstellung); ist ein Werkzeug aktiv und die Taste links,
    /// verschiebt die mittlere Taste.
    void setPanButton(Qt::MouseButton b) { panButton_ = b; }
    [[nodiscard]] Qt::MouseButton panButton() const { return panButton_; }
    /// Mittelpunkt des sichtbaren Ausschnitts (normiert).
    [[nodiscard]] QPointF center() const { return center_; }
    void setCenter(QPointF c);
    /// Lage des ganzen Bildes im Widget beim aktuellen Zoom.
    [[nodiscard]] ViewMapping mapping() const;
    /// Sichtbarer Ausschnitt in normierten Koordinaten.
    [[nodiscard]] QRectF visibleNorm() const;

    void setTool(ICanvasTool* t);
    [[nodiscard]] ICanvasTool* tool() const { return tool_; }

Q_SIGNALS:
    void viewChanged();
    void zoomChanged(); ///< Vergrößerung geändert (Anzeige in der Statusleiste)
    void picked(QPointF norm); ///< Klick ohne Werkzeug (Pipette)

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* e) override;

private:
    class Overlay;
    friend class Overlay;
    void overlayPaint(QPainter& p);
    /// Lage des aktuellen Bilds im Widget bei der jetzigen Ansicht.
    [[nodiscard]] QRectF currentRect() const;
    /// Ansicht seit dem Bild geändert (Zoom/Verschieben): Bild skaliert zeichnen statt 1:1.
    [[nodiscard]] bool transformed() const;
    void refresh();
    void clampCenter();
    bool overlayMouse(QMouseEvent* e);
    bool overlayWheel(QWheelEvent* e);
    [[nodiscard]] Qt::MouseButton effectivePanButton() const;

    Overlay* overlay_ = nullptr;
    QImage image_;
    QImage clipping_;
    bool showClipping_ = false;
    QRectF drawRect_;
    QRectF imageNorm_; ///< Teil des Quellbilds, den image_ zeigt (normiert)
    QSize source_{3, 2};
    bool fit_ = true;
    double scale_ = 1.0; ///< ohne Einpassen: Gerätepixel je Quellpixel
    QPointF center_{0.5, 0.5};
    ICanvasTool* tool_ = nullptr;
    std::unique_ptr<QRhiTexture> texture_;
    bool dirty_ = true;
    Qt::MouseButton panButton_ = Qt::RightButton; ///< Standard: rechts ziehen (Werkzeuge behalten links)
    std::optional<QPointF> panFrom_;
    Qt::MouseButton panWith_ = Qt::NoButton;
    QPointF panCenter_;
};

} // namespace autoneg::ui
