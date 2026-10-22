#pragma once

// Eingecheckte Testdateien der Komponententests (tests/component/data), erzeugt mit
// tools/gen_testdata/gen_component_data.py. Die Formeln hier und im Skript gehören zusammen.

#include <filesystem>

namespace autoneg::testing::files {

constexpr int width = 64;
constexpr int height = 48;

inline std::filesystem::path dataDir()
{
    return std::filesystem::path(AUTONEG_COMPONENT_DATA_DIR);
}

inline std::filesystem::path testData()
{
    return std::filesystem::path(AUTONEG_TESTDATA_DIR);
}

constexpr int pattern8(int x, int y, int c)
{
    return (x * 7 + y * 13 + c * 50) % 256;
}

constexpr int pattern16(int x, int y, int c)
{
    return (x * 771 + y * 1031 + c * 20000) % 65536;
}

} // namespace autoneg::testing::files
