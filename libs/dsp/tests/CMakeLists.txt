# Tests for audio::dsp_core.
#
# These link nothing but the header-only core, so they build in about a second
# and need no JUCE. They also run without CMake at all — see the README.

function(add_dsp_test name)
    add_executable(${name} ${name}.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE audio::dsp_core Threads::Threads)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_dsp_test(biquad_test)
add_dsp_test(envelope_follower_test)
add_dsp_test(gain_computer_test)
add_dsp_test(filter_response_test)
add_dsp_test(spsc_ring_buffer_test)
add_dsp_test(dynamic_eq_band_test)
add_dsp_test(linkwitz_riley_test)
add_dsp_test(multiband_compressor_test)
add_dsp_test(optical_compressor_test)
add_dsp_test(waveshaper_test)

# ─────────────────────────────────────────────────────────────────────────────
# Tests that need JUCE
#
# A few modules legitimately depend on juce::dsp — the oversampler is not worth
# hand-rolling. Those tests build as console apps and link audio::dsp instead of
# audio::dsp_core. They are slower to compile and no slower to run.
#
# Keep this list short. Every test that can stay on the dsp_core path should.
# ─────────────────────────────────────────────────────────────────────────────
function(add_dsp_juce_test name)
    juce_add_console_app(${name} PRODUCT_NAME ${name})

    target_sources(${name} PRIVATE ${name}.cpp)

    target_compile_definitions(${name} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(${name} PRIVATE
        audio::dsp
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    # $<TARGET_FILE:> rather than the bare target name: juce_add_console_app
    # puts the executable inside an artefacts directory, and CTest does not find
    # it from the target name alone.
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
endfunction()

add_dsp_juce_test(harmonic_exciter_test)
