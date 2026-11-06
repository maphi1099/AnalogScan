// Sichttest (Label visual): die echte Anwendung auf der echten Grafikschnittstelle. Die anderen
// E2E-Tests laufen offscreen im QRhi-Null-Backend und sehen nicht, was gezeichnet wird; so blieb
// der Fehler "Bild nach Auto-Bildfeld oben bündig" unbemerkt. Braucht einen Bildschirm.

#include "e2e_fixture.hpp"
#include "ui/image_canvas.hpp"

#include <QtGui/QImage>

#include <cstdlib>

namespace autoneg {
namespace {

/// Bereich des gezeichneten Bildes: alle Pixel, die sich deutlich vom Hintergrund (Ecke) abheben.
QRect drawnArea(const QImage& frame)
{
    const QColor bg = frame.pixelColor(0, 0);
    const auto differs = [&bg](const QColor& c) {
        return std::abs(c.red() - bg.red()) + std::abs(c.green() - bg.green()) +
                   std::abs(c.blue() - bg.blue()) >
               24;
    };
    QRect area;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            if (differs(frame.pixelColor(x, y))) {
                area |= QRect(x, y, 1, 1);
            }
        }
    }
    return area;
}

class Visual : public E2E
{
protected:
    void SetUp() override
    {
#if !defined(_WIN32)
        if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
            GTEST_SKIP() << "kein Bildschirm";
        }
#endif
    }
    /// Bild nach der nächsten Vorschau, wie es auf dem Bildschirm steht.
    [[nodiscard]] QImage shown()
    {
        QApplication::processEvents();
        QTest::qWait(300); // Zeichnen abwarten
        return window->canvas()->grabFramebuffer();
    }
    /// Gezeichnetes Bild liegt mittig in der Bildfläche und hat die Größe des Feldes.
    void expectCentered(const QImage& frame)
    {
        ASSERT_FALSE(frame.isNull());
        const QRect area = drawnArea(frame);
        ASSERT_FALSE(area.isEmpty()) << "nichts gezeichnet";
        const double dpr = window->canvas()->devicePixelRatioF();
        const QRectF expected = window->canvas()->mapping().imageRect;
        EXPECT_NEAR(area.center().x(), frame.width() / 2.0, 3.0 * dpr);
        EXPECT_NEAR(area.center().y(), frame.height() / 2.0, 3.0 * dpr);
        EXPECT_NEAR(area.width(), expected.width() * dpr, 4.0 * dpr);
        EXPECT_NEAR(area.height(), expected.height() * dpr, 4.0 * dpr);
    }
};

// LF-UI-01, LF-GEO-01: das Bild ist sichtbar, füllt das Feld und liegt mittig, bei jedem
// Zuschnitt (die Ränder des künstlichen Scans haben fast die Hintergrundfarbe: ohne Halter prüfen)
TEST_F(Visual, M10_ImageVisibleAndCentered)
{
    start(1);
    for (const model::NormRect& r :
         {model::NormRect{0.0, 0.1, 1.0, 0.8}, model::NormRect{0.1, 0.2, 0.8, 0.6}}) {
        const auto before = window->previewsShown();
        model::EditEntry crop;
        crop.step = model::StepId("autoneg.crop");
        crop.params = model::ParamSet{}.with("rect", r);
        ASSERT_TRUE(window->controller().apply(std::make_unique<application::SetEntryCommand>(crop)));
        ASSERT_TRUE(waitFor([&] { return window->previewsShown() > before; }));
        expectCentered(shown());
    }
}

} // namespace
} // namespace autoneg
