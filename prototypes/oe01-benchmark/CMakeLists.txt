# Benchmark-Prototyp zur offenen Entscheidung OE-01 (Architektur Kap. 2):
# CPU-Kernels mit Halide oder mit Google Highway + oneTBB.
#
# Eigenständiges Projekt, nicht Teil des Produkt-Builds. Halide wird aus dem offiziellen
# Python-Wheel bezogen (Halide_DIR setzen), Highway und oneTBB über vcpkg (vcpkg.json hier).
cmake_minimum_required(VERSION 3.28)
project(oe01_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(TBB CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(Halide 21 REQUIRED)

# --- Halide: Generatoren werden zur Build-Zeit ausgeführt (AOT) und erzeugen Objektdateien
add_halide_generator(bench_generators SOURCES halide_generators.cpp)
add_halide_runtime(bench_halide_runtime TARGETS cmake)
foreach(gen IN ITEMS pointwise lut fused unsharp)
    add_halide_library(halide_${gen} FROM bench_generators
        GENERATOR ${gen}
        TARGETS cmake
        USE_RUNTIME bench_halide_runtime)
endforeach()

add_executable(oe01_benchmark
    main.cpp
    kernels_scalar.cpp
    kernels_hwy.cpp
    kernels_halide.cpp)
# Highway sucht die Datei selbst über HWY_TARGET_INCLUDE
target_include_directories(oe01_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oe01_benchmark PRIVATE
    TBB::tbb hwy::hwy hwy::hwy_contrib
    halide_pointwise halide_lut halide_fused halide_unsharp bench_halide_runtime)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(oe01_benchmark PRIVATE /O2 /EHsc)
    target_compile_definitions(oe01_benchmark PRIVATE NOMINMAX)
else()
    target_compile_options(oe01_benchmark PRIVATE -O3)
endif()
