# Development tools. Not shipped, not part of any plugin.
#
# render_editor.cpp is compiled once per plugin: the plugin's source directory
# goes on the include path so "PluginProcessor.h" resolves to the right one, and
# the processor class name comes in as a define. Adding a renderer for a new
# plugin is one line.

function(add_editor_renderer)
    set(oneValueArgs NAME PLUGIN_TARGET PROCESSOR_CLASS SOURCE_DIR)
    cmake_parse_arguments(R "" "${oneValueArgs}" "" ${ARGN})

    add_executable(${R_NAME} render_editor.cpp)

    target_include_directories(${R_NAME} PRIVATE "${R_SOURCE_DIR}")

    target_compile_definitions(${R_NAME} PRIVATE
        PLUGIN_PROCESSOR_CLASS=${R_PROCESSOR_CLASS})

    # audio::dsp and audio::ui are linked PRIVATE by the plugin, so their
    # include paths do not propagate here — link them explicitly.
    target_link_libraries(${R_NAME} PRIVATE
        ${R_PLUGIN_TARGET}
        audio::dsp
        audio::ui
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endfunction()

add_editor_renderer(
    NAME            render_editor
    PLUGIN_TARGET   MixingPlugin
    PROCESSOR_CLASS MixingPluginProcessor
    SOURCE_DIR      "${CMAKE_SOURCE_DIR}/plugins/mixing-plugin/source")

add_editor_renderer(
    NAME            render_multiband
    PLUGIN_TARGET   Multiband
    PROCESSOR_CLASS MultibandProcessor
    SOURCE_DIR      "${CMAKE_SOURCE_DIR}/plugins/multiband/source")
