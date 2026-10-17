# Installers, built with CPack:
#   macOS    DragNDrop (.dmg) containing the .app bundle
#   Windows  WiX (.msi) and .zip
#   Linux    .deb, .rpm and .tar.gz; AppImage via packaging/linux/make-appimage.sh

include(GNUInstallDirs)

configure_file(${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_BINARY_DIR}/LICENSE.txt COPYONLY)
set(LUMENIS_LICENSE_FILES
    ${PROJECT_SOURCE_DIR}/LICENSE
    ${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md
    ${PROJECT_SOURCE_DIR}/src/ui/resources/fonts/Inter-LICENSE.txt
    ${PROJECT_SOURCE_DIR}/packaging/licenses/FFmpeg-LGPL-2.1.txt
    ${PROJECT_SOURCE_DIR}/packaging/licenses/miniaudio-LICENSE.txt)
if(APPLE)
    install(FILES ${LUMENIS_LICENSE_FILES} DESTINATION .)
else()
    install(FILES ${LUMENIS_LICENSE_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PRODUCT_EXE_NAME})
endif()

if(APPLE)
    # macdeployqt rewrites the binaries, which invalidates the linker's
    # signature; Apple Silicon refuses to run unsigned code. Ad-hoc sign here;
    # packaging/macos/sign-and-notarize.sh replaces it with a Developer ID.
    install(CODE "
        execute_process(
            COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/${PRODUCT_OUTPUT_NAME}.app\"
            RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR \"ad-hoc codesign failed\")
        endif()")
elseif(UNIX)
    configure_file(${PROJECT_SOURCE_DIR}/packaging/linux/app.desktop.in
        ${PROJECT_BINARY_DIR}/${PRODUCT_BUNDLE_ID}.desktop @ONLY)
    install(FILES ${PROJECT_BINARY_DIR}/${PRODUCT_BUNDLE_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 256 512)
        install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/icon-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME ${PRODUCT_BUNDLE_ID}.png)
    endforeach()
endif()

set(CPACK_PACKAGE_NAME "${PRODUCT_EXE_NAME}")
set(CPACK_PACKAGE_VENDOR "${PRODUCT_ORGANIZATION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PRODUCT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PRODUCT_HOMEPAGE}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PRODUCT_NAME}")
set(CPACK_PACKAGE_EXECUTABLES "${PRODUCT_EXE_NAME}" "${PRODUCT_NAME}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/LICENSE.txt")
set(CPACK_STRIP_FILES OFF)

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "${PRODUCT_NAME}")
    set(CPACK_SYSTEM_NAME "macos-${CMAKE_SYSTEM_PROCESSOR}")
elseif(WIN32)
    # The MSI needs WiX Toolset 3.x (candle/light). Look where its installer,
    # scripts/bootstrap-windows.ps1 and the CI runners put it; without it,
    # build the portable .zip only rather than failing.
    set(_pf86 "ProgramFiles(x86)")
    file(GLOB _wix3_dirs "$ENV{${_pf86}}/WiX Toolset v3*/bin")
    file(TO_CMAKE_PATH "$ENV{WIX}" _wix_env)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _home)
    find_program(LUMENIS_WIX_CANDLE candle
        HINTS "${_wix_env}" "${_wix_env}/bin" "${_home}/.wix/wix314" ${_wix3_dirs})
    if(LUMENIS_WIX_CANDLE)
        get_filename_component(CPACK_WIX_ROOT "${LUMENIS_WIX_CANDLE}" DIRECTORY)
        set(CPACK_GENERATOR "WIX;ZIP")
        # light.exe validates the .msi (ICE) through the Windows Installer
        # service, which a service running as a standard user cannot use
        # (LGHT0217). The self-hosted CI runner skips the validation; the
        # GitHub-hosted job, an administrator, keeps it.
        option(LUMENIS_WIX_SKIP_VALIDATION "Do not validate the .msi (light -sval)" OFF)
        if(LUMENIS_WIX_SKIP_VALIDATION)
            set(CPACK_WIX_LIGHT_EXTRA_FLAGS "-sval")
        endif()
    else()
        message(WARNING "WiX Toolset 3.x not found: 'cpack' will build the .zip "
            "package only. Run scripts/bootstrap-windows.ps1 to install WiX and "
            "get the .msi installer too.")
        set(CPACK_GENERATOR "ZIP")
    endif()
    # Never change: Windows uses it to recognise upgrades of the same product.
    set(CPACK_WIX_UPGRADE_GUID "28ED799C-CCE1-4C2B-804E-AC38665844AD")
    set(CPACK_WIX_PRODUCT_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/app.ico")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "${PRODUCT_NAME}")
    set(CPACK_SYSTEM_NAME "windows-x64")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    find_program(LUMENIS_RPMBUILD rpmbuild)
    if(LUMENIS_RPMBUILD)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()
    set(CPACK_SYSTEM_NAME "linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PRODUCT_ORGANIZATION}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Qt loads its Wayland platform plugin at run time, so shlibdeps cannot
    # see it; without it the app runs through XWayland (BUILDING.md).
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_PACKAGE_SUGGESTS "qt6-qtwayland")
endif()

include(CPack)
