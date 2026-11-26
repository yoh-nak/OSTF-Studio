# This instrument hosts another VST3 and therefore needs its own JUCE host modules.
if(WIN32 AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
    FetchContent_Declare(ostf_webview2
        URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3485.44/microsoft.web.webview2.1.0.3485.44.nupkg
        URL_HASH SHA256=bc09150b179246ac90189649b13be8e6b11b3ac200e817e18df106e1f3cf489e
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/Microsoft.Web.WebView2-1.0.3485.44")
    FetchContent_MakeAvailable(ostf_webview2)
    get_filename_component(JUCE_WEBVIEW2_PACKAGE_LOCATION "${ostf_webview2_SOURCE_DIR}" DIRECTORY)
endif()
juce_add_binary_data(OSTFScaleAdviser NAMESPACE ScaleAssets HEADER_NAME ScaleAssets.h SOURCES Adviser.js AdviserBridge.js ScaleWorkspace.html)
juce_add_plugin(OSTFSynthScaleEditor COMPANY_NAME "OSTF Studio" BUNDLE_ID "org.ostf.instrument.Ose1"
    PLUGIN_MANUFACTURER_CODE Ostf PLUGIN_CODE Ose1 PRODUCT_NAME "OSTF Scale Editor"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE NEEDS_WEBVIEW2 TRUE NEEDS_WEB_BROWSER TRUE FORMATS VST3 VST3_CATEGORIES Instrument Synth
    VST3_COPY_DIR "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>" COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(OSTFSynthScaleEditor PRIVATE ScaleEditor.cpp)
if(MSVC)
    set_property(SOURCE ScaleEditor.cpp APPEND PROPERTY COMPILE_OPTIONS /utf-8)
endif()
target_include_directories(OSTFSynthScaleEditor PRIVATE "${ostf_vst_sdk}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(OSTFSynthScaleEditor PUBLIC JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)
target_link_libraries(OSTFSynthScaleEditor PRIVATE ostf_scale_document OSTFScaleAdviser juce::juce_audio_processors
    juce::juce_javascript juce::juce_recommended_config_flags)
ostf_attach_notices(OSTFSynthScaleEditor_VST3)
if(WIN32)
    set(webview_notices "$<TARGET_FILE_DIR:OSTFSynthScaleEditor_VST3>/../Resources/Licenses/WebView2")
    add_custom_command(TARGET OSTFSynthScaleEditor_VST3 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${webview_notices}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${WebView2_root_dir}/LICENSE.txt"
            "${WebView2_root_dir}/NOTICE.txt" "${webview_notices}" VERBATIM)
endif()
juce_enable_copy_plugin_step(OSTFSynthScaleEditor)

add_executable(ostf_scale_instrument_tests ../../../tests/scale_instrument_tests.cpp)
target_compile_definitions(ostf_scale_instrument_tests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(ostf_scale_instrument_tests PRIVATE "${JUCE_MODULES_DIR}")
target_link_libraries(ostf_scale_instrument_tests PRIVATE OSTFSynthScaleEditor juce::juce_recommended_config_flags)
add_test(NAME ostf_scale_instrument_tests COMMAND ostf_scale_instrument_tests
    "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF FM Synth.vst3"
    "${CMAKE_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostf")
if(WIN32 OR APPLE)
    add_dependencies(ostf_scale_instrument_tests OSTFSynthScaleEditor_VST3)
    add_test(NAME ostf_scale_linked_save COMMAND ostf_scale_instrument_tests
        "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF Scale Editor.vst3"
        "${CMAKE_SOURCE_DIR}/examples/demo-songs/neon-platforms.ostf" --linked-save)
    add_test(NAME ostf_scale_workspace COMMAND ostf_scale_instrument_tests
        "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF FM Synth.vst3"
        "${CMAKE_SOURCE_DIR}/examples/demo-songs/neon-platforms.ostf" --workspace)
    add_test(NAME ostf_scale_workspace_recall COMMAND ostf_scale_instrument_tests
        "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF FM Synth.vst3"
        "${CMAKE_SOURCE_DIR}/examples/demo-songs/neon-platforms.ostf" --recall)
endif()
