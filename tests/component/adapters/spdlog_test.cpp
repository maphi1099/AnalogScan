// Komponententest SpdLogger mit echtem spdlog (K-20, NF-REL-03): Einträge mit Zeit und Ebene,
// Rotation beim Überschreiten der Größe, Ordner mit Umlauten.

#include "autoneg/adapter/spdlog/spd_logger.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <sstream>

namespace autoneg::adapter::spdlog {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// K-20: NF-REL-03
TEST(SpdLoggerComponent, K_20_EntriesWithTimeAndLevel)
{
    autoneg::testing::TempDir dir;
    const fs::path file = dir.path() / fs::path(u8"Protokoll Jörg") / "autoneg.log";
    {
        SpdLogger log(file);
        log.log(ports::LogLevel::Debug, "unter der Schwelle");
        log.log(ports::LogLevel::Info, "Rolle geöffnet");
        log.log(ports::LogLevel::Warning, "Begleitdatei defekt");
        log.log(ports::LogLevel::Error, "Export fehlgeschlagen");
    }
    const std::string text = readFile(file);
    EXPECT_EQ(text.find("unter der Schwelle"), std::string::npos);
    const std::regex line(
        R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[(info|warning|error)\] \[\d+\] .+)");
    std::istringstream lines(text);
    int n = 0;
    for (std::string l; std::getline(lines, l);) {
        if (!l.empty() && l.back() == '\r') {
            l.pop_back(); // Zeilenende unter Windows CR LF
        }
        EXPECT_TRUE(std::regex_match(l, line)) << l;
        ++n;
    }
    EXPECT_EQ(n, 3);
    EXPECT_NE(text.find("[warning] "), std::string::npos);
    EXPECT_NE(text.find("[error] "), std::string::npos);
    EXPECT_NE(text.find("Rolle geöffnet"), std::string::npos);
}

// K-20: Rotation
TEST(SpdLoggerComponent, K_20_RotationKeepsLimitedFiles)
{
    autoneg::testing::TempDir dir;
    const fs::path file = dir.path() / "autoneg.log";
    {
        SpdLogger log(file, 2000, 2);
        for (int i = 0; i < 200; ++i) {
            log.log(ports::LogLevel::Warning, "Eintrag " + std::to_string(i) + " mit etwas Text zum Füllen");
        }
    }
    EXPECT_TRUE(fs::exists(file));
    EXPECT_TRUE(fs::exists(dir.path() / "autoneg.1.log"));
    EXPECT_TRUE(fs::exists(dir.path() / "autoneg.2.log"));
    EXPECT_FALSE(fs::exists(dir.path() / "autoneg.3.log"));
    EXPECT_LE(fs::file_size(file), 2000U);
    EXPECT_NE(readFile(file).find("Eintrag 199"),
              std::string::npos); // jüngster Eintrag in der aktuellen Datei
}

} // namespace
} // namespace autoneg::adapter::spdlog
