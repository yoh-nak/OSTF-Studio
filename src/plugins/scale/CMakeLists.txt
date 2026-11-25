# This instrument hosts another VST3 and therefore needs its own JUCE host modules.
juce_add_binary_data(OSTFScaleAdviser NAMESPACE ScaleAssets HEADER_NAME ScaleAssets.h SOURCES Adviser.js AdviserBridge.js)
juce_add_plugin(OSTFSynthScaleEditor COMPANY_NAME "OSTF Studio" BUNDLE_ID "org.ostf.instrument.Ose1"
    PLUGIN_MANUFACTURER_CODE Ostf PLUGIN_CODE Ose1 PRODUCT_NAME "OSTF Scale Editor"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE FORMATS VST3 VST3_CATEGORIES Instrument Synth
    VST3_COPY_DIR "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>" COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(OSTFSynthScaleEditor PRIVATE ScaleEditor.cpp)
target_include_directories(OSTFSynthScaleEditor PRIVATE "${ostf_vst_sdk}")
target_compile_definitions(OSTFSynthScaleEditor PUBLIC JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OSTFSynthScaleEditor PRIVATE ostf_scale_document OSTFScaleAdviser juce::juce_audio_processors
    juce::juce_javascript juce::juce_recommended_config_flags)
ostf_attach_notices(OSTFSynthScaleEditor_VST3)
juce_enable_copy_plugin_step(OSTFSynthScaleEditor)

add_executable(ostf_scale_instrument_tests ../../../tests/scale_instrument_tests.cpp)
target_compile_definitions(ostf_scale_instrument_tests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(ostf_scale_instrument_tests PRIVATE "${JUCE_MODULES_DIR}")
target_link_libraries(ostf_scale_instrument_tests PRIVATE OSTFSynthScaleEditor juce::juce_recommended_config_flags)
add_test(NAME ostf_scale_instrument_tests COMMAND ostf_scale_instrument_tests
    "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF FM Synth.vst3"
    "${CMAKE_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostf")
