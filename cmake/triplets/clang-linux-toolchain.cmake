# Toolchain für vcpkg-Ports im Triplet x64-linux-tsan: clang mit ThreadSanitizer.
# Bei einer eigenen Toolchain wendet vcpkg VCPKG_*_FLAGS nicht selbst an; deshalb hier.
# -mwaitpkg: oneTBB nutzt _tpause (zur Laufzeit abgefragt) und setzt das Flag selbst nur bei GCC.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_FLAGS_INIT "-fPIC -fsanitize=thread -fno-omit-frame-pointer -mwaitpkg")
set(CMAKE_CXX_FLAGS_INIT "-fPIC -fsanitize=thread -fno-omit-frame-pointer -mwaitpkg")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fsanitize=thread")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fsanitize=thread")
