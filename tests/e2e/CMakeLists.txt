# E2E-Tests (E-…): die echte Anwendung über ihre Kompositionswurzel, gesteuert mit Qt Test.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Test Widgets)
autoneg_add_test(e2e
    LABEL e2e
    SOURCES e2e_test.cpp ${PROJECT_SOURCE_DIR}/apps/shared/composition.cpp
    DEPS ui base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
         adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
         test_synthetic
)
target_link_libraries(autoneg_test_e2e PRIVATE Qt6::Test)
target_include_directories(autoneg_test_e2e PRIVATE ${PROJECT_SOURCE_DIR}/apps/autoneg/include
    ${PROJECT_SOURCE_DIR}/apps/shared/include)
get_target_property(_qt_test_type Qt6::Widgets TYPE)
if(_qt_test_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(autoneg_test_e2e INCLUDE Qt6::QOffscreenIntegrationPlugin)
else()
    target_compile_definitions(autoneg_test_e2e PRIVATE
        AUTONEG_QT_PLUGIN_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins")
endif()
