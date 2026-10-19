# Gemeinsame Testhilfen (nur Header).
add_library(autoneg_test_support INTERFACE)
target_include_directories(autoneg_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_support INTERFACE autoneg_model)
