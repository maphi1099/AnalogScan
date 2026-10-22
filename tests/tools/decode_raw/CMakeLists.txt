# K-02: RAW mit LibRawDecoder dekodieren und als PFM schreiben (Prüfung mit rawpy)
add_executable(autoneg_decode_raw decode_raw.cpp)
target_link_libraries(autoneg_decode_raw PRIVATE autoneg_adapter_libraw)
autoneg_set_warnings(autoneg_decode_raw)
