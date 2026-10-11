qt_add_executable(lumenis_app WIN32 MACOSX_BUNDLE
    main.cpp
    module_paths.cpp
    module_paths.h
)
set_target_properties(lumenis_app PROPERTIES
    OUTPUT_NAME "${PRODUCT_OUTPUT_NAME}"
    MACOSX_BUNDLE_BUNDLE_NAME "${PRODUCT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${PRODUCT_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later"
)
target_link_libraries(lumenis_app PRIVATE lumenis::ui lumenis::ext lumenis::core Qt6::Widgets)
lumenis_set_warnings(lumenis_app)

if(WIN32)
    # Console output for --version / --list-providers when run from a terminal.
    target_compile_definitions(lumenis_app PRIVATE LUMENIS_WIN32_ATTACH_CONSOLE)
endif()

include(GNUInstallDirs)
install(TARGETS lumenis_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Bundle Qt into the install tree on macOS and Windows. Linux packages use the
# distribution's Qt (deb/rpm) or linuxdeploy (AppImage).
if((APPLE OR WIN32) AND COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET lumenis_app
        OUTPUT_SCRIPT lumenis_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${lumenis_deploy_script})
endif()
