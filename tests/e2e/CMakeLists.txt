# E2E-Tests (E-…): die echte Anwendung über ihre Kompositionswurzel, gesteuert mit Qt Test.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Test Widgets)
autoneg_add_test(e2e
    LABEL e2e
    SOURCES e2e_test.cpp ${PROJECT_SOURCE_DIR}/apps/autoneg/composition.cpp
    DEPS ui base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
         adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
         test_synthetic
)

# Sichttest (E-…, Label visual): dieselbe Anwendung auf der echten Grafikschnittstelle; braucht
# einen Bildschirm (ctest -L visual), ohne wird er übersprungen
autoneg_add_test(visual
    LABEL visual
    SOURCES visual_test.cpp ${PROJECT_SOURCE_DIR}/apps/autoneg/composition.cpp
    DEPS ui base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
         adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
         test_synthetic
)
target_compile_definitions(autoneg_test_visual PRIVATE AUTONEG_E2E_NATIVE)

get_target_property(_qt_test_type Qt6::Widgets TYPE)
foreach(_t autoneg_test_e2e autoneg_test_visual)
    target_link_libraries(${_t} PRIVATE Qt6::Test)
    target_include_directories(${_t} PRIVATE ${PROJECT_SOURCE_DIR}/apps)
    if(_qt_test_type STREQUAL "STATIC_LIBRARY")
        qt_import_plugins(${_t} INCLUDE Qt6::QOffscreenIntegrationPlugin)
    else()
        target_compile_definitions(${_t} PRIVATE
            AUTONEG_QT_PLUGIN_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins")
    endif()
endforeach()
