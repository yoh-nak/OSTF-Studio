cmake_minimum_required(VERSION 3.22)
project(OSTFStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OSTF_STUDIO_BUILD_GUI "Build JUCE desktop UI prototype" ON)

enable_testing()
add_library(ostf_studio_core STATIC src/core/TimelineModel.cpp)
target_include_directories(ostf_studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ostf_studio_core PUBLIC cxx_std_20)

add_executable(ostf_studio_core_tests tests/timeline_model_tests.cpp)
target_link_libraries(ostf_studio_core_tests PRIVATE ostf_studio_core)
add_test(NAME ostf_studio_core_tests COMMAND ostf_studio_core_tests)

if (OSTF_STUDIO_BUILD_GUI)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(OSTFStudio
        PRODUCT_NAME "OSTF Studio"
        COMPANY_NAME "OSTF"
        BUNDLE_ID "org.ostf.studio"
    )
    juce_generate_juce_header(OSTFStudio)
    target_sources(OSTFStudio PRIVATE src/app/Main.cpp src/ui/MainComponent.cpp)
    target_include_directories(OSTFStudio PRIVATE src)
    target_compile_definitions(OSTFStudio PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(OSTFStudio PRIVATE
        ostf_studio_core
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )
endif()
