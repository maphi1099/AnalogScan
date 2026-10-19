# Grafische Anwendung, Kompositionswurzel: erzeugt die Adapter und reicht sie nach innen.
autoneg_add_program(autoneg
    SOURCES main.cpp
    DEPS ui base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_qtprint adapter_spdlog adapter_tbb backend_cpu
)
