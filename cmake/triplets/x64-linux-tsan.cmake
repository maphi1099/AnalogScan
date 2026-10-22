# vcpkg-Triplet für das Preset linux-clang-tsan: alle Abhängigkeiten (v. a. oneTBB) mit
# ThreadSanitizer und clang bauen. Sonst sieht TSan die Synchronisation im TBB-Scheduler
# nicht und meldet Scheinrennen (BUGS.md B-12).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_C_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/clang-linux-toolchain.cmake")
