autoneg_add_test(steps
    LABEL unit
    SOURCES steps_test.cpp
    DEPS steps test_synthetic test_reference test_mocks
)
# Eingecheckte Referenzliste der Kennungen und Versionen (U-STP-03)
target_compile_definitions(autoneg_test_steps PRIVATE
    AUTONEG_STEP_IDS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/step_ids.txt")
