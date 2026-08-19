add_audio_plugin(
    TARGET       Multiband
    PRODUCT_NAME "Multiband"
    PLUGIN_CODE  Mbc1
    DESCRIPTION  "Multiband compressor"
    SOURCES
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp)
