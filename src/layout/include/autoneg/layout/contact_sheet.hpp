#pragma once

#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace autoneg::layout {

// Kontaktabzug als reine Geometrie in Millimetern (Architektur 9.8, LF-KTA). Bilder und
// Druck ergänzt der ContactSheetService über IDocumentRenderer.

struct SizeMm
{
    double w = 0.0;
    double h = 0.0;
    friend bool operator==(const SizeMm&, const SizeMm&) = default;
};

struct PointMm
{
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const PointMm&, const PointMm&) = default;
};

struct RectMm
{
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    [[nodiscard]] double right() const noexcept { return x + w; }
    [[nodiscard]] double bottom() const noexcept { return y + h; }
    friend bool operator==(const RectMm&, const RectMm&) = default;
};

struct Margins
{
    double left = 10.0;
    double top = 10.0;
    double right = 10.0;
    double bottom = 10.0;
    friend bool operator==(const Margins&, const Margins&) = default;
};

enum class PaperSize
{
    A4,
    A3,
    Letter
};

/// Hochformat-Abmessungen des Papiers.
[[nodiscard]] SizeMm paperSize(PaperSize p) noexcept;

/// Filmformat mit Voreinstellung für gängige Negativhüllen (LF-KTA-02, Mengengerüst).
/// Längen in Laufrichtung des Films (along) bzw. quer dazu (across).
struct FilmFormat
{
    std::string name;
    SizeMm frameMm;         ///< Bildfeld: w = in Laufrichtung, h = quer
    double pitchMm = 0.0;   ///< Bildabstand in Laufrichtung (Bildfeld + Steg)
    double filmWidthMm = 0; ///< Filmbreite (Kleinbild 35, Rollfilm 61,5)
    int perStrip = 6;       ///< Bilder je Streifen (Negativhülle)

    /// Alle Formate des Mengengerüsts: Kleinbild, Halbformat, Panorama, 6×4,5, 6×6, 6×7, 6×9.
    [[nodiscard]] static std::span<const FilmFormat> catalog();
    [[nodiscard]] static const FilmFormat* find(std::string_view name);

    friend bool operator==(const FilmFormat&, const FilmFormat&) = default;
};

/// Ein Bild auf dem Bogen.
struct SheetFrame
{
    model::FrameId id;
    int number = 0;      ///< Bildnummer (Aufnahmereihenfolge)
    double aspect = 1.5; ///< Seitenverhältnis des Zuschnitts (Breite / Höhe)
};

enum class SheetContent
{
    AsEdited, ///< aktuelle Einstellungen je Bild
    Uniform   ///< gleiche Belichtung und Gradation für alle (LF-KTA-04)
};

enum class SheetBackground
{
    Black,
    White
};

enum class SheetScale
{
    Fit,     ///< Streifen füllen die Breite
    OneToOne ///< Originalgröße des Negativs, wenn es passt (LF-KTA-08)
};

/// Kopfzeile (LF-KTA-05).
struct SheetHeader
{
    std::string rollNo;
    model::FilmInfo film;
    std::string notes;
};

struct ContactSheetSpec
{
    PaperSize paper = PaperSize::A4;
    bool landscape = false;
    Margins marginsMm;
    double bindingMm = 25.0; ///< Heftrand an der Bindungsseite (links), LF-KTA-07
    bool punchMarks = false; ///< Lochmarkierung im Heftrand
    int framesPerStrip = 0;  ///< 0 = Voreinstellung des Filmformats
    bool showRebate = true;  ///< mit Filmrand (ungeschnittenes Bildfeld), LF-KTA-03
    SheetContent content = SheetContent::AsEdited;
    SheetBackground background = SheetBackground::Black;
    SheetScale scale = SheetScale::Fit;
    int dpi = 300;
    SheetHeader header;
    double headerPt = 9.0; ///< Schriftgröße Kopfzeile (mindestens 7 pt)
    double numberPt = 7.0; ///< Schriftgröße Bildnummer (mindestens 7 pt)
};

/// Platz für ein Bild. rotated: das Bild ist für den Druck um 90° zu drehen.
struct ImageSlot
{
    model::FrameId frame;
    RectMm rectMm;
    bool rotated = false;
    bool withRebate = false; ///< Slot zeigt das ungeschnittene Feld mit Filmrand
};

enum class TextAlign
{
    Left,
    Center,
    Right
};

struct TextBox
{
    std::string text;
    RectMm rectMm;
    double pt = 9.0;
    TextAlign align = TextAlign::Left;
};

struct PunchMark
{
    PointMm centerMm;
    double diameterMm = 6.0;
};

using LayoutElement = std::variant<ImageSlot, TextBox, PunchMark>;

/// Eine Seite des Kontaktabzugs.
struct PageLayout
{
    SizeMm sizeMm;
    SheetBackground background = SheetBackground::Black;
    std::vector<LayoutElement> elements;
    int pageNo = 1;
    int pageCount = 1;
    double scale = 1.0;              ///< Maßstab gegenüber dem Negativ
    std::optional<std::string> hint; ///< z. B. "1:1 passt nicht, verkleinert" (LF-KTA-08)
};

/// Ordnet die Bilder einer Rolle wie in der Negativhülle an (LF-KTA-01…10).
///
/// Bilder in Aufnahmereihenfolge, Streifen zeilenweise; Kopfzeile mit Rolle, Film, ISO/EI,
/// Entwicklung, Datum, Kamera und Notiz; Bildnummer unter jedem Feld; nichts im Heftrand
/// außer den Lochmarken; bei Bedarf mehrere Seiten mit Seitenzähler.
class ContactSheetLayouter
{
public:
    [[nodiscard]] std::vector<PageLayout>
    layout(const ContactSheetSpec& s, std::span<const SheetFrame> frames, const FilmFormat& fmt) const;
};

} // namespace autoneg::layout
