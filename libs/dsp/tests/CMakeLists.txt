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
