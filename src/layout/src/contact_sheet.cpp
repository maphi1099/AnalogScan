#include "autoneg/layout/contact_sheet.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace autoneg::layout {

namespace {

constexpr double mmPerPt = 25.4 / 72.0;
constexpr double lineFactor = 1.4; ///< Zeilenhöhe = Schriftgröße × 1,4
constexpr double rowGapMm = 3.0;   ///< Abstand zwischen zwei Streifen
constexpr double cutGapMm = 2.0;   ///< Abstand zwischen zugeschnittenen Bildern

double lineMm(double pt)
{
    return pt * mmPerPt * lineFactor;
}

std::string headerLine(const SheetHeader& h)
{
    std::string out;
    const auto add = [&out](const std::string& label, const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (!out.empty()) {
            out += "  ·  ";
        }
        out += label + value;
    };
    add("Rolle ", h.rollNo);
    add("", h.film.filmName);
    add("ISO/EI ", h.film.iso > 0 ? std::to_string(h.film.iso) : std::string());
    add("Entwicklung: ", h.film.developer);
    add("", h.film.shotDate);
    add("Kamera: ", h.film.camera);
    return out;
}

std::string notesLine(const SheetHeader& h)
{
    if (!h.notes.empty() && !h.film.notes.empty()) {
        return h.notes + "  ·  " + h.film.notes;
    }
    return h.notes.empty() ? h.film.notes : h.notes;
}

} // namespace

SizeMm paperSize(PaperSize p) noexcept
{
    switch (p) {
    case PaperSize::A4:
        return {210.0, 297.0};
    case PaperSize::A3:
        return {297.0, 420.0};
    case PaperSize::Letter:
        return {215.9, 279.4};
    }
    return {210.0, 297.0};
}

std::span<const FilmFormat> FilmFormat::catalog()
{
    // Bildfeld in Laufrichtung × quer, Bildabstand, Filmbreite, Bilder je Streifen gängiger
    // Negativhüllen (Kleinbild 6×6 Streifen, Rollfilm 120: 6×4,5 4, 6×6 3, 6×7 3, 6×9 2)
    static const std::array<FilmFormat, 7> formats{{
        {"Kleinbild 24×36", {36.0, 24.0}, 38.0, 35.0, 6},
        {"Halbformat 18×24", {18.0, 24.0}, 19.0, 35.0, 12},
        {"Panorama 24×65", {65.0, 24.0}, 67.0, 35.0, 3},
        {"Mittelformat 6×4,5", {41.5, 56.0}, 45.0, 61.5, 4},
        {"Mittelformat 6×6", {56.0, 56.0}, 60.0, 61.5, 3},
        {"Mittelformat 6×7", {69.5, 56.0}, 72.0, 61.5, 3},
        {"Mittelformat 6×9", {84.0, 56.0}, 88.0, 61.5, 2},
    }};
    return formats;
}

const FilmFormat* FilmFormat::find(std::string_view name)
{
    const auto c = catalog();
    const auto it = std::ranges::find(c, name, &FilmFormat::name);
    return it != c.end() ? &*it : nullptr;
}

