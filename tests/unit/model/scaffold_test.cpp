// Beispieltest des Projektgerüsts (M0): prüft, dass ein Kernmodul baut, linkt und
// von CTest mit dem Label "unit" gefunden wird. Kein Szenario der Testliste.

#include "autoneg/model/module.hpp"

#include <gtest/gtest.h>

TEST(Scaffold, M0_ModelModuleIsLinked)
{
    EXPECT_EQ(autoneg::model::moduleName(), "model");
}
