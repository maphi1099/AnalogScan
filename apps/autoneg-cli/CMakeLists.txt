# Kommandozeile, Kompositionswurzel; hängt nicht an der Oberfläche (AE-11). Gleiche
# Komposition wie das GUI-Programm (apps/shared). Den Druck-Adapter (PDF des Kontaktabzugs)
# gibt es nur mit Qt (AUTONEG_WITH_GUI).
set(cli_deps base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
    adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_spdlog adapter_tbb backend_cpu)
if(AUTONEG_WITH_GUI)
    list(APPEND cli_deps adapter_qtprint)
endif()
set(cli_sources main.cpp ${PROJECT_SOURCE_DIR}/apps/shared/composition.cpp)
if(WIN32)
    list(APPEND cli_sources ${PROJECT_SOURCE_DIR}/apps/shared/utf8.manifest) # argv als UTF-8
endif()
autoneg_add_program(autoneg-cli
    SOURCES ${cli_sources}
    DEPS ${cli_deps}
)
target_include_directories(autoneg-cli PRIVATE ${PROJECT_SOURCE_DIR}/apps/shared/include)
if(AUTONEG_WITH_GUI)
    target_compile_definitions(autoneg-cli PRIVATE AUTONEG_WITH_GUI=1)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)
    get_target_property(_qt_gui_type Qt6::Gui TYPE)
    if(_qt_gui_type STREQUAL "STATIC_LIBRARY")
        qt_import_plugins(autoneg-cli INCLUDE Qt6::QOffscreenIntegrationPlugin)
    else()
        # Qt als DLL (Windows): Plugins wie beim GUI-Programm über qt.conf
        file(GENERATE OUTPUT "$<TARGET_FILE_DIR:autoneg-cli>/qt.conf"
            CONTENT "[Paths]\nPlugins = ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins\n")
    endif()
endif()
