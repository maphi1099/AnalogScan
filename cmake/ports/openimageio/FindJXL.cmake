# Find-Modul für libjxl im Overlay-Port (AutoNeg, BUGS.md B-16).
#
# Ersetzt src/cmake/modules/FindJXL.cmake von OpenImageIO 3.1.14: Das Original nennt nur
# jxl und jxl_threads. Bei statischem Bau (Linux-Triplets) fehlen dann jxl_cms, Brotli,
# Highway und Little CMS beim Linken der Anwendung. libjxl liefert keine CMake-Konfiguration,
# die Abhängigkeiten stehen nur in libjxl.pc (Requires: libhwy libbrotlienc libbrotlidec
# libjxl_cms; libjxl_cms: lcms2).
#
# Ergebnis: JXL_FOUND, JXL_INCLUDES, JXL_LIBRARIES, JXL_VERSION

include(FindPackageHandleStandardArgs)

find_path(JXL_INCLUDE_DIR NAMES jxl/decode.h jxl/encode.h)
mark_as_advanced(JXL_INCLUDE_DIR)

if(JXL_INCLUDE_DIR)
    file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_MAJOR_VERSION .*$")
    string(REGEX MATCHALL "[0-9]+" JPEGXL_MAJOR_VERSION ${TMP})
    file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_MINOR_VERSION .*$")
    string(REGEX MATCHALL "[0-9]+" JPEGXL_MINOR_VERSION ${TMP})
    file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_PATCH_VERSION .*$")
    string(REGEX MATCHALL "[0-9]+" JPEGXL_PATCH_VERSION ${TMP})
    set(JXL_VERSION "${JPEGXL_MAJOR_VERSION}.${JPEGXL_MINOR_VERSION}.${JPEGXL_PATCH_VERSION}")
endif()

find_library(JXL_LIBRARY NAMES jxl)
find_library(JXL_THREADS_LIBRARY NAMES jxl_threads)
mark_as_advanced(JXL_LIBRARY JXL_THREADS_LIBRARY JXL_VERSION)

find_package_handle_standard_args(JXL REQUIRED_VARS JXL_LIBRARY JXL_THREADS_LIBRARY JXL_INCLUDE_DIR)

if(JXL_FOUND)
    set(JXL_LIBRARIES ${JXL_LIBRARY} ${JXL_THREADS_LIBRARY})
    set(JXL_INCLUDES ${JXL_INCLUDE_DIR})
    if(NOT BUILD_SHARED_LIBS)
        # Statische Bibliotheken: Abhängigkeiten aus libjxl.pc in Linkreihenfolge anhängen
        foreach(dep jxl_cms brotlienc brotlidec brotlicommon hwy lcms2)
            string(TOUPPER "${dep}" upper)
            find_library(JXL_DEP_${upper}_LIBRARY NAMES ${dep})
            mark_as_advanced(JXL_DEP_${upper}_LIBRARY)
            if(JXL_DEP_${upper}_LIBRARY)
                list(APPEND JXL_LIBRARIES ${JXL_DEP_${upper}_LIBRARY})
            endif()
        endforeach()
    endif()
endif()
