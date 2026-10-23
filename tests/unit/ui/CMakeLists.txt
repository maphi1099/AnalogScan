# Unit-Tests der Oberfläche (U-UI-01 … 05): Widgets ohne Bildschirm (Plattform offscreen).
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)
autoneg_add_test(ui
    LABEL unit
    SOURCES ui_test.cpp
    DEPS ui steps
)
get_target_property(_qt_widgets_type Qt6::Widgets TYPE)
if(_qt_widgets_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(autoneg_test_ui INCLUDE Qt6::QOffscreenIntegrationPlugin)
else()
    target_compile_definitions(autoneg_test_ui PRIVATE
        AUTONEG_QT_PLUGIN_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins")
endif()

# LF-UI-03: jeder übersetzbare Text der Oberfläche hat eine englische Übersetzung
add_test(NAME ui.translations_complete
    COMMAND ${Python3_EXECUTABLE} -X utf8 ${PROJECT_SOURCE_DIR}/tools/check_translations.py ${PROJECT_SOURCE_DIR})
set_tests_properties(ui.translations_complete PROPERTIES LABELS unit)
