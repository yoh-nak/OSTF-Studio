# Build a notice directory that stays with the app and with each independently installed VST3.
set(ostf_licenses "${CMAKE_CURRENT_BINARY_DIR}/distribution-licenses")
file(MAKE_DIRECTORY "${ostf_licenses}")
configure_file(LICENSE "${ostf_licenses}/GPL-3.0.txt" COPYONLY)
configure_file(licenses/AGPL-3.0.txt "${ostf_licenses}/AGPL-3.0.txt" COPYONLY)
configure_file(licenses/NOTICE.md "${ostf_licenses}/NOTICE.md" COPYONLY)
configure_file(third_party/tinysoundfont/LICENSE.txt "${ostf_licenses}/TinySoundFont.txt" COPYONLY)
configure_file("${juce_SOURCE_DIR}/LICENSE.md" "${ostf_licenses}/JUCE.md" COPYONLY)
configure_file("${juce_SOURCE_DIR}/JUCE.spdx.json" "${ostf_licenses}/JUCE.spdx.json" COPYONLY)
if(EXISTS "${OSTF_FLUID_R3_DIR}/FluidR3-License.txt")
    configure_file("${OSTF_FLUID_R3_DIR}/FluidR3-License.txt" "${ostf_licenses}/FluidR3.txt" COPYONLY)
endif()
file(GLOB_RECURSE juce_notices LIST_DIRECTORIES FALSE RELATIVE "${juce_SOURCE_DIR}"
    "${juce_SOURCE_DIR}/modules/*LICENSE*" "${juce_SOURCE_DIR}/modules/*COPYING*"
    "${juce_SOURCE_DIR}/modules/*Licence.txt" "${juce_SOURCE_DIR}/modules/*license.txt")
set(notice_text "")
foreach(notice IN LISTS juce_notices)
    file(READ "${juce_SOURCE_DIR}/${notice}" notice_contents)
    string(APPEND notice_text "\n\n${notice}\n\n${notice_contents}")
endforeach()
file(WRITE "${ostf_licenses}/JUCE-Third-Party-Notices.txt" "${notice_text}")

function(ostf_attach_notices target)
    if(WIN32 AND target STREQUAL "OSTFStudio")
        set(destination "$<TARGET_FILE_DIR:${target}>/Licenses")
    else()
        set(destination "$<TARGET_FILE_DIR:${target}>/../Resources/Licenses")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ostf_licenses}" "${destination}" VERBATIM)
    if(APPLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>" VERBATIM)
    endif()
endfunction()

foreach(target IN ITEMS OSTFStudio OSTFArranger_VST3 OSTFGMSoundModule_VST3)
    ostf_attach_notices(${target})
endforeach()
