// Unit-Tests Kontaktabzug-Layout (Architektur 9.8, LF-KTA-01…10). Reine Geometrie.

#include "autoneg/layout/contact_sheet.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace autoneg::layout {
namespace {

std::vector<SheetFrame> frames(int n, double aspect = 1.5)
{
    std::vector<SheetFrame> out;
    for (int i = 1; i <= n; ++i) {
        std::array<std::uint8_t, 16> b{};
        b[15] = static_cast<std::uint8_t>(i);
        b[14] = static_cast<std::uint8_t>(i >> 8);
        out.push_back({model::FrameId(b), i, aspect});
    }
    return out;
}

const FilmFormat& kb()
{
    return *FilmFormat::find("Kleinbild 24×36");
}

template <typename T>
std::vector<T> elementsOf(const PageLayout& p)
{
    std::vector<T> out;
    for (const LayoutElement& e : p.elements) {
        if (const T* x = std::get_if<T>(&e)) {
            out.push_back(*x);
        }
    }
    return out;
}

int numberOf(const model::FrameId& id)
{
    return id.bytes()[15] + 256 * id.bytes()[14];
}

// LF-KTA-01
TEST(ContactSheet, U_LAY_01_StripsInShootingOrder)
{
    const auto pages = ContactSheetLayouter{}.layout({}, frames(36), kb());
    ASSERT_EQ(pages.size(), 1U);
    const auto slots = elementsOf<ImageSlot>(pages[0]);
    ASSERT_EQ(slots.size(), 36U);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        EXPECT_EQ(numberOf(slots[i].frame), static_cast<int>(i) + 1);
        const std::size_t strip = i / 6;
        const std::size_t col = i % 6;
        EXPECT_NEAR(slots[i].rectMm.y, slots[strip * 6].rectMm.y, 1e-9); // gleiche Zeile
        if (col > 0) {
            EXPECT_GT(slots[i].rectMm.x, slots[i - 1].rectMm.x); // links nach rechts
        }
        if (strip > 0 && col == 0) {
            EXPECT_GT(slots[i].rectMm.y, slots[i - 6].rectMm.y); // nächster Streifen darunter
        }
    }
    std::set<double> rows;
    for (const ImageSlot& s : slots) {
        rows.insert(s.rectMm.y);
    }
    EXPECT_EQ(rows.size(), 6U);
}

// LF-KTA-02
TEST(ContactSheet, U_LAY_02_FormatCatalog)
{
    for (const char* name : {"Kleinbild 24×36", "Halbformat 18×24", "Panorama 24×65", "Mittelformat 6×4,5",
                             "Mittelformat 6×6", "Mittelformat 6×7", "Mittelformat 6×9"}) {
        const FilmFormat* f = FilmFormat::find(name);
        ASSERT_NE(f, nullptr) << name;
        EXPECT_GT(f->perStrip, 0);
        EXPECT_GE(f->pitchMm, f->frameMm.w);
        EXPECT_GE(f->filmWidthMm, f->frameMm.h);
    }
    EXPECT_EQ(kb().perStrip, 6);
    EXPECT_EQ(FilmFormat::find("Mittelformat 6×6")->perStrip, 3);
    // frei änderbar
    ContactSheetSpec s;
    s.framesPerStrip = 4;
    const auto slots = elementsOf<ImageSlot>(ContactSheetLayouter{}.layout(s, frames(8), kb())[0]);
    EXPECT_NEAR(slots[3].rectMm.y, slots[0].rectMm.y, 1e-9);
    EXPECT_GT(slots[4].rectMm.y, slots[0].rectMm.y);
}

