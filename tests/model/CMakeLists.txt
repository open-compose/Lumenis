lumenis_add_test(tst_project_store SOURCES tst_project_store.cpp LIBS lumenis::model)
lumenis_add_test(tst_bins SOURCES tst_bins.cpp LIBS lumenis::model)
lumenis_add_test(tst_bin_lock SOURCES tst_bin_lock.cpp LIBS lumenis::model)
lumenis_add_test(tst_transcript SOURCES tst_transcript.cpp LIBS lumenis::model)
lumenis_add_test(tst_script SOURCES tst_script.cpp LIBS lumenis::model)
lumenis_add_test(tst_bin_attic SOURCES tst_bin_attic.cpp LIBS lumenis::model)
lumenis_add_test(tst_effect_keyframes SOURCES tst_effect_keyframes.cpp LIBS lumenis::model)
# Rendering video effects: targets, keys and Clear Renders (docs/spec/rendering.md).
lumenis_add_test(tst_effect_render SOURCES tst_effect_render.cpp LIBS lumenis::model)
lumenis_add_test(tst_motion_effect SOURCES tst_motion_effect.cpp LIBS lumenis::model)
# Mixed rate clips and Motion Adapters (Editing Guide p.468-478).
# Group clips for MultiCamera editing (Editing Guide p.1123-1131).
lumenis_add_test(tst_group_clip SOURCES tst_group_clip.cpp LIBS lumenis::model)
lumenis_add_test(tst_sync_clips SOURCES tst_sync_clips.cpp LIBS lumenis::model)
lumenis_add_test(tst_motion_adapter SOURCES tst_motion_adapter.cpp LIBS lumenis::model)
# Nested effects and Collapse (FX Guide p.219-227).
lumenis_add_test(tst_nest SOURCES tst_nest.cpp LIBS lumenis::model)
lumenis_add_test(tst_effect_template SOURCES tst_effect_template.cpp LIBS lumenis::model)
lumenis_add_test(tst_monitor SOURCES tst_monitor.cpp LIBS lumenis::model)
lumenis_add_test(tst_sequence SOURCES tst_sequence.cpp LIBS lumenis::model)
# Set Format on sequences (docs/spec/sequence-format.md).
lumenis_add_test(tst_sequence_format SOURCES tst_sequence_format.cpp LIBS lumenis::model)
lumenis_add_test(tst_subcap SOURCES tst_subcap.cpp LIBS lumenis::model)
lumenis_add_test(tst_closed_captions SOURCES tst_closed_captions.cpp LIBS lumenis::model)
lumenis_add_test(tst_multichannel SOURCES tst_multichannel.cpp LIBS lumenis::model)
lumenis_add_test(tst_automation SOURCES tst_automation.cpp LIBS lumenis::model)
lumenis_add_test(tst_settings SOURCES tst_settings.cpp LIBS lumenis::model)
lumenis_add_test(tst_calculator SOURCES tst_calculator.cpp LIBS lumenis::model)
# Relationship colour corrections (FX Guide p.596-598).
lumenis_add_test(tst_correction_relations SOURCES tst_correction_relations.cpp LIBS lumenis::model)
lumenis_add_test(tst_safe_colors SOURCES tst_safe_colors.cpp LIBS lumenis::model)
lumenis_add_test(tst_correction_manage SOURCES tst_correction_manage.cpp LIBS lumenis::model)
# Keyframing colour corrections (FX Guide p.683-686).
lumenis_add_test(tst_correction_keyframes SOURCES tst_correction_keyframes.cpp LIBS lumenis::model)

lumenis_isolate_media_drives()
