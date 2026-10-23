# Adapter mit echten Bibliotheken: LibRaw (K-01, K-03), OpenImageIO (K-03, K-04, K-05),
# Little CMS (K-07). Eingecheckte Testdateien: tests/component/data.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

autoneg_add_test(adapters
    LABEL component
    SOURCES libraw_test.cpp oiio_test.cpp lcms_test.cpp
    DEPS adapter_libraw adapter_oiio adapter_lcms test_synthetic
)
target_compile_definitions(autoneg_test_adapters PRIVATE
    AUTONEG_COMPONENT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
    AUTONEG_PYTHON="${Python3_EXECUTABLE}"
    AUTONEG_VERIFY_EXPORT="${CMAKE_CURRENT_SOURCE_DIR}/verify_export.py")

# K-02: Linearität gegen die mit rawpy gelesenen Sensordaten (Python, unabhängiger Leser)
add_test(NAME LibRawComponent.K_02_LinearDecoding
    COMMAND ${Python3_EXECUTABLE} -X utf8 ${CMAKE_CURRENT_SOURCE_DIR}/k02_linearity.py
            $<TARGET_FILE:autoneg_decode_raw> ${AUTONEG_TESTDATA_DIR}/100MSDCF/MAT05092.ARW
            ${CMAKE_CURRENT_BINARY_DIR}/k02)
set_tests_properties(LibRawComponent.K_02_LinearDecoding PROPERTIES LABELS component)

# K-15: Kontaktabzug als PDF über Qt, geprüft mit pypdf (unabhängiger Parser). Nur mit GUI.
if(AUTONEG_WITH_GUI)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)
    autoneg_add_test(qtprint
        LABEL component
        SOURCES qtprint_test.cpp
        DEPS adapter_qtprint adapter_lcms test_support
    )
    target_compile_definitions(autoneg_test_qtprint PRIVATE
        AUTONEG_PYTHON="${Python3_EXECUTABLE}"
        AUTONEG_VERIFY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/verify_pdf.py")
    get_target_property(_qt_gui_type Qt6::Gui TYPE)
    if(_qt_gui_type STREQUAL "STATIC_LIBRARY")
        # statisches Qt (Linux): Plattform offscreen fest einbinden
        qt_import_plugins(autoneg_test_qtprint INCLUDE Qt6::QOffscreenIntegrationPlugin)
    else()
        # Qt als DLL (Windows): Plugins aus vcpkg_installed (plattformabhängig, PROGRESS.md)
        target_compile_definitions(autoneg_test_qtprint PRIVATE
            AUTONEG_QT_PLUGIN_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins")
    endif()
endif()

# K-20: Protokoll mit spdlog
autoneg_add_test(spdlog
    LABEL component
    SOURCES spdlog_test.cpp
    DEPS adapter_spdlog test_support
)
