# Everything shared — company name, manufacturer code, formats, module trims,
# warning flags, the link to audio::dsp — comes from add_audio_plugin().
# See cmake/AudioPlugin.cmake.

add_audio_plugin(
    TARGET       MixingPlugin
    PRODUCT_NAME "Mixing Plugin"
    PLUGIN_CODE  Mxp1
    DESCRIPTION  "Single-band parametric EQ with gain staging"
    SOURCES
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp)
