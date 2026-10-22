# Gemeinsame Testhilfen.
add_library(autoneg_test_support INTERFACE)
target_include_directories(autoneg_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_support INTERFACE autoneg_model)

# Test-Schritte für die Pipeline (brauchen autoneg_pipeline).
add_library(autoneg_test_fakesteps INTERFACE)
target_link_libraries(autoneg_test_fakesteps INTERFACE autoneg_test_support autoneg_pipeline)

# Generator künstlicher Scans und Farbabstände (ΔE2000).
add_library(autoneg_test_synthetic STATIC
    src/color_metrics.cpp
    src/synthetic.cpp)
target_link_libraries(autoneg_test_synthetic PUBLIC autoneg_test_support autoneg_image)
autoneg_set_warnings(autoneg_test_synthetic)
