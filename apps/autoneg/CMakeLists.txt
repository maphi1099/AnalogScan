# Grafische Anwendung, Kompositionswurzel: erzeugt die Adapter und reicht sie nach innen.
autoneg_add_program(autoneg
    SOURCES main.cpp ${PROJECT_SOURCE_DIR}/apps/shared/composition.cpp
        $<$<BOOL:${WIN32}>:${PROJECT_SOURCE_DIR}/apps/shared/utf8.manifest>
    DEPS ui base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
)
target_include_directories(autoneg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/apps/shared/include)
if(WIN32)
    set_target_properties(autoneg PROPERTIES WIN32_EXECUTABLE ON) # kein Konsolenfenster
endif()
# Qt als DLL (Windows): qt.conf neben dem Programm zeigt im Entwicklungsbau auf die Plugins in
# vcpkg_installed (Plattform, Bildformate, Stile). Die Auslieferung kopiert sie (NF-PORT-05).
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)
get_target_property(_qt_core_type Qt6::Core TYPE)
if(NOT _qt_core_type STREQUAL "STATIC_LIBRARY")
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:autoneg>/qt.conf"
        CONTENT "[Paths]\nPlugins = ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>Qt6/plugins\n")
endif()
