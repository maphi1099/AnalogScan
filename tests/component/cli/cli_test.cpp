// Komponententest Kommandozeile (K-17, LF-BAT-11, NF-ARC-10): eine Rolle mit Begleitdateien
// wird einmal über die Dienstschicht der Anwendung (ExportService der gemeinsamen Komposition)
// und einmal mit dem Programm autoneg-cli exportiert; die Pixel müssen bitgleich sein.

#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/app/composition.hpp"
#include "autoneg/testing/synthetic.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace autoneg {
namespace {

namespace fs = std::filesystem;

model::EditEntry entry(const std::string& id, model::ParamSet p)
{
    model::EditEntry e;
    e.step = model::StepId(id);
    e.params = std::move(p);
    return e;
}

int run(const std::string& command)
{
    std::cout << "  " << command << "\n";
#if defined(_WIN32)
    return std::system(("\"" + command + "\"").c_str()); // cmd.exe: äußere Anführungszeichen
#else
    return std::system(command.c_str());
#endif
}

class CliComponent : public ::testing::Test
{
protected:
    void SetUp() override
    {
        roll = tmp.path() / "rolle";
        fs::create_directories(roll);
        adapter::oiio::OiioImageWriter writer;
        for (int i = 1; i <= 3; ++i) {
            testing::NegativeSpec spec;
            spec.width = 360;
            spec.height = 240;
            spec.seed = static_cast<std::uint64_t>(i);
            spec.dust = i;
            spec.frame.x = 0.1 + 0.002 * i;
            ports::WriteOptions o;
            o.format = ports::FileFormat::Tiff;
            o.bitDepth = 16;
            ASSERT_TRUE(writer
                            .write(roll / ("SCAN000" + std::to_string(i) + ".tif"),
                                   testing::makeNegative(spec).scan, o)
                            .has_value());
        }
        dirs = {tmp.path() / "config", tmp.path() / "cache"};
    }

    testing::TempDir tmp;
    fs::path roll;
    app::Directories dirs;
};

// K-17: LF-BAT-11, NF-ARC-10
TEST_F(CliComponent, K_17_CliExportEqualsApplicationExport)
{
    const fs::path viaApp = tmp.path() / "anwendung";
    {
        application::NoDocumentRenderer document;
        app::Composition c(dirs, {}, [](std::function<void()> f) { f(); }, document);
        // Bearbeitung über die Befehlsschicht, gespeichert als Begleitdateien
        auto opened = c.rolls().open(roll);
        ASSERT_TRUE(opened.has_value());
        int n = 0;
        for (const application::OpenedFrame& f : opened->frames) {
            c.bus().open(f.frame, f.data);
            const std::vector<model::FrameId> target{f.frame.id};
            // Filmtyp wie nach der Wahl in der Oberfläche (K-M10-08): sonst nur der Scan
            c.bus().execute(application::SetEntryCommand(entry(
                                "autoneg.filmmode", model::ParamSet{}.with("type", std::string("color")))),
                            target, opened->roll);
            c.bus().execute(
                application::SetEntryCommand(entry(
                    "autoneg.crop", model::ParamSet{}.with("rect", model::NormRect{0.1, 0.2, 0.8, 0.6}))),
                target, opened->roll);
            c.bus().execute(application::SetEntryCommand(
                                entry("autoneg.exposure", model::ParamSet{}.with("ev", 0.3 * ++n))),
                            target, opened->roll);
            c.bus().execute(
                application::SetEntryCommand(entry("autoneg.sharpen", model::ParamSet{}.with("amount", 0.8))),
                target, opened->roll);
        }
        c.queue().flush();
        c.rolls().close(roll);
        // Export über den ExportService (wie das Hauptfenster)
        auto again = c.rolls().open(roll);
        ASSERT_TRUE(again.has_value());
        std::vector<application::ExportService::Item> items;
        for (const application::OpenedFrame& f : again->frames) {
            ASSERT_EQ(f.data.edits.size(), 4U);
            items.push_back({f.frame, f.data.edits});
        }
        application::ExportSettings s;
        s.folder = viaApp;
        s.pattern = "{name}";
        s.write.format = ports::FileFormat::Tiff;
        s.write.bitDepth = 16;
        fs::create_directories(viaApp);
        ASSERT_TRUE(c.exports()
                        .exportFrames(items, std::make_shared<const model::Roll>(again->roll), s)
                        .wait()
                        .has_value());
        c.rolls().close(roll);
    }
    // dieselbe Rolle mit der Kommandozeile
    const fs::path viaCli = tmp.path() / "kommandozeile";
    const std::string cmd = std::string("\"") + AUTONEG_CLI + "\" export \"" + roll.string() + "\" -o \"" +
                            viaCli.string() + "\" --format tiff --bits 16 --pattern {name} --config \"" +
                            dirs.config.string() + "\" --cache \"" + dirs.cache.string() + "\"";
    ASSERT_EQ(run(cmd), 0);

    adapter::oiio::OiioImageReader reader;
    for (int i = 1; i <= 3; ++i) {
        const std::string name = "SCAN000" + std::to_string(i) + ".tif";
        const auto a = reader.read(viaApp / name);
        const auto b = reader.read(viaCli / name);
        ASSERT_TRUE(a.has_value() && b.has_value()) << name;
        ASSERT_EQ(a->spec().width, b->spec().width);
        ASSERT_EQ(a->spec().height, b->spec().height);
        ASSERT_EQ(a->spec().channels, b->spec().channels);
        const auto pa = a->host();
        const auto pb = b->host();
        ASSERT_EQ(pa.size(), pb.size());
        std::size_t diff = 0;
        for (std::size_t k = 0; k < pa.size(); ++k) {
            diff += pa[k] != pb[k] ? 1U : 0U;
        }
        EXPECT_EQ(diff, 0U) << name << ": Pixel nicht bitgleich";
        // gleiches eingebettetes Profil; ausgenommen Erstellungsdatum (Kopf, Bytes 24–35) und die
        // davon abhängige Profil-ID (Bytes 84–99): Little CMS setzt beim Erzeugen die aktuelle Zeit
        auto pa2 = reader.profile(viaApp / name).value().bytes;
        auto pb2 = reader.profile(viaCli / name).value().bytes;
        ASSERT_EQ(pa2.size(), pb2.size());
        ASSERT_GT(pa2.size(), 128U);
        for (auto* bytes : {&pa2, &pb2}) {
            std::fill(bytes->begin() + 24, bytes->begin() + 36, std::uint8_t{0});
            std::fill(bytes->begin() + 84, bytes->begin() + 100, std::uint8_t{0});
        }
        EXPECT_EQ(pa2, pb2) << name;
    }
}

// LF-BAT-11: Aufruffehler mit Rückgabewert, keine Ausnahme, kein Absturz
TEST_F(CliComponent, M9_UsageErrorsHaveExitCodes)
{
    const std::string cli = std::string("\"") + AUTONEG_CLI + "\"";
    EXPECT_EQ(run(cli + " --version"), 0);
    EXPECT_NE(run(cli + " export \"" + roll.string() + "\""), 0); // Ziel fehlt
    EXPECT_NE(run(cli + " export \"" + (tmp.path() / "fehlt").string() + "\" -o \"" + tmp.path().string() +
                  "/x\" --config \"" + dirs.config.string() + "\" --cache \"" + dirs.cache.string() + "\""),
              0);
    EXPECT_NE(run(cli + " unbekannt \"" + roll.string() + "\""), 0);
}

} // namespace
} // namespace autoneg
