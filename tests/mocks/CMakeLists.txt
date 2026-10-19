# Attrappen der Ports für Unit-Tests (nur Header).
add_library(autoneg_test_mocks INTERFACE)
target_include_directories(autoneg_test_mocks INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_mocks INTERFACE autoneg_ports GTest::gmock)
