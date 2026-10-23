#pragma once

#include "autoneg/ui/canvas_tools.hpp"

#include <QtGui/QImage>
#include <QtWidgets/QRhiWidget>

#include <memory>
#include <optional>

class QRhiTexture;

namespace autoneg::ui {

/// Arbeitsfläche (Architektur 9.14, LF-UI-02): zeigt die fertige Vorschau als Textur.
///
/// Die Vorschau kommt aus dem Render-Thread bereits in Monitorgröße und -farbraum; die
/// Fläche kopiert sie nur in ihr Ziel (ohne Shader, ohne Rechnen im UI-Thread). Zoom-Stufen:
/// Einpassen, 100 %, 200 % mit verschiebbarem Ausschnitt; jede Änderung meldet viewChanged,
/// worauf das Hauptfenster eine passende Vorschau bzw. Detailansicht anfordert.
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
        Double  ///< 200 %
    };

    explicit ImageCanvas(QWidget* parent = nullptr);
    ~ImageCanvas() override;

    /// Größe des Quellbildes in Pixeln (nach Geometrie: Drehung, Zuschnitt), für Zoom und Werkzeuge.
    void setSourceSize(QSize px);
    /// Neues Bild. drawRect: wohin es im Widget gehört (logische Pixel); bei Einpassen das
    /// ganze Bild, in der Detailansicht der sichtbare Ausschnitt. Das Bild hat bereits die
    /// Pixelgröße von drawRect × Gerätepixelverhältnis.
    void setImage(const QImage& img, const QRectF& drawRect);
    void setClipping(const QImage& mask, bool on);
    [[nodiscard]] const QImage& image() const { return image_; }

    void setZoom(Zoom z);
    [[nodiscard]] Zoom zoom() const { return zoom_; }
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
    bool overlayMouse(QMouseEvent* e);

    Overlay* overlay_ = nullptr;
    QImage image_;
    QImage clipping_;
    bool showClipping_ = false;
    QRectF drawRect_;
    QSize source_{3, 2};
    Zoom zoom_ = Zoom::Fit;
    QPointF center_{0.5, 0.5};
    ICanvasTool* tool_ = nullptr;
    std::unique_ptr<QRhiTexture> texture_;
    bool dirty_ = true;
    std::optional<QPointF> panFrom_;
    QPointF panCenter_;
};

} // namespace autoneg::ui
