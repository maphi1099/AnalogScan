#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QThread>
#include <QtGui/QColorSpace>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPageLayout>
#include <QtGui/QPageSize>
#include <QtGui/QPainter>
#include <QtGui/QPdfOutputIntent>
#include <QtGui/QPdfWriter>
#include <QtGui/QTransform>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrinter>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>
#include <variant>

namespace autoneg::adapter::qtprint {

namespace {

constexpr double mmPerInch = 25.4;

/// Anwendungsinstanz mit GUI-Teil (QPdfWriter braucht Schriften und QGuiApplication).
bool haveGuiApp()
{
    return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;
}

QPageLayout pageLayout(const layout::SizeMm& size)
{
    // Größe in mm bestimmt die Ausrichtung; ohne Ränder, gezeichnet wird auf die ganze Seite
    const QPageSize ps(QSizeF(size.w, size.h), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    QPageLayout l(ps, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    l.setMode(QPageLayout::FullPageMode);
    return l;
}

std::uint8_t to8(float v)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
}

/// ImageBuffer (Werte 0…1 im Ausgabefarbraum) als 8-Bit-QImage mit Farbraum.
base::Expected<QImage> toQImage(const image::ImageBuffer& b, const QColorSpace& cs)
{
    const auto& spec = b.spec();
    const std::span<const float> px = b.host();
    if (px.empty() || spec.width <= 0 || spec.height <= 0 || spec.channels < 1) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Bild für den Kontaktabzug ist leer oder nicht im Host-Speicher");
    }
    const bool gray = spec.channels < 3;
    QImage img(spec.width, spec.height, gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    const auto ch = static_cast<std::size_t>(spec.channels);
    for (int y = 0; y < spec.height; ++y) {
        auto* line = img.scanLine(y);
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(spec.width) * ch;
        for (int x = 0; x < spec.width; ++x) {
            const std::size_t i = row + static_cast<std::size_t>(x) * ch;
            if (gray) {
                line[x] = to8(px[i]);
            } else {
                line[3 * x] = to8(px[i]);
                line[3 * x + 1] = to8(px[i + 1]);
                line[3 * x + 2] = to8(px[i + 2]);
            }
        }
    }
    // Graubilder nutzen das Profil nur, wenn es ein Grauprofil ist
    if (cs.isValid() && (gray == (cs.colorModel() == QColorSpace::ColorModel::Gray))) {
        img.setColorSpace(cs);
    }
    return img;
}

/// Bild für den Slot: gedreht, mittig auf das Seitenverhältnis des Slots beschnitten und
/// auf die Pixelzahl bei der Auflösung der Seite gebracht (keine Verzerrung, mindestens dpi).
QImage fitToSlot(QImage img, const layout::ImageSlot& slot, int dpi)
{
    if (slot.rotated) {
        img = img.transformed(QTransform().rotate(90.0), Qt::SmoothTransformation);
    }
    const int wantW = std::max(1, static_cast<int>(std::ceil(slot.rectMm.w / mmPerInch * dpi)));
    const int wantH = std::max(1, static_cast<int>(std::ceil(slot.rectMm.h / mmPerInch * dpi)));
    const double slotAspect = static_cast<double>(wantW) / wantH;
    const double imgAspect = static_cast<double>(img.width()) / img.height();
    QRect src = img.rect();
    if (imgAspect > slotAspect) {
        const int w = std::max(1, static_cast<int>(std::lround(img.height() * slotAspect)));
        src = QRect((img.width() - w) / 2, 0, w, img.height());
    } else if (imgAspect < slotAspect) {
        const int h = std::max(1, static_cast<int>(std::lround(img.width() / slotAspect)));
        src = QRect(0, (img.height() - h) / 2, img.width(), h);
    }
    const QColorSpace cs = img.colorSpace();
    QImage out = img.copy(src).scaled(wantW, wantH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (cs.isValid()) {
        out.setColorSpace(cs);
    }
    return out;
}

Qt::Alignment alignment(layout::TextAlign a)
{
    switch (a) {
    case layout::TextAlign::Center:
        return Qt::AlignHCenter | Qt::AlignVCenter;
    case layout::TextAlign::Right:
        return Qt::AlignRight | Qt::AlignVCenter;
    case layout::TextAlign::Left:
        break;
    }
    return Qt::AlignLeft | Qt::AlignVCenter;
}

/// Zeichnet eine Seite in Millimeter-Koordinaten auf das Gerät des Painters.
base::Expected<void> paintPage(QPainter& painter, const ports::RenderedPage& page, const QColorSpace& cs)
{
    const double k = painter.device()->logicalDpiX() / mmPerInch; // Geräteeinheiten je mm
    const auto rect = [k](const layout::RectMm& r) { return QRectF(r.x * k, r.y * k, r.w * k, r.h * k); };
    const bool black = page.layout.background == layout::SheetBackground::Black;
    const QColor ink = black ? Qt::white : Qt::black;
    painter.fillRect(QRectF(0, 0, page.layout.sizeMm.w * k, page.layout.sizeMm.h * k),
                     black ? Qt::black : Qt::white);
    for (const layout::LayoutElement& e : page.layout.elements) {
        if (const auto* slot = std::get_if<layout::ImageSlot>(&e)) {
            const auto it = page.images.find(slot->frame);
            if (it == page.images.end()) {
                continue; // Bild fehlt: Slot bleibt leer (Hintergrund)
            }
            auto img = toQImage(it->second, cs);
            if (!img) {
                return std::unexpected(img.error());
            }
            painter.drawImage(rect(slot->rectMm), fitToSlot(std::move(*img), *slot, page.dpi));
        } else if (const auto* text = std::get_if<layout::TextBox>(&e)) {
            QFont f = painter.font();
            f.setPointSizeF(text->pt);
            painter.setFont(f);
            painter.setPen(ink);
            painter.drawText(rect(text->rectMm),
                             static_cast<int>(alignment(text->align) | Qt::TextSingleLine),
                             QString::fromStdString(text->text));
        } else if (const auto* mark = std::get_if<layout::PunchMark>(&e)) {
            QPen pen(Qt::gray);
            pen.setWidthF(0.2 * k);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            const double r = mark->diameterMm / 2.0;
            painter.drawEllipse(QPointF(mark->centerMm.x * k, mark->centerMm.y * k), r * k, r * k);
        }
    }
    return {};
}

QColorSpace colorSpaceOf(const ports::IccProfile& p)
{
    const QByteArray icc(reinterpret_cast<const char*>(p.bytes.data()),
                         static_cast<qsizetype>(p.bytes.size()));
    return QColorSpace::fromIccProfile(icc);
}

QString qpath(const std::filesystem::path& p)
{
    // UTF-16 unter Windows, UTF-8 unter Linux: beides verlustfrei (plattformabhängig, PROGRESS.md)
    return QString::fromStdU16String(p.u16string());
}

/// Alle Seiten nacheinander; newPage(i) schaltet vor Seite i ≥ 1 um.
template <class NewPage>
base::Expected<void> paintAll(QPainter& painter, std::span<const ports::RenderedPage> pages,
                              const QColorSpace& cs, NewPage&& newPage)
{
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (i > 0 && !newPage(pages[i])) {
            return base::makeError(base::ErrorCode::Io, "neue Seite konnte nicht begonnen werden");
        }
        if (auto r = paintPage(painter, pages[i], cs); !r) {
            return r;
        }
    }
    return {};
}

} // namespace

struct QtGuiSession::Impl
{
    int argc = 1;
    char arg0[8] = "autoneg";
    char* argv[2] = {arg0, nullptr};
    std::unique_ptr<QGuiApplication> app;
};

QtGuiSession::QtGuiSession()
    : impl_(std::make_unique<Impl>())
{
    if (QCoreApplication::instance() == nullptr) {
#if !defined(_WIN32)
        // Linux ohne Anzeige (Kommandozeile, Tests): offscreen mit Schriften über fontconfig.
        // Unter Windows bleibt die native Plattform; sie braucht kein Fenster und findet die
        // Systemschriften (offscreen hätte dort keine). Plattformabhängig, PROGRESS.md.
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsSet("DISPLAY") &&
            !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
#endif
        impl_->app = std::make_unique<QGuiApplication>(impl_->argc, impl_->argv);
    }
}

QtGuiSession::~QtGuiSession() = default;

base::Expected<void> QtDocumentRenderer::writePdf(std::span<const ports::RenderedPage> pages,
                                                  const std::filesystem::path& p)
{
    if (!haveGuiApp()) {
        return base::makeError(base::ErrorCode::Internal, "keine Qt-Anwendungsinstanz (QtGuiSession fehlt)");
    }
    if (pages.empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Kontaktabzug ohne Seiten");
    }
    const QColorSpace cs = colorSpaceOf(pages.front().outputProfile);
    if (!cs.isValid()) {
        return base::makeError(base::ErrorCode::InvalidArgument,
                               "Ausgabeprofil ist kein gültiges ICC-Profil");
    }
    QPdfWriter writer(qpath(p));
    writer.setResolution(pages.front().dpi);
    writer.setCreator(QStringLiteral("AutoNeg"));
    writer.setTitle(QStringLiteral("Kontaktabzug"));
    // Ausgabeprofil einbetten (LF-KTA-06): Output Intent nach PDF/X-4, Bilder mit ICC-Farbraum
    writer.setPdfVersion(QPagedPaintDevice::PdfVersion_X4);
    QPdfOutputIntent intent;
    intent.setOutputConditionIdentifier(QString::fromStdString(pages.front().outputProfile.name));
    intent.setOutputCondition(QString::fromStdString(pages.front().outputProfile.name));
    intent.setOutputProfile(cs);
    writer.setOutputIntent(intent);
    if (!writer.setPageLayout(pageLayout(pages.front().layout.sizeMm))) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Seitengröße wird nicht unterstützt");
    }
    QPainter painter;
    if (!painter.begin(&writer)) {
        return base::makeError(base::ErrorCode::Io, "PDF kann nicht geschrieben werden: " + p.string());
    }
    auto r = paintAll(painter, pages, cs, [&writer](const ports::RenderedPage& page) {
        writer.setPageLayout(pageLayout(page.layout.sizeMm));
        return writer.newPage();
    });
    if (!painter.end() && r) {
        return base::makeError(base::ErrorCode::Io, "PDF unvollständig: " + p.string());
    }
    return r;
}

base::Expected<void> QtDocumentRenderer::print(std::span<const ports::RenderedPage> pages,
                                               const ports::PrintSettings& s)
{
    if (!haveGuiApp()) {
        return base::makeError(base::ErrorCode::Internal, "keine Qt-Anwendungsinstanz (QtGuiSession fehlt)");
    }
    if (pages.empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Kontaktabzug ohne Seiten");
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    printer.setCopyCount(std::max(1, s.copies));
    printer.setPageLayout(pageLayout(pages.front().layout.sizeMm));
    printer.setDocName(QStringLiteral("Kontaktabzug"));
    if (s.showDialog) {
        // Der Systemdruckdialog braucht QApplication und läuft im Thread der Anwendung; der
        // aufrufende Job wartet (die Ereignisschleife des GUI-Programms muss laufen).
        if (qobject_cast<QApplication*>(QCoreApplication::instance()) == nullptr) {
            return base::makeError(base::ErrorCode::Unsupported, "Druckdialog nur im GUI-Programm verfügbar");
        }
        bool accepted = false;
        const auto ask = [&printer, &accepted] {
            QPrintDialog dialog(&printer);
            accepted = dialog.exec() == QDialog::Accepted;
        };
        QCoreApplication* app = QCoreApplication::instance();
        if (QThread::currentThread() == app->thread()) {
            ask();
        } else {
            QMetaObject::invokeMethod(app, ask, Qt::BlockingQueuedConnection);
        }
        if (!accepted) {
            return base::makeError(base::ErrorCode::Cancelled, "Druck abgebrochen");
        }
    }
    if (!printer.isValid()) {
        return base::makeError(base::ErrorCode::Unsupported, "kein Drucker verfügbar");
    }
    const QColorSpace cs = colorSpaceOf(pages.front().outputProfile);
    QPainter painter;
    if (!painter.begin(&printer)) {
        return base::makeError(base::ErrorCode::Io, "Druckauftrag kann nicht begonnen werden");
    }
    auto r = paintAll(painter, pages, cs, [&printer](const ports::RenderedPage& page) {
        printer.setPageLayout(pageLayout(page.layout.sizeMm));
        return printer.newPage();
    });
    if (!painter.end() && r) {
        return base::makeError(base::ErrorCode::Io, "Druckauftrag unvollständig");
    }
    return r;
}

} // namespace autoneg::adapter::qtprint
