cmake_minimum_required(VERSION 3.22)

# ─────────────────────────────────────────────────────────────────────────────
# Shared identity
#
# Applies to every plugin in the repo. Per-plugin identity — product name,
# plugin code, bundle id — lives in plugins/<name>/CMakeLists.txt.
#
# AUDIO_MANUFACTURER_CODE is shared on purpose: it identifies *you*, and hosts
# group plugins by it. The per-plugin PLUGIN_CODE is what must be unique, and
# add_audio_plugin() enforces that.
# ─────────────────────────────────────────────────────────────────────────────
set(AUDIO_COMPANY_NAME      "Ivo Di Barros")
set(AUDIO_COMPANY_DOMAIN    "com.ivodibarros")
set(AUDIO_MANUFACTURER_CODE "Ivdb")
set(AUDIO_DEFAULT_FORMATS   VST3 Standalone)

project(AudioPlugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AUDIO_BUILD_TESTS "Build the DSP unit tests" ON)

if(AUDIO_BUILD_TESTS)
    enable_testing()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# JUCE
#
# Fetched once for the whole repo, shared by every plugin target. Adding a
# second plugin costs no extra download and no extra JUCE compile.
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AudioPlugin)

# Shared DSP first — plugins depend on it.
add_subdirectory(libs/dsp)
add_subdirectory(libs/ui)

# ─────────────────────────────────────────────────────────────────────────────
# Plugins
#
# Auto-discovered, so scripts/new-plugin.sh does not have to edit this file.
# CONFIGURE_DEPENDS makes the build re-check for new directories rather than
# silently ignoring them until the next manual configure.
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB AUDIO_PLUGIN_DIRS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/CMakeLists.txt")

if(NOT AUDIO_PLUGIN_DIRS)
    message(WARNING "No plugins found under plugins/. Run scripts/new-plugin.sh to add one.")
endif()

foreach(plugin_cmake ${AUDIO_PLUGIN_DIRS})
    get_filename_component(plugin_dir "${plugin_cmake}" DIRECTORY)
    add_subdirectory("${plugin_dir}")
endforeach()

get_property(_codes GLOBAL PROPERTY AUDIO_PLUGIN_SUMMARY)
message(STATUS "Plugins in this build: ${_codes}")

# Development tools — offscreen editor rendering, so the UI can be reviewed
# from a build artefact instead of by launching the standalone.
option(AUDIO_BUILD_TOOLS "Build development tools" ON)

if(AUDIO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
