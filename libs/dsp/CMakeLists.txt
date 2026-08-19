# ─────────────────────────────────────────────────────────────────────────────
# Shared DSP
#
# Split in two on purpose:
#
#   audio::dsp_core   pure maths, zero dependencies — not even JUCE. Buildable
#                     and testable with a bare g++ invocation, which keeps the
#                     test loop at well under a second and means the maths can
#                     be verified before any framework is installed.
#
#   audio::dsp        the above plus modules that legitimately need juce_dsp
#                     (smoothed values, AudioBlock). This is what plugins link.
#
# Keep new code in dsp_core unless it genuinely needs JUCE. Every module that
# stays framework-free is a module that can be tested the cheap way.
# ─────────────────────────────────────────────────────────────────────────────

add_library(audio_dsp_core INTERFACE)
add_library(audio::dsp_core ALIAS audio_dsp_core)

target_include_directories(audio_dsp_core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features(audio_dsp_core INTERFACE cxx_std_20)


add_library(audio_dsp INTERFACE)
add_library(audio::dsp ALIAS audio_dsp)

target_link_libraries(audio_dsp INTERFACE
    audio::dsp_core
    juce::juce_dsp)


if(AUDIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
