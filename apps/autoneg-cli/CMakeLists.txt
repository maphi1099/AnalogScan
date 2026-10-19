# Kommandozeile, Kompositionswurzel; hängt nicht an der Oberfläche (AE-11).
autoneg_add_program(autoneg-cli
    SOURCES main.cpp
    DEPS base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
)
