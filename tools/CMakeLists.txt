# Development tools. Not shipped, not part of any plugin.

add_executable(render_editor render_editor.cpp)

target_include_directories(render_editor PRIVATE
    "${CMAKE_SOURCE_DIR}/plugins/mixing-plugin/source")

# MixingPlugin is JUCE's shared-code target — it carries the processor, the
# editor and the JucePlugin_* defines.
# audio::dsp is linked PRIVATE by the plugin, so its include paths do not
# propagate here — link it explicitly.
target_link_libraries(render_editor PRIVATE
    MixingPlugin
    audio::dsp
    audio::ui
    juce::juce_gui_basics
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
