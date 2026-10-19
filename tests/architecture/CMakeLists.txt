# Selbsttest der Architekturprüfung: absichtliche Verstöße müssen erkannt werden.
add_test(NAME architecture.selftest
    COMMAND ${Python3_EXECUTABLE} -B -m unittest -v test_check_architecture
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(architecture.selftest PROPERTIES
    LABELS "arch"
    ENVIRONMENT "AUTONEG_TOOLS_DIR=${PROJECT_SOURCE_DIR}/tools")
