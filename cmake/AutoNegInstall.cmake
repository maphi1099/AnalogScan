# Auslieferung (NF-PORT-05): "cmake --install" legt die Anwendung samt Laufzeit und Lizenzen in
# einen Ordner; das Ziel "installer" packt ihn in eine einzelne Datei unter build/<Bau>/installer:
#   Windows: AutoNeg-<Version>-Setup-x64.exe (Inno Setup 6)
#   Linux:   AutoNeg-<Version>-Linux.sh (selbstentpackendes Archiv, CPack)
#
#   cmake --build --preset windows-release --target installer
#
# Nur im Release-Bau; ein Installer mit Debug-Programmen hat keinen Nutzen.

if(NOT AUTONEG_WITH_GUI OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    return()
endif()

set(AUTONEG_INSTALLER_DIR "${CMAKE_BINARY_DIR}/../installer")
set(_triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

if(WIN32)
    install(TARGETS autoneg RUNTIME DESTINATION .)
    # DLLs neben dem Programm (vcpkg applocal); qt.conf des Entwicklungsbaus bleibt draußen
    install(CODE "
        file(GLOB _dlls \"$<TARGET_FILE_DIR:autoneg>/*.dll\")
        file(INSTALL \${_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
    ")
    # Qt-Plugins: Plattform, Stil, Bildformate (Miniaturen als JPEG)
    foreach(_plugin platforms/qwindows styles/qmodernwindowsstyle imageformats/qjpeg imageformats/qgif
            imageformats/qico)
        get_filename_component(_sub "${_plugin}" DIRECTORY)
        install(FILES "${_triplet_dir}/Qt6/plugins/${_plugin}.dll" DESTINATION "plugins/${_sub}")
    endforeach()
    install(CODE "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/qt.conf\" \"[Paths]\\nPlugins = plugins\\n\")")

    # Visual-C++-Laufzeit; OpenMP (vcomp) braucht LibRaw mit raw-parallel. InstallRequiredSystemLibraries
    # findet vcomp unter clang-cl nicht, vcvars64.bat setzt aber VCToolsRedistDir.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE) # Teil von Windows 10 und 11
    include(InstallRequiredSystemLibraries)
    if(DEFINED ENV{VCToolsRedistDir})
        file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _redist)
        file(GLOB _vcomp "${_redist}/x64/Microsoft.VC*.OpenMP/vcomp*.dll")
        install(FILES ${_vcomp} DESTINATION .)
    endif()
    set(_docs .)
    set(_licenses licenses)
else()
    include(GNUInstallDirs)
    # Qt und LibRaw sind gemeinsame Bibliotheken (LGPL, OE-03). Programm, Bibliotheken, Plugins
    # und qt.conf liegen zusammen in lib/autoneg; bin/autoneg verweist darauf. So stört nichts
    # andere Qt-Programme im selben bin-Ordner.
    set(_app ${CMAKE_INSTALL_LIBDIR}/autoneg)
    set_target_properties(autoneg PROPERTIES INSTALL_RPATH "$ORIGIN")
    install(TARGETS autoneg RUNTIME DESTINATION ${_app})
    foreach(_dir platforms xcbglintegrations imageformats platforminputcontexts)
        if(EXISTS "${_triplet_dir}/Qt6/plugins/${_dir}")
            install(DIRECTORY "${_triplet_dir}/Qt6/plugins/${_dir}" DESTINATION ${_app}/plugins)
        endif()
    endforeach()
    install(CODE "
        set(_app \"\${CMAKE_INSTALL_PREFIX}/${_app}\")
        file(WRITE \"\${_app}/qt.conf\" \"[Paths]\nPlugins = plugins\n\")
        file(GLOB_RECURSE _plugins \"\${_app}/plugins/*.so\")
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \"\${_app}/autoneg\"
            LIBRARIES \${_plugins}
            DIRECTORIES \"${_triplet_dir}/lib\"
            RESOLVED_DEPENDENCIES_VAR _deps
            UNRESOLVED_DEPENDENCIES_VAR _missing
            POST_INCLUDE_REGEXES \"^${_triplet_dir}/\"
            POST_EXCLUDE_REGEXES \".*\")
        file(INSTALL \${_deps} DESTINATION \"\${_app}\" FOLLOW_SYMLINK_CHAIN)
        foreach(_p IN LISTS _plugins)
            file(RPATH_SET FILE \"\${_p}\" NEW_RPATH \"\$ORIGIN/../..\")
        endforeach()
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(CREATE_LINK ../${_app}/autoneg \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/autoneg\" SYMBOLIC)
    ")
    install(FILES "${PROJECT_SOURCE_DIR}/installer/linux/autoneg.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    set(_docs ${CMAKE_INSTALL_DATADIR}/doc/autoneg)
    set(_licenses ${_docs}/licenses)
endif()

install(FILES "${PROJECT_SOURCE_DIR}/docs/handbuch/handbuch.pdf" DESTINATION ${_docs} OPTIONAL)

# Lizenztexte der mitgelieferten Bibliotheken (vcpkg: share/<paket>/copyright), ohne Werkzeuge
install(CODE "
    file(GLOB _copyrights \"${_triplet_dir}/share/*/copyright\")
    file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${_licenses}\")
    foreach(_c IN LISTS _copyrights)
        get_filename_component(_dir \"\${_c}\" DIRECTORY)
        get_filename_component(_pkg \"\${_dir}\" NAME)
        if(_pkg MATCHES \"^(gtest|vcpkg-.*|pkgconf.*)$\")
            continue()
        endif()
        file(COPY_FILE \"\${_c}\" \"\${CMAKE_INSTALL_PREFIX}/${_licenses}/\${_pkg}.txt\" ONLY_IF_DIFFERENT)
    endforeach()
")

if(WIN32)
    find_program(AUTONEG_ISCC ISCC
        PATHS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6" "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
              "$ENV{ProgramFiles}/Inno Setup 6")
    if(NOT AUTONEG_ISCC)
        message(STATUS "Inno Setup nicht gefunden (winget install JRSoftware.InnoSetup): kein Ziel installer")
        return()
    endif()
    set(_stage "${CMAKE_BINARY_DIR}/stage")
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_stage}"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${_stage}"
        COMMAND ${AUTONEG_ISCC} /Q "/DStageDir=${_stage}" "/DAppVersion=${PROJECT_VERSION}"
                "/DOutputDir=${AUTONEG_INSTALLER_DIR}" "${PROJECT_SOURCE_DIR}/installer/windows/autoneg.iss"
        DEPENDS autoneg
        COMMENT "Installer nach ${AUTONEG_INSTALLER_DIR}"
        VERBATIM)
else()
    set(CPACK_PACKAGE_NAME AutoNeg)
    set(CPACK_PACKAGE_VENDOR AutoNeg)
    set(CPACK_GENERATOR STGZ)
    set(CPACK_PACKAGE_DIRECTORY "${AUTONEG_INSTALLER_DIR}")
    set(CPACK_PACKAGE_FILE_NAME "AutoNeg-${PROJECT_VERSION}-Linux")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/installer/linux/hinweis.txt")
    set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackConfig.cmake")
    include(CPack)
    add_custom_target(installer
        COMMAND ${CMAKE_CPACK_COMMAND} --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${AUTONEG_INSTALLER_DIR}/_CPack_Packages"
        DEPENDS autoneg
        COMMENT "Installer nach ${AUTONEG_INSTALLER_DIR}"
        VERBATIM)
endif()
