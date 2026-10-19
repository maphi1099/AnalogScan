cmake_minimum_required(VERSION 3.28)

project(AutoNeg
    VERSION 1.0.0
    DESCRIPTION "Konvertierung kameradigitalisierter Filmnegative und Dias"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "AutoNeg wird mit clang gebaut (unter Linux zusätzlich GCC); gefunden: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUTONEG_BUILD_TESTS "Tests bauen" ON)
option(AUTONEG_CHECK_ARCHITECTURE "Architekturprüfung bei jedem Build ausführen" ON)
option(AUTONEG_ARCH_VIOLATION_DEMO "Absichtlicher Regelverstoß (model -> ports) zum Nachweis der Architekturprüfung" OFF)
set(AUTONEG_SANITIZER "" CACHE STRING "Sanitizer: leer, address oder thread")
set(AUTONEG_TESTDATA_DIR "${PROJECT_SOURCE_DIR}/../Testing/Data" CACHE PATH "Testdaten (nur lesen)")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(AutoNegWarnings)
include(AutoNegModule)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

add_subdirectory(src)
add_subdirectory(apps)

if(AUTONEG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Nach allen Zielen: Abhängigkeiten exportieren und Architekturprüfung einhängen
autoneg_finalize_architecture()
