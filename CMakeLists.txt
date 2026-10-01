cmake_minimum_required(VERSION 3.25)

project(MONSTROSITY VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Use the static MSVC runtime so the .vst3 does not depend on a separately
# installed Visual C++ redistributable on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MONSTROSITY_BUILD_PLUGIN "Build the JUCE VST3 plugin" ON)
option(MONSTROSITY_BUILD_TOOLS  "Build command-line test tools" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)

if(MONSTROSITY_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(MONSTROSITY_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()

# Developer-only: end-to-end test that hosts the built VST3 (not needed for normal builds).
option(MONSTROSITY_BUILD_HOST_TEST "Build the VST3 hosting end-to-end test" OFF)
if(MONSTROSITY_BUILD_HOST_TEST AND MONSTROSITY_BUILD_PLUGIN)
  juce_add_console_app(plugin_host_test PRODUCT_NAME "plugin_host_test")
  target_sources(plugin_host_test PRIVATE tests/plugin_host_test.cpp)
  target_compile_definitions(plugin_host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(plugin_host_test PRIVATE juce::juce_audio_processors juce::juce_audio_formats
                        juce::juce_gui_basics juce::juce_recommended_config_flags)
  add_dependencies(plugin_host_test Monstrosity_VST3)
endif()
