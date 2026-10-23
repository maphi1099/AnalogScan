// Komponententest QtDocumentRenderer mit echtem Qt (K-15, LF-KTA-06, LF-KTA-08): Kontaktabzug
// mit 36 Bildern als PDF schreiben und mit einem unabhängigen Parser (pypdf, verify_pdf.py)
// prüfen: Seitengröße, Bildpositionen und -größen ±0,2 mm, Auflösung ≥ 300 dpi, ICC eingebettet.

#include "autoneg/adapter/lcms/lcms_color_engine.hpp"
#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>

namespace autoneg::adapter::qtprint {
namespace {

void setEnv(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str()); // plattformabhängig (PROGRESS.md)
#else
    setenv(name, value.c_str(), 1);
#endif
}

std::optional<std::string> getEnv(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    std::optional<std::string> out;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        out = value;
    }
    std::free(value);
    return out;
#else
    const char* v = std::getenv(name);
    return v != nullptr ? std::optional<std::string>(v) : std::nullopt;
#endif
}

/// Hält die Qt-Instanz für alle Tests und baut sie nach dem letzten Test ab (sonst meldet
/// LeakSanitizer Qts Schriftcaches als Lecks).
template <class T>
class QtInstance final : public ::testing::Environment
{
public:
    static std::unique_ptr<T>& slot()
    {
        static std::unique_ptr<T> instance;
        return instance;
    }
    void TearDown() override { slot().reset(); }
};

/// Eine Qt-Instanz für alle Tests des Programms, ohne Bildschirm: unter Linux offscreen, unter
/// Windows die native Plattform (offscreen fände dort keine Schriften), wie im Programm.
QtGuiSession& session()
{
    auto& s = QtInstance<QtGuiSession>::slot();
    if (!s) {
#if defined(_WIN32)
        setEnv("QT_QPA_PLATFORM", "windows");
#else
        setEnv("QT_QPA_PLATFORM", "offscreen");
#endif
#if defined(AUTONEG_QT_PLUGIN_DIR)
        setEnv("QT_PLUGIN_PATH", AUTONEG_QT_PLUGIN_DIR);
#endif
        s = std::make_unique<QtGuiSession>();
    }
    return *s;
}

// Abbau nach dem letzten Test; gtest übernimmt das Objekt
[[maybe_unused]] ::testing::Environment* const qtTeardown =
    ::testing::AddGlobalTestEnvironment(new QtInstance<QtGuiSession>);

/// Verlauf als Testbild, damit der Inhalt nicht trivial ist.
image::ImageBuffer gradient(int w, int h, int seed)
{
    const auto uw = static_cast<std::size_t>(w);
    std::vector<float> v(uw * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * uw + static_cast<std::size_t>(x)) * 3;
            v[i] = static_cast<float>(x) / static_cast<float>(w);
            v[i + 1] = static_cast<float>(y) / static_cast<float>(h);
            v[i + 2] = static_cast<float>(seed % 7) / 7.0F;
        }
    }
    return *image::ImageBuffer::fromFloats({w, h, 3, image::PixelFormat::F32, image::ColorSpace::Output},
                                           std::move(v));
}

int runPython(const std::string& script, const std::string& args)
{
    const std::string command = std::string("\"") + AUTONEG_PYTHON + "\" -X utf8 \"" + script + "\" " + args;
    std::cout << "  " << command << "\n";
#if defined(_WIN32)
    return std::system(("\"" + command + "\"").c_str()); // cmd.exe: äußere Anführungszeichen
#else
    return std::system(command.c_str());
#endif
}

// K-15: LF-KTA-06, LF-KTA-08
TEST(QtDocumentRendererComponent, K_15_ContactSheetPdf)
{
    session();
    autoneg::testing::TempDir dir;
    lcms::LcmsColorEngine color;
    const auto srgb = color.profile(ports::ProfileId::Srgb);
    ASSERT_TRUE(srgb.has_value());

    std::vector<layout::SheetFrame> frames;
    for (int i = 1; i <= 36; ++i) {
        std::array<std::uint8_t, 16> b{};
        b[15] = static_cast<std::uint8_t>(i);
        frames.push_back({model::FrameId(b), i, 1.5});
    }
    layout::ContactSheetSpec spec; // A4 hoch, mit Filmrand, schwarz, 300 dpi
    spec.punchMarks = true;
    spec.header.rollNo = "K-15";
    spec.header.film.filmName = "Ilford HP5 Plus";
    const auto pages =
        layout::ContactSheetLayouter{}.layout(spec, frames, *layout::FilmFormat::find("Kleinbild 24×36"));
    ASSERT_EQ(pages.size(), 1U);

    ports::RenderedPage page;
    page.layout = pages[0];
    page.outputProfile = *srgb;
    page.dpi = spec.dpi;
    std::ofstream expected(dir.path() / "expected.txt");
    expected << "page " << page.layout.sizeMm.w << " " << page.layout.sizeMm.h << " " << page.dpi << "\n";
    int seed = 0;
    for (const layout::LayoutElement& e : page.layout.elements) {
        if (const auto* slot = std::get_if<layout::ImageSlot>(&e)) {
            // Render-Ergebnis etwas größer als nötig; der Adapter bringt es auf die Slotauflösung
            page.images.insert_or_assign(slot->frame, gradient(600, 400, seed++));
            expected << "slot " << slot->rectMm.x << " " << slot->rectMm.y << " " << slot->rectMm.w << " "
                     << slot->rectMm.h << "\n";
        }
    }
    for (const char* t : {"Rolle K-15", "Ilford HP5 Plus", "Seite 1/1", "36"}) {
        expected << "text " << t << "\n";
    }
    expected.close();
    ASSERT_EQ(page.images.size(), 36U);

    QtDocumentRenderer renderer;
    const auto pdf = dir.path() / "kontaktabzug.pdf";
    const auto r = renderer.writePdf(std::span(&page, 1), pdf);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(std::filesystem::exists(pdf));
    if (const auto keep = getEnv("AUTONEG_KEEP_PDF")) { // Sichtprüfung von Hand
        std::filesystem::copy_file(pdf, *keep, std::filesystem::copy_options::overwrite_existing);
    }
    EXPECT_EQ(runPython(AUTONEG_VERIFY_PDF,
                        "\"" + pdf.string() + "\" \"" + (dir.path() / "expected.txt").string() + "\""),
              0)
        << "unabhängige Prüfung fehlgeschlagen (Ausgabe oben)";
}

// LF-KTA-06: ohne gültiges Ausgabeprofil kein PDF (Profil muss eingebettet werden)
TEST(QtDocumentRendererComponent, M7_InvalidProfileIsRejected)
{
    session();
    autoneg::testing::TempDir dir;
    ports::RenderedPage page;
    page.layout =
        layout::ContactSheetLayouter{}.layout({}, {}, *layout::FilmFormat::find("Kleinbild 24×36"))[0];
    page.outputProfile = {base::Bytes{1, 2}, "kaputt"};
    const auto r = QtDocumentRenderer{}.writePdf(std::span(&page, 1), dir.path() / "x.pdf");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, base::ErrorCode::InvalidArgument);
}

} // namespace
} // namespace autoneg::adapter::qtprint
