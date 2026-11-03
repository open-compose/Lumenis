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
lumenis_add_test(tst_effect_mode SOURCES tst_effect_mode.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_effect_keyframes_ui SOURCES tst_effect_keyframes_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_tracking_ui SOURCES tst_tracking_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_titler_ui SOURCES tst_titler_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_intraframe_ui SOURCES tst_intraframe_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_titler_templates SOURCES tst_titler_templates.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_title_tool SOURCES tst_title_tool.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_title_clips SOURCES tst_title_clips.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_audio_media SOURCES tst_audio_media.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_automation_ui SOURCES tst_automation_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_audio_eq_ui SOURCES tst_audio_eq_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_waveform_ui SOURCES tst_waveform_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_track_control SOURCES tst_track_control.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_timeline_views SOURCES tst_timeline_views.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_sync_dupes SOURCES tst_sync_dupes.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_clip_notes SOURCES tst_clip_notes.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_timeline_commands SOURCES tst_timeline_commands.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_sequence_map SOURCES tst_sequence_map.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_timeline_settings SOURCES tst_timeline_settings.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_trim_settings SOURCES tst_trim_settings.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_composer_display SOURCES tst_composer_display.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_select SOURCES tst_bin_select.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_media_unmount SOURCES tst_media_unmount.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_set_font SOURCES tst_set_font.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_layout SOURCES tst_bin_layout.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_attic_ui SOURCES tst_bin_attic_ui.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_editing SOURCES tst_bin_editing.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_interface_colors SOURCES tst_interface_colors.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_settings SOURCES tst_bin_settings.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_monitor_markers SOURCES tst_monitor_markers.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_composer_edit SOURCES tst_composer_edit.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_timecode_window SOURCES tst_timecode_window.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_find_window SOURCES tst_find_window.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_autosequence SOURCES tst_autosequence.cpp LIBS ${ui_test_libs})
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
# The Multi-Mix Tool (docs/spec/multi-mix.md).
lumenis_add_test(tst_multi_mix SOURCES tst_multi_mix.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_audio_punch_in SOURCES tst_audio_punch_in.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_track_effects SOURCES tst_track_effects.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_source_browser SOURCES tst_source_browser.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_managed_media SOURCES tst_managed_media.cpp LIBS ${ui_test_libs} lumenis::media)
# Render Effect and Clear Renders in the application (docs/spec/rendering.md).
lumenis_add_test(tst_render_ui SOURCES tst_render_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_motion_effect_ui SOURCES tst_motion_effect_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_motion_adapter_ui SOURCES tst_motion_adapter_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_nest_ui SOURCES tst_nest_ui.cpp LIBS ${ui_test_libs} lumenis::media)
lumenis_add_test(tst_effect_template_ui SOURCES tst_effect_template_ui.cpp LIBS ${ui_test_libs} lumenis::media)
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
