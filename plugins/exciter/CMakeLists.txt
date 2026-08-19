add_audio_plugin(
    TARGET       Exciter
    PRODUCT_NAME "Exciter"
    PLUGIN_CODE  Exc1
    DESCRIPTION  "Harmonic exciter"
    SOURCES
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp)
