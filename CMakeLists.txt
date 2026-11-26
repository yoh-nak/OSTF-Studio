cmake_minimum_required(VERSION 3.22)
project(OSTFStudio VERSION 1.0.0 LANGUAGES C CXX)
set(OSTF_STUDIO_ALPHA_NUMBER 3)
if(NOT DEFINED OSTF_STUDIO_DISPLAY_VERSION)
    set(OSTF_STUDIO_DISPLAY_VERSION "${PROJECT_VERSION}-alpha.${OSTF_STUDIO_ALPHA_NUMBER}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OSTF_STUDIO_BUILD_GUI "Build JUCE desktop UI prototype" ON)
option(OSTF_STUDIO_ENABLE_ASIO "Compile the Windows ASIO backend using JUCE's bundled headers" ON)

enable_testing()
add_library(ostf_studio_core STATIC src/core/TimelineModel.cpp)
target_include_directories(ostf_studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ostf_studio_core PUBLIC cxx_std_20)

add_executable(ostf_studio_core_tests tests/timeline_model_tests.cpp)
target_link_libraries(ostf_studio_core_tests PRIVATE ostf_studio_core)
add_test(NAME ostf_studio_core_tests COMMAND ostf_studio_core_tests)

add_executable(ostf_analyzer_meter_tests tests/analyzer_meter_tests.cpp)
target_include_directories(ostf_analyzer_meter_tests PRIVATE src/plugins/effects)
add_test(NAME ostf_analyzer_meter_tests COMMAND ostf_analyzer_meter_tests)

if (OSTF_STUDIO_BUILD_GUI)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    FetchContent_Declare(ableton_link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG 45931b8bd2a7066f1950f2a0beab81a0c51f28fe
        GIT_SUBMODULES modules/asio-standalone
        SOURCE_SUBDIR ostf-no-examples)
    FetchContent_MakeAvailable(ableton_link)
    include("${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake")
    add_library(ostf_link STATIC src/ui/LinkSession.cpp)
    target_include_directories(ostf_link PUBLIC src)
    target_link_libraries(ostf_link PRIVATE Ableton::Link)
    if(WIN32)
        target_link_libraries(ostf_link PRIVATE ws2_32 iphlpapi winmm)
    endif()
    add_executable(ostf_link_tests tests/link_session_tests.cpp)
    target_link_libraries(ostf_link_tests PRIVATE ostf_link)
    add_test(NAME link_transport COMMAND ostf_link_tests)

    set(JSONCONS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(jsoncons
        URL https://codeload.github.com/danielaparker/jsoncons/zip/refs/tags/v1.5.0
        URL_HASH SHA256=6de746d3fd381937c9fc58ed47df7404792881b9ba924d43124dc6fb7c22f741)
    FetchContent_MakeAvailable(jsoncons)
    set(score_schemas)
    foreach(version 3.5.0 4.0.0 4.1.0 4.2.0 4.3.0 5.0.0 5.1.0 package-manifest-1.1 production-session-0.4)
        list(APPEND score_schemas "third_party/ostf-schemas/ostf-${version}.schema.json")
    endforeach()
    juce_add_binary_data(OSTFScoreSchemas HEADER_NAME ScoreSchemas.h NAMESPACE ScoreSchemas SOURCES ${score_schemas})
    add_library(ostf_score_validation STATIC src/core/ScoreSchema.cpp)
    target_link_libraries(ostf_score_validation PRIVATE jsoncons OSTFScoreSchemas)
    if(MSVC)
        target_compile_options(ostf_score_validation PRIVATE /bigobj)
    endif()

    set(effect_catalog "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/effects/catalog.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${effect_catalog}")
    file(READ "${effect_catalog}" effects)
    string(JSON effect_count LENGTH "${effects}")
    math(EXPR last_effect "${effect_count}-1")
    # Share the JUCE module objects across the stock effect wrappers.
    add_library(ostf_effect_dsp STATIC src/plugins/effects/StudioEffect.cpp src/plugins/effects/MixingEffects.cpp src/plugins/instruments/StudioInstrument.cpp)
    set_target_properties(ostf_effect_dsp PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_compile_definitions(ostf_effect_dsp PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(ostf_effect_dsp PRIVATE juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
    target_include_directories(ostf_effect_dsp INTERFACE "$<TARGET_PROPERTY:juce::juce_core,INTERFACE_JUCE_MODULE_PATH>")

    set(ostf_vst_sdk "${JUCE_MODULES_DIR}/juce_audio_processors_headless/format_types/VST3_SDK")
    add_library(ostf_scale_document STATIC src/plugins/scale/ScaleDocument.cpp
                "${JUCE_MODULES_DIR}/juce_cryptography/juce_cryptography.cpp")
    set_target_properties(ostf_scale_document PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_link_libraries(ostf_scale_document PRIVATE jsoncons ostf_score_validation ostf_effect_dsp)
    juce_add_plugin(OSTFGMSoundModule
        COMPANY_NAME "OSTF Studio"
        BUNDLE_ID "org.ostf.gmsoundmodule"
        PLUGIN_MANUFACTURER_CODE Ostf
        PLUGIN_CODE Ogm1
        PRODUCT_NAME "OSTF GM Sound Module" PLUGIN_NAME "GM Sound Module"
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        VST3_CATEGORIES Instrument Synth
    )
    target_sources(OSTFGMSoundModule PRIVATE src/plugins/GMSoundModule.cpp)
    target_include_directories(OSTFGMSoundModule PRIVATE third_party/tinysoundfont)
    target_compile_definitions(OSTFGMSoundModule PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_CURL=0)
    juce_add_binary_data(OSTFGMPreviewBank HEADER_NAME GMPreviewData.h NAMESPACE GMPreviewData SOURCES assets/StudioPreview.sf2)
    target_link_libraries(OSTFGMSoundModule PRIVATE OSTFGMPreviewBank juce::juce_audio_processors juce::juce_recommended_config_flags)
    set(OSTF_FLUID_R3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/fluid-r3")
    if(EXISTS "${OSTF_FLUID_R3_DIR}/FluidR3_GM.sf2" AND EXISTS "${OSTF_FLUID_R3_DIR}/FluidR3-License.txt")
        add_custom_command(TARGET OSTFGMSoundModule_VST3 POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:OSTFGMSoundModule_VST3>/../Resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${OSTF_FLUID_R3_DIR}/FluidR3_GM.sf2" "${OSTF_FLUID_R3_DIR}/FluidR3-License.txt"
                "$<TARGET_FILE_DIR:OSTFGMSoundModule_VST3>/../Resources")
    endif()

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/DocumentTypes.plist" ostf_document_types)
    if(MSVC)
        set_property(SOURCE tests/track_view_smoke.cpp tests/notation_smoke.cpp tests/ostfp_preview_tests.cpp
                     APPEND PROPERTY COMPILE_OPTIONS /utf-8)
    endif()
    juce_add_gui_app(OSTFStudio
        PRODUCT_NAME "OSTF Studio"
        COMPANY_NAME "OSTF"
        BUNDLE_ID "org.ostf.studio"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "OSTF Studio uses the selected audio input to record audio clips and display input levels."
        ICON_BIG "assets/AppIcon.png"
        PLIST_TO_MERGE "${ostf_document_types}"
    )
    if (APPLE)
        set(ostf_document_icons assets/OstfScore.icns assets/OstfpPackage.icns)
        target_sources(OSTFStudio PRIVATE ${ostf_document_icons})
        set_source_files_properties(${ostf_document_icons} PROPERTIES MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
    endif()
    juce_add_binary_data(OSTFEnglishLexicon HEADER_NAME EnglishLexiconData.h NAMESPACE EnglishLexiconData SOURCES assets/voices/english-lexicon.gz)
    target_link_libraries(OSTFStudio PRIVATE OSTFEnglishLexicon)
    juce_add_binary_data(OSTFStudioAssets SOURCES
        assets/AppIcon.png assets/Japanese.txt assets/NewProject.ostf assets/GMFamilies.json
        examples/demo-songs/neon-platforms.ostfp examples/demo-songs/blue-hour-waltz.ostfp
        examples/demo-songs/blue-hour-waltz-shared-gm.ostfp
        examples/demo-songs/relay-circuit.ostfp examples/demo-songs/paper-lantern-replies.ostfp
        examples/demo-songs/offset-bridges.ostfp examples/demo-songs/copper-mile.ostfp examples/demo-songs/first-light-vector.ostfp)
    juce_generate_juce_header(OSTFStudio)
    target_sources(OSTFStudio PRIVATE src/app/Main.cpp src/ui/MainComponent.cpp src/ui/SystemUsageMeter.cpp src/ui/ArrangementClipEdits.cpp src/ui/ChordReview.cpp src/ui/OstfpPreview.cpp
        src/ui/DeviceSettingsWindow.cpp src/ui/MixerPanel.cpp src/ui/PluginBrowser.cpp src/ui/PreviewTransport.cpp
        src/ui/SongMidi.cpp src/ui/StudioLanguage.cpp src/ui/Notation.cpp src/ui/ScoreView.cpp src/ui/AnalysisService.cpp src/ui/ScorePdfPages.cpp src/ui/MediaImport.cpp src/ui/StemTranscription.cpp src/ui/MusicXmlImport.cpp
        src/ui/TheoryGuide.cpp tests/stock_effects_smoke.cpp tests/track_view_smoke.cpp tests/notation_smoke.cpp
        tests/transport_smoke.cpp tests/piano_edit_smoke.cpp tests/theory_guide_smoke.cpp)
    set_property(SOURCE src/ui/MainComponent.cpp tests/stock_effects_smoke.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${ostf_vst_sdk}")
    target_include_directories(OSTFStudio PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(OSTFStudio PRIVATE OSTF_STUDIO_DISPLAY_VERSION="${OSTF_STUDIO_DISPLAY_VERSION}")
    target_compile_definitions(OSTFStudio PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_VST3_CAN_REPLACE_VST2=0)
    if (WIN32)
        target_compile_definitions(OSTFStudio PRIVATE JUCE_ASIO=$<BOOL:${OSTF_STUDIO_ENABLE_ASIO}>)
    endif()
    target_link_libraries(OSTFStudio PRIVATE
        ostf_link
        ostf_studio_core
        ostf_score_validation
        OSTFStudioAssets
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_recommended_config_flags
    )
    if (WIN32)
        target_sources(OSTFStudio PRIVATE src/ui/NativePrint.cpp)
        target_link_libraries(OSTFStudio PRIVATE comdlg32 gdi32)
    elseif (APPLE)
        enable_language(OBJCXX)
        set_target_properties(OSTFStudio PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
        target_sources(OSTFStudio PRIVATE src/ui/NativePrint.mm)
        set_source_files_properties(src/ui/NativePrint.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(OSTFStudio PRIVATE "-framework PDFKit")
    endif()
    if (WIN32 AND MSVC)
        target_link_libraries(OSTFStudio PRIVATE windowsapp shcore shlwapi windowscodecs)
    elseif (APPLE)
        target_link_libraries(OSTFStudio PRIVATE "-framework CoreGraphics" "-framework ImageIO")
    endif()
    include(packaging/collect-notices.cmake)
    add_subdirectory(src/plugins/scale)
    add_subdirectory(src/plugins/rack)
    add_subdirectory(src/plugins/effects)
    add_dependencies(ostf_scale_instrument_tests OSTFSynthFM_VST3)
    configure_file(src/plugins/effects/StockEffects.h.in generated/StockEffects.h @ONLY)

    add_executable(ostf_gm_channel_activity_tests tests/gm_channel_activity_tests.cpp src/ui/PreviewTransport.cpp
        src/ui/OstfpPreview.cpp src/ui/SongMidi.cpp)
    target_include_directories(ostf_gm_channel_activity_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(ostf_gm_channel_activity_tests PRIVATE OSTFGMSoundModule ostf_effect_dsp ostf_link
        OSTFEnglishLexicon ostf_score_validation ostf_studio_core juce::juce_audio_devices juce::juce_audio_formats
        juce::juce_cryptography juce::juce_recommended_config_flags)
    add_test(NAME ostf_gm_channel_activity_tests COMMAND ostf_gm_channel_activity_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/neon-platforms.ostfp")

    add_executable(ostf_studio_chatgpt_tests tests/chatgpt_connection_tests.cpp src/ui/AnalysisService.cpp)
    target_include_directories(ostf_studio_chatgpt_tests PRIVATE src)
    target_compile_definitions(ostf_studio_chatgpt_tests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(ostf_studio_chatgpt_tests PRIVATE juce::juce_core)
    add_test(NAME ostf_studio_chatgpt_connection COMMAND ostf_studio_chatgpt_tests)

    add_executable(ostf_studio_ostfp_tests tests/ostfp_preview_tests.cpp src/ui/OstfpPreview.cpp src/ui/SongMidi.cpp)
    target_include_directories(ostf_studio_ostfp_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(ostf_studio_ostfp_tests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(ostf_studio_ostfp_tests PRIVATE ostf_score_validation juce::juce_cryptography juce::juce_audio_formats)
    add_test(NAME ostf_studio_ostfp_tests COMMAND ostf_studio_ostfp_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/10-comprehensive-core.ostfp")
    foreach(version 3.5.0 4.0.0 4.1.0 4.2.0 4.3.0)
        add_test(NAME ostf_studio_read_${version} COMMAND ostf_studio_ostfp_tests --legacy
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy/${version}.ostf")
    endforeach()
    add_test(NAME ostf_studio_demo_gallery COMMAND ostf_studio_ostfp_tests --gallery
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs")
    add_test(NAME ostf_studio_song_midi COMMAND ostf_studio_ostfp_tests --smf
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostf")
    add_test(NAME ostf_studio_languages COMMAND ostf_studio_ostfp_tests --languages
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Japanese.txt")
    if(WIN32 AND OSTF_STUDIO_ENABLE_ASIO)
        add_test(NAME ostf_studio_asio_backend COMMAND OSTFStudio --asio-backend-smoke)
        set_tests_properties(ostf_studio_asio_backend PROPERTIES TIMEOUT 15)
    endif()
    if (WIN32 OR APPLE)
        add_test(NAME ostf_studio_startup COMMAND OSTFStudio --startup-smoke)
        add_test(NAME ostf_studio_transport COMMAND OSTFStudio --transport-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/10-comprehensive-core.ostfp" "${CMAKE_CURRENT_BINARY_DIR}/transport-check")
        set_tests_properties(ostf_studio_transport PROPERTIES TIMEOUT 90)
        add_test(NAME ostf_studio_notation COMMAND OSTFStudio --notation-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostfp" "${CMAKE_CURRENT_BINARY_DIR}/notation-check")
        set_tests_properties(ostf_studio_notation PROPERTIES TIMEOUT 120)
        add_test(NAME ostf_native_comps COMMAND OSTFStudio --native-comp-smoke "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/26-midi-comp.ostf")
        add_test(NAME ostf_studio_live_session COMMAND OSTFStudio --live-session-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostf")
        add_test(NAME ostf_studio_track_view COMMAND OSTFStudio --track-view-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/10-comprehensive-core.ostfp")
        set_tests_properties(ostf_studio_track_view PROPERTIES TIMEOUT 120)
        add_test(NAME ostf_studio_piano_edits COMMAND OSTFStudio --piano-edit-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/10-comprehensive-core.ostfp")
        set_tests_properties(ostf_studio_piano_edits PROPERTIES TIMEOUT 60)
        add_test(NAME ostf_studio_theory_guide COMMAND OSTFStudio --theory-guide-smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/neon-platforms.ostf")
        set_tests_properties(ostf_studio_theory_guide PROPERTIES TIMEOUT 60)
        add_test(NAME ostf_studio_open_project
            COMMAND OSTFStudio --startup-smoke "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/10-comprehensive-core.ostfp")
        add_test(NAME ostf_studio_open_score
            COMMAND OSTFStudio --startup-smoke "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/paper-lantern-replies.ostf")
        set_tests_properties(ostf_studio_open_project ostf_studio_open_score PROPERTIES TIMEOUT 15)
        set_tests_properties(ostf_studio_startup PROPERTIES TIMEOUT 15)
        add_test(NAME ostf_studio_gm_plugin
            COMMAND OSTFStudio --gm-plugin-smoke
            "$<TARGET_FILE_DIR:OSTFGMSoundModule_VST3>/../.."
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tinysoundfont-saw.sf2")
        set_tests_properties(ostf_studio_gm_plugin PROPERTIES TIMEOUT 45)
        add_test(NAME ostf_studio_instruments COMMAND OSTFStudio --studio-instruments-smoke
            "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/first-light-vector.ostfp")
        set_tests_properties(ostf_studio_instruments PROPERTIES TIMEOUT 90)
        add_test(NAME ostf_studio_stock_effects COMMAND OSTFStudio --stock-effects-smoke
            "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>" "$<TARGET_FILE_DIR:OSTFGMSoundModule_VST3>/../.."
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo-songs/blue-hour-waltz.ostfp")
        set_tests_properties(ostf_studio_stock_effects PROPERTIES TIMEOUT 90)
        add_test(NAME ostf_studio_plugin_focus COMMAND OSTFStudio --plugin-focus-smoke
            "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF Harmonizer.vst3")
        set_tests_properties(ostf_studio_plugin_focus PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME ostf_scale_host_resize COMMAND OSTFStudio --plugin-focus-smoke
            "${CMAKE_BINARY_DIR}/StudioEffects/$<CONFIG>/OSTF Scale Editor.vst3")
        set_tests_properties(ostf_scale_host_resize PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    endif()
endif()
