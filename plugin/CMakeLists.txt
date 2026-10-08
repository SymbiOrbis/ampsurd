juce_add_plugin(Ampsurd
  PRODUCT_NAME              "AMPSURD"
  COMPANY_NAME              "AMPSURD"
  BUNDLE_ID                 "com.ampsurd.ampsurd"
  PLUGIN_MANUFACTURER_CODE  Amps
  PLUGIN_CODE               Amp5
  FORMATS                   VST3 Standalone
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  # Do not copy into C:\Program Files\Common Files\VST3 (needs admin rights).
  # REAPER is pointed at the build output folder instead - see docs/BUILD_WINDOWS.md.
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Distortion)

target_sources(Ampsurd PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp
  ui/Theme.cpp
  ui/Components.cpp
  standalone/StandaloneApp.cpp
  player/PlayerRecorder.cpp)

# Embedded assets: fonts (Inter, SIL OFL) and the footer logos (assets/logos/logo_1..3.svg|png).
file(GLOB AMPSURD_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/logos/*.svg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/logos/*.png")
juce_add_binary_data(AmpsurdAssets SOURCES ${AMPSURD_ASSETS})

target_compile_definitions(Ampsurd PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
  JUCE_USE_MP3AUDIOFORMAT=1)
if(UNIX AND NOT APPLE)
  # Linux builds are for tests only: no audio driver headers needed
  target_compile_definitions(Ampsurd PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(Ampsurd
  PRIVATE
    ampsurd_core
    AmpsurdAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_include_directories(Ampsurd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
