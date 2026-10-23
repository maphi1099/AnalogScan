# Kommandozeile, Kompositionswurzel; hängt nicht an der Oberfläche (AE-11).
# Den Druck-Adapter (PDF des Kontaktabzugs) gibt es nur mit Qt (AUTONEG_WITH_GUI).
set(cli_deps base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
    adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_spdlog adapter_tbb backend_cpu)
if(AUTONEG_WITH_GUI)
    list(APPEND cli_deps adapter_qtprint)
endif()
autoneg_add_program(autoneg-cli
    SOURCES main.cpp
    DEPS ${cli_deps}
)
if(AUTONEG_WITH_GUI)
    target_compile_definitions(autoneg-cli PRIVATE AUTONEG_WITH_GUI=1)
endif()