std::vector<PageLayout> ContactSheetLayouter::layout(const ContactSheetSpec& s,
                                                     std::span<const SheetFrame> frames,
                                                     const FilmFormat& fmt) const
{
    SizeMm page = paperSize(s.paper);
    if (s.landscape) {
        std::swap(page.w, page.h);
    }
    const int per = s.framesPerStrip > 0 ? s.framesPerStrip : std::max(1, fmt.perStrip);
    const double headerPt = std::max(7.0, s.headerPt);
    const double numberPt = std::max(7.0, s.numberPt);

    // Druckbereich: links der Heftrand bzw. der Rand, je nachdem was größer ist (LF-KTA-07)
    const double left = std::max(s.marginsMm.left, s.bindingMm);
    const double right = page.w - s.marginsMm.right;
    const double top = s.marginsMm.top;
    const double bottom = page.h - s.marginsMm.bottom;
    const double headerH = 2.0 * lineMm(headerPt) + 3.0;
    const double labelH = lineMm(numberPt);
    const double availW = right - left;
    const double availH = bottom - (top + headerH);

    // Streifen ungeschnitten (mit Filmrand) oder als Folge der Zuschnitte
    const double cell = s.showRebate ? fmt.pitchMm : fmt.frameMm.w + cutGapMm;
    const double stripL = per * cell;
    const double stripH = s.showRebate ? fmt.filmWidthMm : fmt.frameMm.h;
    const auto rowFor = [&](double scale) { return stripH * scale + labelH + rowGapMm; };

    double fit = availW / stripL;
    if (rowFor(fit) > availH) {
        fit = (availH - labelH - rowGapMm) / stripH;
    }
    double scale = fit;
    std::optional<std::string> hint;
    if (s.scale == SheetScale::OneToOne) {
        if (stripL <= availW && rowFor(1.0) <= availH) {
            scale = 1.0;
        } else {
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "Maßstab 1:1 passt nicht auf das Seitenformat; verkleinert auf %.0f %%",
                          100.0 * fit);
            hint = buf;
        }
    }
    const int rowsPerPage = std::max(1, static_cast<int>(std::floor((availH + rowGapMm) / rowFor(scale))));
    const int strips = (static_cast<int>(frames.size()) + per - 1) / per;
    const int pages = std::max(1, (strips + rowsPerPage - 1) / rowsPerPage);
    const double x0 = left + (availW - stripL * scale) / 2.0;
    const double frameAspect = fmt.frameMm.w / fmt.frameMm.h;

    std::vector<PageLayout> out;
    for (int p = 0; p < pages; ++p) {
        PageLayout pl;
        pl.sizeMm = page;
        pl.background = s.background;
        pl.pageNo = p + 1;
        pl.pageCount = pages;
        pl.scale = scale;
        pl.hint = hint;
        // Kopfzeile und Seitenzähler
        const double counterW = 30.0;
        pl.elements.emplace_back(
            TextBox{headerLine(s.header), {left, top, availW - counterW, lineMm(headerPt)}, headerPt});
        pl.elements.emplace_back(TextBox{"Seite " + std::to_string(p + 1) + "/" + std::to_string(pages),
                                         {right - counterW, top, counterW, lineMm(headerPt)},
                                         headerPt,
                                         TextAlign::Right});
        if (const std::string notes = notesLine(s.header); !notes.empty()) {
            pl.elements.emplace_back(
                TextBox{notes, {left, top + lineMm(headerPt), availW, lineMm(headerPt)}, headerPt});
        }
        if (s.punchMarks) { // zwei Löcher im Abstand 80 mm, mittig im Heftrand
            const double cx = s.bindingMm / 2.0;
            pl.elements.emplace_back(PunchMark{{cx, page.h / 2.0 - 40.0}});
            pl.elements.emplace_back(PunchMark{{cx, page.h / 2.0 + 40.0}});
        }
        for (int r = 0; r < rowsPerPage; ++r) {
            const int strip = p * rowsPerPage + r;
            if (strip >= strips) {
                break;
            }
            const double y = top + headerH + r * rowFor(scale);
            for (int j = 0; j < per; ++j) {
                const std::size_t k = static_cast<std::size_t>(strip * per + j);
                if (k >= frames.size()) {
                    break;
                }
                const SheetFrame& f = frames[k];
                const double cellX = x0 + j * cell * scale;
                ImageSlot slot;
                slot.frame = f.id;
                if (s.showRebate) {
                    slot.rectMm = {cellX, y, cell * scale, stripH * scale}; // ungeschnittenes Feld
                    slot.withRebate = true;
                } else {
                    // Zuschnitt im Bildfeld; Hochformat-Bilder liegen gedreht wie auf dem Film
                    slot.rotated = (f.aspect < 1.0) != (frameAspect < 1.0);
                    const double aspect = slot.rotated ? 1.0 / f.aspect : f.aspect;
                    const double boxW = fmt.frameMm.w * scale;
                    const double boxH = fmt.frameMm.h * scale;
                    double w = boxW;
                    double h = boxW / aspect;
                    if (h > boxH) {
                        h = boxH;
                        w = boxH * aspect;
                    }
                    slot.rectMm = {cellX + cutGapMm * scale / 2.0 + (boxW - w) / 2.0, y + (boxH - h) / 2.0, w,
                                   h};
                }
                pl.elements.emplace_back(slot);
                pl.elements.emplace_back(TextBox{std::to_string(f.number),
                                                 {cellX, y + stripH * scale, cell * scale, labelH},
                                                 numberPt,
                                                 TextAlign::Center});
            }
        }
        out.push_back(std::move(pl));
    }
    return out;
}

} // namespace autoneg::layout