// LF-KTA-03
TEST(ContactSheet, U_LAY_03_WithAndWithoutRebate)
{
    ContactSheetSpec s;
    s.scale = SheetScale::OneToOne;
    s.landscape = true;
    s.showRebate = true;
    const auto with = ContactSheetLayouter{}.layout(s, frames(6), kb());
    const ImageSlot a = elementsOf<ImageSlot>(with[0])[0];
    EXPECT_TRUE(a.withRebate);
    EXPECT_NEAR(a.rectMm.w, 38.0, 1e-9); // ungeschnittenes Feld: Bildabstand × Filmbreite
    EXPECT_NEAR(a.rectMm.h, 35.0, 1e-9);
    s.showRebate = false;
    const auto cut = ContactSheetLayouter{}.layout(s, frames(6, 1.6), kb());
    const ImageSlot b = elementsOf<ImageSlot>(cut[0])[0];
    EXPECT_FALSE(b.withRebate);
    EXPECT_NEAR(b.rectMm.w / b.rectMm.h, 1.6, 1e-9); // Seitenverhältnis des Zuschnitts
    EXPECT_LE(b.rectMm.w, 36.0 + 1e-9);
    // Hochformat-Zuschnitt liegt gedreht im Bildfeld
    const auto portrait = ContactSheetLayouter{}.layout(s, frames(1, 1.0 / 1.5), kb());
    EXPECT_TRUE(elementsOf<ImageSlot>(portrait[0])[0].rotated);
}

// LF-KTA-05
TEST(ContactSheet, U_LAY_04_HeaderAndNumbers)
{
    ContactSheetSpec s;
    s.header.rollNo = "2026-09-3";
    s.header.film = {"Ilford HP5 Plus", 400, "ID-11 1+1, 13 min", "Leica M6", "2026-09", ""};
    s.header.notes = "Toskana";
    const auto pages = ContactSheetLayouter{}.layout(s, frames(36), kb());
    const auto texts = elementsOf<TextBox>(pages[0]);
    std::string all;
    for (const TextBox& t : texts) {
        all += t.text + "\n";
        EXPECT_GE(t.pt, 7.0) << t.text;
    }
    for (const char* field :
         {"2026-09-3", "Ilford HP5 Plus", "400", "ID-11 1+1, 13 min", "Leica M6", "2026-09", "Toskana"}) {
        EXPECT_NE(all.find(field), std::string::npos) << field;
    }
    for (int i = 1; i <= 36; ++i) {
        EXPECT_TRUE(std::ranges::any_of(texts, [i](const TextBox& t) { return t.text == std::to_string(i); }))
            << i;
    }
}

bool inside(const RectMm& r, double l, double t, double rt, double b)
{
    const double eps = 1e-9;
    return r.x >= l - eps && r.y >= t - eps && r.right() <= rt + eps && r.bottom() <= b + eps;
}

// LF-KTA-06
TEST(ContactSheet, U_LAY_05_PaperSizesAndOrientation)
{
    for (const PaperSize p : {PaperSize::A4, PaperSize::A3, PaperSize::Letter}) {
        for (const bool landscape : {false, true}) {
            ContactSheetSpec s;
            s.paper = p;
            s.landscape = landscape;
            s.punchMarks = true;
            const auto pages = ContactSheetLayouter{}.layout(s, frames(36), kb());
            for (const PageLayout& page : pages) {
                SizeMm expected = paperSize(p);
                if (landscape) {
                    std::swap(expected.w, expected.h);
                }
                EXPECT_EQ(page.sizeMm, expected);
                const double l = std::max(s.marginsMm.left, s.bindingMm);
                const double r = page.sizeMm.w - s.marginsMm.right;
                const double b = page.sizeMm.h - s.marginsMm.bottom;
                for (const ImageSlot& slot : elementsOf<ImageSlot>(page)) {
                    EXPECT_TRUE(inside(slot.rectMm, l, s.marginsMm.top, r, b));
                }
                for (const TextBox& t : elementsOf<TextBox>(page)) {
                    EXPECT_TRUE(inside(t.rectMm, l, s.marginsMm.top, r, b)) << t.text;
                }
                for (const PunchMark& m : elementsOf<PunchMark>(page)) {
                    EXPECT_TRUE(m.centerMm.x > 0 && m.centerMm.x < page.sizeMm.w && m.centerMm.y > 0 &&
                                m.centerMm.y < page.sizeMm.h);
                }
            }
        }
    }
}

