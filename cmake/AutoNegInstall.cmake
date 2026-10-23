# Installation unter Windows (Auslieferung, NF-PORT-05): Programme, die von vcpkg neben die
# Programme kopierten DLLs, die benötigten Qt-Plugins mit relativer qt.conf, die
# Visual-C++- und OpenMP-Laufzeit sowie die Lizenztexte der Fremdbibliotheken.
#
#   cmake --install build/windows-clang --config Release --prefix build/install-windows
#
# Den Installer baut installer/windows/build_installer.ps1 (Inno Setup). Linux folgt später.

if(NOT WIN32 OR NOT AUTONEG_WITH_GUI)
    return()
endif()

set(_triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

install(TARGETS autoneg autoneg-cli RUNTIME DESTINATION . CONFIGURATIONS Release)

# DLLs neben den Programmen (vcpkg applocal); qt.conf des Entwicklungsbaus bleibt draußen
install(CODE "
    file(GLOB _dlls \"$<TARGET_FILE_DIR:autoneg>/*.dll\" \"$<TARGET_FILE_DIR:autoneg-cli>/*.dll\")
    list(REMOVE_DUPLICATES _dlls)
    file(INSTALL \${_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
" CONFIGURATIONS Release)

# Qt-Plugins: Plattform, Stil, Bildformate (Miniaturen als JPEG)
foreach(_plugin platforms/qwindows styles/qmodernwindowsstyle imageformats/qjpeg imageformats/qgif
        imageformats/qico)
    get_filename_component(_sub "${_plugin}" DIRECTORY)
    install(FILES "${_triplet_dir}/Qt6/plugins/${_plugin}.dll" DESTINATION "plugins/${_sub}"
        CONFIGURATIONS Release)
endforeach()
install(CODE "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/qt.conf\" \"[Paths]\\nPlugins = plugins\\n\")"
    CONFIGURATIONS Release)

# Visual-C++-Laufzeit und OpenMP (vcomp, LibRaw mit raw-parallel) neben die Programme
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE) # Teil von Windows 10 und 11
include(InstallRequiredSystemLibraries)
# InstallRequiredSystemLibraries findet vcomp unter clang-cl nicht; LibRaw (raw-parallel) braucht
# es. vcvars64.bat setzt VCToolsRedistDir.
if(DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _redist)
    file(GLOB _vcomp "${_redist}/x64/Microsoft.VC*.OpenMP/vcomp*.dll")
    install(FILES ${_vcomp} DESTINATION . CONFIGURATIONS Release)
endif()

# Lizenztexte der mitgelieferten Bibliotheken (vcpkg: share/<paket>/copyright); Werkzeuge,
# die nicht ausgeliefert werden, bleiben draußen
install(CODE "
    file(GLOB _copyrights \"${_triplet_dir}/share/*/copyright\")
    file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/licenses\")
    foreach(_c IN LISTS _copyrights)
        get_filename_component(_dir \"\${_c}\" DIRECTORY)
        get_filename_component(_pkg \"\${_dir}\" NAME)
        if(_pkg MATCHES \"^(gtest|vcpkg-.*|pkgconf.*)$\")
            continue()
        endif()
        file(COPY_FILE \"\${_c}\" \"\${CMAKE_INSTALL_PREFIX}/licenses/\${_pkg}.txt\" ONLY_IF_DIFFERENT)
    endforeach()
" CONFIGURATIONS Release)
