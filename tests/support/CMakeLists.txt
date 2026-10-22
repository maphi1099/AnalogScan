# Gemeinsame Testhilfen (nur Header).
add_library(autoneg_test_support INTERFACE)
target_include_directories(autoneg_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_support INTERFACE autoneg_model)

# Test-Schritte für die Pipeline (brauchen autoneg_pipeline).
add_library(autoneg_test_steps INTERFACE)
target_link_libraries(autoneg_test_steps INTERFACE autoneg_test_support autoneg_pipeline)
