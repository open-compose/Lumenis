# The application icon, made from packaging/icon/icon-1024.png by
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
    crash_report.cpp
    crash_report.h
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
target_link_libraries(lumenis_app PRIVATE lumenis::ui lumenis::media lumenis::interop lumenis::ext lumenis::core Qt6::Widgets)
lumenis_set_warnings(lumenis_app)

if(WIN32)
    # Console output for --version / --list-providers when run from a terminal.
    target_compile_definitions(lumenis_app PRIVATE LUMENIS_WIN32_ATTACH_CONSOLE)
    # The crash report's stack (crash_report.cpp).
    target_link_libraries(lumenis_app PRIVATE dbghelp)
endif()

include(GNUInstallDirs)
# Windows: FFmpeg's DLLs next to the executable, in the build tree (for the
# tests) and in the packages. macOS (macdeployqt) and Linux (packages,
# linuxdeploy) find them on their own.
if(WIN32 AND FFmpeg_RUNTIME_DLLS)
    file(COPY ${FFmpeg_RUNTIME_DLLS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    install(FILES ${FFmpeg_RUNTIME_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS lumenis_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(MSVC)
    # So a crash report names functions and lines (crash_report.cpp).
    install(FILES $<TARGET_PDB_FILE:lumenis_app> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()

# Bundle Qt into the install tree on macOS and Windows. Linux packages use the
# distribution's Qt (deb/rpm) or linuxdeploy (AppImage).
if((APPLE OR WIN32) AND COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET lumenis_app
        OUTPUT_SCRIPT lumenis_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${lumenis_deploy_script})
endif()

# macdeployqt bundles only the cocoa platform plugin. Add Qt's offscreen one
# so the installed app runs without a window server: --screenshot and
# --smoke-test from a launchd service, such as the self-hosted CI runner
# (BUILDING.md). Its rpath must find the bundle's Frameworks. Packaging.cmake
# signs the bundle afterwards.
if(APPLE AND TARGET Qt6::QOffscreenIntegrationPlugin)
    install(CODE "
        set(platforms \"\${CMAKE_INSTALL_PREFIX}/${PRODUCT_OUTPUT_NAME}.app/Contents/PlugIns/platforms\")
        file(COPY \"$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>\" DESTINATION \"\${platforms}\")
        get_filename_component(plugin \"$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>\" NAME)
        execute_process(
            COMMAND install_name_tool -add_rpath @loader_path/../../Frameworks \"\${platforms}/\${plugin}\"
            RESULT_VARIABLE rc ERROR_VARIABLE err)
        if(NOT rc EQUAL 0 AND NOT err MATCHES \"would duplicate path\")
            message(FATAL_ERROR \"install_name_tool failed: \${err}\")
        endif()")
endif()
