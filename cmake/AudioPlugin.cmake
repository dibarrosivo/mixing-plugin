# ─────────────────────────────────────────────────────────────────────────────
# add_audio_plugin()
#
# Declares a JUCE plugin target with this repo's shared conventions already
# applied — company identity, formats, module trims, warning flags, and a link
# to the shared DSP library.
#
# Usage:
#
#   add_audio_plugin(
#       TARGET       MixingPlugin                    # CMake target + artefact dir
#       PRODUCT_NAME "Mixing Plugin"                 # what the host displays
#       PLUGIN_CODE  Mxp1                            # 4 chars, >=1 uppercase, UNIQUE
#       DESCRIPTION  "Single-band parametric EQ"
#       SOURCES      source/PluginProcessor.cpp ...
#   )
#
# Optional: BUNDLE_ID (defaults to <domain>.<lowercased target>),
#           FORMATS (defaults to AUDIO_DEFAULT_FORMATS),
#           EXTRA_LIBS, IS_SYNTH, NEEDS_MIDI_INPUT.
# ─────────────────────────────────────────────────────────────────────────────

function(add_audio_plugin)
    set(options       IS_SYNTH NEEDS_MIDI_INPUT)
    set(oneValueArgs  TARGET PRODUCT_NAME PLUGIN_CODE BUNDLE_ID DESCRIPTION)
    set(multiValueArgs SOURCES FORMATS EXTRA_LIBS)
    cmake_parse_arguments(AP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # ── Validation ───────────────────────────────────────────────────────────
    foreach(required TARGET PRODUCT_NAME PLUGIN_CODE SOURCES)
        if(NOT AP_${required})
            message(FATAL_ERROR "add_audio_plugin: ${required} is required")
        endif()
    endforeach()

    string(LENGTH "${AP_PLUGIN_CODE}" _code_len)
    if(NOT _code_len EQUAL 4)
        message(FATAL_ERROR
            "add_audio_plugin(${AP_TARGET}): PLUGIN_CODE must be exactly 4 characters, "
            "got '${AP_PLUGIN_CODE}' (${_code_len}).")
    endif()

    if(NOT AP_PLUGIN_CODE MATCHES "[A-Z]")
        message(FATAL_ERROR
            "add_audio_plugin(${AP_TARGET}): PLUGIN_CODE '${AP_PLUGIN_CODE}' needs at least "
            "one uppercase letter — some hosts reject all-lowercase codes.")
    endif()

    # Two plugins sharing a code makes hosts treat them as the same plugin and
    # silently load the wrong one. It is near-impossible to diagnose from the
    # host side, so it is a hard error here.
    get_property(_seen GLOBAL PROPERTY AUDIO_PLUGIN_CODES)
    if("${AP_PLUGIN_CODE}" IN_LIST _seen)
        message(FATAL_ERROR
            "add_audio_plugin(${AP_TARGET}): PLUGIN_CODE '${AP_PLUGIN_CODE}' is already used by "
            "another plugin in this repo. Codes must be unique or hosts will confuse them.")
    endif()
    set_property(GLOBAL APPEND PROPERTY AUDIO_PLUGIN_CODES "${AP_PLUGIN_CODE}")
    set_property(GLOBAL APPEND PROPERTY AUDIO_PLUGIN_SUMMARY "${AP_TARGET}(${AP_PLUGIN_CODE})")

    # ── Defaults ─────────────────────────────────────────────────────────────
    if(NOT AP_FORMATS)
        set(AP_FORMATS ${AUDIO_DEFAULT_FORMATS})
    endif()

    if(NOT AP_BUNDLE_ID)
        string(TOLOWER "${AP_TARGET}" _lower_target)
        set(AP_BUNDLE_ID "${AUDIO_COMPANY_DOMAIN}.${_lower_target}")
    endif()

    if(AP_IS_SYNTH)
        set(_is_synth TRUE)
        set(_needs_midi TRUE)
    else()
        set(_is_synth FALSE)
        set(_needs_midi ${AP_NEEDS_MIDI_INPUT})
    endif()

    # ── The target ───────────────────────────────────────────────────────────
    juce_add_plugin(${AP_TARGET}
        COMPANY_NAME                "${AUDIO_COMPANY_NAME}"
        BUNDLE_ID                   "${AP_BUNDLE_ID}"
        PRODUCT_NAME                "${AP_PRODUCT_NAME}"
        DESCRIPTION                 "${AP_DESCRIPTION}"
        PLUGIN_MANUFACTURER_CODE    ${AUDIO_MANUFACTURER_CODE}
        PLUGIN_CODE                 ${AP_PLUGIN_CODE}

        IS_SYNTH                    ${_is_synth}
        NEEDS_MIDI_INPUT            ${_needs_midi}
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE

        COPY_PLUGIN_AFTER_BUILD     TRUE
        FORMATS                     ${AP_FORMATS})

    target_sources(${AP_TARGET} PRIVATE ${AP_SOURCES})

    target_include_directories(${AP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source")

    target_compile_definitions(${AP_TARGET} PUBLIC
        JUCE_WEB_BROWSER=0          # drops the libwebkit2gtk-dev dependency
        JUCE_USE_CURL=0             # drops the libcurl-dev dependency
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        # JUCE_DISPLAY_SPLASH_SCREEN is left at its default deliberately.
        # Disabling it requires an appropriate JUCE licence — see README.
    )

    target_link_libraries(${AP_TARGET}
        PRIVATE
            audio::dsp
            audio::ui
            juce::juce_audio_utils
            juce::juce_dsp
            ${AP_EXTRA_LIBS}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()
