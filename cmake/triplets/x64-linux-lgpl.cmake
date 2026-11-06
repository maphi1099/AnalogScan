# vcpkg-Triplet für Linux (OE-03, D-xx): Qt (LGPL) und LibRaw als gemeinsame Bibliotheken, damit
# Anwender sie austauschen können; alles andere statisch wie bisher.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
if(PORT MATCHES "^(qt.*|libraw)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_FIXUP_ELF_RPATH ON)
