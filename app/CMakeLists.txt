# The application icon, rendered from packaging/icon.svg by
# scripts/make-icons.py.
set(app_icon_sources)
if(APPLE)
    set(app_icon_sources ${PROJECT_SOURCE_DIR}/packaging/macos/app.icns)
    set_source_files_properties(${app_icon_sources} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    set(APP_ICON_ICO "${PROJECT_SOURCE_DIR}/packaging/windows/app.ico")
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    set(app_icon_sources ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

qt_add_executable(lumenis_app WIN32 MACOSX_BUNDLE
    main.cpp
    module_paths.cpp
    module_paths.h
    ${app_icon_sources}
)
set_target_properties(lumenis_app PROPERTIES
    OUTPUT_NAME "${PRODUCT_OUTPUT_NAME}"
    MACOSX_BUNDLE_BUNDLE_NAME "${PRODUCT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${PRODUCT_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later"
    MACOSX_BUNDLE_ICON_FILE app.icns
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
