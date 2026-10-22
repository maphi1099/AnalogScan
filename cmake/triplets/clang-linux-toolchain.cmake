# Toolchain für vcpkg-Ports im Triplet x64-linux-tsan: clang mit ThreadSanitizer.
# Bei einer eigenen Toolchain wendet vcpkg VCPKG_*_FLAGS nicht selbst an; deshalb hier.
# -mwaitpkg: oneTBB nutzt _tpause (zur Laufzeit abgefragt) und setzt das Flag selbst nur bei GCC.
# Das Triplet setzt CMAKE_SYSTEM_NAME; dann bleibt CMAKE_SYSTEM_PROCESSOR leer (wie beim
# Cross-Compiling). OpenImageIO fragt es ab (compiler.cmake) - wie die vcpkg-Toolchain setzen.
if(NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_FLAGS_INIT "-fPIC -fsanitize=thread -fno-omit-frame-pointer -mwaitpkg")
set(CMAKE_CXX_FLAGS_INIT "-fPIC -fsanitize=thread -fno-omit-frame-pointer -mwaitpkg")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fsanitize=thread")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fsanitize=thread")
