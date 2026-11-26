add_library(ostf_rack_host STATIC Rack.cpp)
set_target_properties(ostf_rack_host PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_compile_definitions(ostf_rack_host PUBLIC JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ostf_rack_host PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)

add_executable(ostf_plugin_rack_tests ../../../tests/plugin_rack_tests.cpp)
target_compile_definitions(ostf_plugin_rack_tests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(ostf_plugin_rack_tests PRIVATE "${JUCE_MODULES_DIR}")
target_link_libraries(ostf_plugin_rack_tests PRIVATE ostf_rack_host juce::juce_recommended_config_flags)
add_test(NAME ostf_plugin_rack_tests COMMAND ostf_plugin_rack_tests
    "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF FM Synth.vst3"
    "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF EQ.vst3")
