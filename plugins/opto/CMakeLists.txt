add_audio_plugin(
    TARGET       Opto
    PRODUCT_NAME "Opto"
    PLUGIN_CODE  Opt1
    DESCRIPTION  "Optical compressor"
    SOURCES
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp)