// LF-KTA-07
TEST(ContactSheet, U_LAY_06_NothingInBindingMargin)
{
    for (const double binding : {25.0, 35.0}) {
        ContactSheetSpec s;
        s.bindingMm = binding;
        s.punchMarks = true;
        const auto pages = ContactSheetLayouter{}.layout(s, frames(36), kb());
        for (const ImageSlot& slot : elementsOf<ImageSlot>(pages[0])) {
            EXPECT_GE(slot.rectMm.x, binding);
        }
        for (const TextBox& t : elementsOf<TextBox>(pages[0])) {
            EXPECT_GE(t.rectMm.x, binding) << t.text;
        }
        const auto marks = elementsOf<PunchMark>(pages[0]);
        ASSERT_EQ(marks.size(), 2U);
        for (const PunchMark& m : marks) {
            EXPECT_LT(m.centerMm.x + m.diameterMm / 2.0, binding);
        }
    }
}

// LF-KTA-08
TEST(ContactSheet, U_LAY_07_OneToOneScale)
{
    ContactSheetSpec s;
    s.scale = SheetScale::OneToOne;
    s.showRebate = false;
    s.landscape = true; // A4 quer: sechs Bilder 1:1 passen
    const auto quer = ContactSheetLayouter{}.layout(s, frames(36), kb());
    const ImageSlot a = elementsOf<ImageSlot>(quer[0])[0];
    EXPECT_NEAR(a.rectMm.w, 36.0, 0.36);
    EXPECT_NEAR(a.rectMm.h, 24.0, 0.24);
    EXPECT_FALSE(quer[0].hint.has_value());
    EXPECT_GT(quer.size(), 1U); // 1:1 braucht mehr als eine Seite
    s.landscape = false;        // A4 hoch: Streifen zu lang, verkleinert mit Hinweis
    const auto hoch = ContactSheetLayouter{}.layout(s, frames(36), kb());
    EXPECT_LT(elementsOf<ImageSlot>(hoch[0])[0].rectMm.w, 36.0);
    ASSERT_TRUE(hoch[0].hint.has_value());
    EXPECT_LT(hoch[0].scale, 1.0);
}

// LF-KTA-10
TEST(ContactSheet, U_LAY_08_MultiplePages)
{
    ContactSheetSpec s;
    s.scale = SheetScale::OneToOne;
    s.landscape = true;
    const auto pages = ContactSheetLayouter{}.layout(s, frames(80), kb());
    ASSERT_GT(pages.size(), 1U);
    int next = 1;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        EXPECT_EQ(pages[p].pageNo, static_cast<int>(p) + 1);
        EXPECT_EQ(pages[p].pageCount, static_cast<int>(pages.size()));
        const auto texts = elementsOf<TextBox>(pages[p]);
        const std::string counter = "Seite " + std::to_string(p + 1) + "/" + std::to_string(pages.size());
        EXPECT_TRUE(std::ranges::any_of(texts, [&](const TextBox& t) { return t.text == counter; }));
        for (const ImageSlot& slot : elementsOf<ImageSlot>(pages[p])) {
            EXPECT_EQ(numberOf(slot.frame), next++); // keine Lücken
        }
        if (p + 1 < pages.size()) {
            EXPECT_EQ(elementsOf<ImageSlot>(pages[p]).size() % 6, 0U); // Seiten voll mit ganzen Streifen
        }
    }
    EXPECT_EQ(next, 81);
}

// LF-KTA-09
TEST(ContactSheet, U_LAY_09_Background)
{
    ContactSheetSpec s;
    s.background = SheetBackground::Black;
    EXPECT_EQ(ContactSheetLayouter{}.layout(s, frames(6), kb())[0].background, SheetBackground::Black);
    s.background = SheetBackground::White;
    EXPECT_EQ(ContactSheetLayouter{}.layout(s, frames(6), kb())[0].background, SheetBackground::White);
}

} // namespace
} // namespace autoneg::layout
