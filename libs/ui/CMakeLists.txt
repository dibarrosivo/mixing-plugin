# Shared UI. Header-only, same pattern as libs/dsp.
#
# Every plugin in the repo draws from this so the four modules of the vocal
# chain read as one product rather than four. Colours, the curve display and
# the frequency grid all live here rather than in any single plugin.

add_library(audio_ui INTERFACE)
add_library(audio::ui ALIAS audio_ui)

target_include_directories(audio_ui INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(audio_ui INTERFACE
    audio::dsp_core
    juce::juce_gui_basics)
