cmake_minimum_required(VERSION 3.22)

# ─────────────────────────────────────────────────────────────────────────────
# Identity
#
# PLUGIN_CODE and PLUGIN_MANUFACTURER_CODE are baked into the plugin's unique
# ID. Changing them later makes every host treat this as a *different* plugin
# and silently drop it from already-saved projects. Pick them once, now.
# Both must be exactly 4 characters with at least one uppercase letter.
# ─────────────────────────────────────────────────────────────────────────────
set(PROJECT_NAME             "MixingPlugin")
set(PRODUCT_NAME             "Mixing Plugin")
set(COMPANY_NAME             "Ivo Di Barros")
set(BUNDLE_ID                "com.ivodibarros.mixingplugin")
set(PLUGIN_MANUFACTURER_CODE Ivdb)
set(PLUGIN_CODE              Mxp1)

project(${PROJECT_NAME} VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# JUCE
#
# Fetched rather than vendored as a submodule, so `git clone && cmake -B build`
# works on a fresh machine with nothing to remember to init. The download is
# cached in build/_deps, so it only happens on the first configure.
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ─────────────────────────────────────────────────────────────────────────────
# The plugin target
# ─────────────────────────────────────────────────────────────────────────────
juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME                "${COMPANY_NAME}"
    BUNDLE_ID                   "${BUNDLE_ID}"
    PRODUCT_NAME                "${PRODUCT_NAME}"
    PLUGIN_MANUFACTURER_CODE    ${PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE                 ${PLUGIN_CODE}

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Drops the built .vst3 into ~/.vst3 so a rebuild is immediately visible to
    # the host. Rescan in the host after the first build only.
    COPY_PLUGIN_AFTER_BUILD     TRUE

    # Add LV2 here if you want it; CLAP needs the clap-juce-extensions wrapper.
    FORMATS                     VST3 Standalone)

target_sources(${PROJECT_NAME} PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/Parameters.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE source)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    JUCE_WEB_BROWSER=0          # drops the libwebkit2gtk-dev build dependency
    JUCE_USE_CURL=0             # drops the libcurl-dev build dependency
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    # Note: JUCE_DISPLAY_SPLASH_SCREEN is deliberately left at its default.
    # Disabling it requires an appropriate JUCE licence.
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ─────────────────────────────────────────────────────────────────────────────
# Tests
#
# The dsp/ headers deliberately avoid JUCE where they can, so these build and
# run in about a second with no framework and no plugin. They also run without
# CMake at all, which is what makes them usable before the toolchain is set up:
#
#   g++ -std=c++20 -O2 -Isource -o /tmp/biquad_test tests/biquad_test.cpp && /tmp/biquad_test
# ─────────────────────────────────────────────────────────────────────────────
option(MIXINGPLUGIN_BUILD_TESTS "Build the DSP unit tests" ON)

if(MIXINGPLUGIN_BUILD_TESTS)
    enable_testing()

    add_executable(biquad_test tests/biquad_test.cpp)
    target_include_directories(biquad_test PRIVATE source)
    target_compile_features(biquad_test PRIVATE cxx_std_20)
    target_compile_options(biquad_test PRIVATE -Wall -Wextra)

    add_test(NAME biquad COMMAND biquad_test)
endif()
