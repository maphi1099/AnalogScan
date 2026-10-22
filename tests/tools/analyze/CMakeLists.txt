# K-11 (Vorbereitung): Analyse echter Scans mit den echten Adaptern, Bericht und Übersichtsbilder
add_executable(autoneg_analyze analyze.cpp)
target_link_libraries(autoneg_analyze PRIVATE autoneg_analysis autoneg_adapter_libraw autoneg_adapter_oiio
                                              autoneg_adapter_opencv autoneg_backend_cpu)
autoneg_set_warnings(autoneg_analyze)
