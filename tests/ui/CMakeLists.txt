set(ui_test_libs lumenis::ui Qt6::Widgets)

lumenis_add_test(tst_command_registry SOURCES tst_command_registry.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_workspace SOURCES tst_workspace.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_panels SOURCES tst_panels.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_theme SOURCES tst_theme.cpp LIBS ${ui_test_libs})

lumenis_add_test(tst_menu_inventory SOURCES tst_menu_inventory.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_menu_inventory PRIVATE
    MENU_INVENTORY="${PROJECT_SOURCE_DIR}/tests/data/menu_inventory.json")
lumenis_add_test(tst_select_project_dialog SOURCES tst_select_project_dialog.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_main_window SOURCES tst_main_window.cpp LIBS ${ui_test_libs})
# File > Open Bin of a Media Composer bin reads one of the interchange corpus
# (tests/interop/CMakeLists.txt); without it that test is skipped.
set_tests_properties(tst_main_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CUTTHULHU_INTEROP_CORPUS=${LUMENIS_INTEROP_CORPUS}"
    DEPENDS interop_corpus_fetch)

lumenis_add_test(tst_keyboard SOURCES tst_keyboard.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_keyboard PRIVATE
    KEYBOARD_INVENTORY="${PROJECT_SOURCE_DIR}/tests/data/keyboard_inventory.json")
lumenis_add_test(tst_bin_container SOURCES tst_bin_container.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_views SOURCES tst_bin_views.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_dock SOURCES tst_bin_dock.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_locking SOURCES tst_bin_locking.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_platform_check SOURCES tst_platform_check.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_composer SOURCES tst_composer.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_timeline SOURCES tst_timeline.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_settings_window SOURCES tst_settings_window.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_tools SOURCES tst_tools.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_keyboard_workspaces SOURCES tst_keyboard_workspaces.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_effects SOURCES tst_effects.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_audio_media SOURCES tst_audio_media.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_automation_ui SOURCES tst_automation_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_audio_eq_ui SOURCES tst_audio_eq_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_audio_suite_ui SOURCES tst_audio_suite_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_multichannel_ui SOURCES tst_multichannel_ui.cpp LIBS ${ui_test_libs})
# Clip colours in the Timeline (docs/spec/timeline-colors.md).
lumenis_add_test(tst_clip_colors SOURCES tst_clip_colors.cpp LIBS ${ui_test_libs})
# Surround panning in the Audio Mixer (docs/spec/surround.md).
lumenis_add_test(tst_surround_ui SOURCES tst_surround_ui.cpp LIBS ${ui_test_libs})
# The Audio Mixer's loop, inserts, render and Fast menu items (docs/spec/audio-mixer.md).
lumenis_add_test(tst_audio_mixer SOURCES tst_audio_mixer.cpp LIBS ${ui_test_libs} lumenis::media)
# The Clip menu's audio commands (docs/spec/clip-audio.md).
lumenis_add_test(tst_clip_audio SOURCES tst_clip_audio.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_audio_punch_in SOURCES tst_audio_punch_in.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_track_effects SOURCES tst_track_effects.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_source_browser SOURCES tst_source_browser.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_managed_media SOURCES tst_managed_media.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_playback SOURCES tst_playback.cpp LIBS ${ui_test_libs} lumenis::media)
# File > Input > Import EDL, Tools > List Tool, shot logs (Phase 3.6a).
lumenis_add_test(tst_interchange SOURCES tst_interchange.cpp LIBS ${ui_test_libs})

# Scripted edit scenarios (ROADMAP Phase 4 gates 1 and 2; docs/spec/edit-scenarios.md).
lumenis_add_test(tst_scenarios SOURCES tst_scenarios.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_scenarios PRIVATE LUMENIS_SCENARIO_DIR="${PROJECT_SOURCE_DIR}/tests/scenarios")
# The same test on random scenarios whose results scripts/make-scenarios.py works out with a
# reference model of its own (gate 1).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME edit_scenarios_generate
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/make-scenarios.py
                --out ${CMAKE_CURRENT_BINARY_DIR}/generated-scenarios)
    set_tests_properties(edit_scenarios_generate PROPERTIES FIXTURES_SETUP generated_scenarios TIMEOUT 60)
    add_test(NAME edit_scenarios_generated COMMAND tst_scenarios)
    set_tests_properties(edit_scenarios_generated PROPERTIES
        FIXTURES_REQUIRED generated_scenarios TIMEOUT 600
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LUMENIS_SCENARIOS=${CMAKE_CURRENT_BINARY_DIR}/generated-scenarios")
endif()
