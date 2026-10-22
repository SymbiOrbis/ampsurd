# Windows: VST3 + standalone app. macOS: also Audio Unit (Logic Pro, GarageBand, MainStage).
if(APPLE)
  set(AMPSURD_FORMATS VST3 AU Standalone)
else()
  set(AMPSURD_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Ampsurd
  PRODUCT_NAME              "AMPSURD"
  COMPANY_NAME              "AMPSURD"
  BUNDLE_ID                 "com.ampsurd.ampsurd"
  PLUGIN_MANUFACTURER_CODE  Amps
  PLUGIN_CODE               Amp5
  FORMATS                   ${AMPSURD_FORMATS}
  # macOS asks the user once before the app may use the audio interface input
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "AMPSURD needs the audio input to hear your guitar through your audio interface."
  # VIDEO takes in the standalone player (macOS asks once)
  CAMERA_PERMISSION_ENABLED TRUE
  CAMERA_PERMISSION_TEXT "AMPSURD films you with the camera only while you record a VIDEO take in the player."
  AU_MAIN_TYPE              kAudioUnitType_MusicEffect
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          TRUE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  # Do not copy into C:\Program Files\Common Files\VST3 (needs admin rights).
  # REAPER is pointed at the build output folder instead - see docs/BUILD_WINDOWS.md.
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Distortion
  # app icon: AMP over SURD, blood red on the AMPSURD background (assets/icon, made by script)
  ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/icon_1024.png"
  ICON_SMALL                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/icon_256.png")

target_sources(Ampsurd PRIVATE
  PluginProcessor.cpp
  RigControl.cpp
  PluginEditor.cpp
  ui/Theme.cpp
  ui/Components.cpp
  ui/RootPanel.cpp
  ui/CameraWindow.cpp
  standalone/StandaloneApp.cpp
  player/PlayerRecorder.cpp
  player/VideoCapture.cpp)
# VIDEO takes: MP4 export with the operating system's own media framework
if(WIN32)
  target_sources(Ampsurd PRIVATE player/VideoMux_win.cpp)
elseif(APPLE)
  target_sources(Ampsurd PRIVATE player/VideoMux_mac.mm)
else()
  target_sources(Ampsurd PRIVATE player/VideoMux_none.cpp)
endif()

# Embedded assets: fonts (Inter, SIL OFL), the footer logos (assets/logos/logo_1..3.svg|png) and the
# background dragon (assets/dragon, made by tools/dragon/make_dragon.py).
file(GLOB AMPSURD_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/logos/*.svg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/logos/*.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/dragon/*.png")
juce_add_binary_data(AmpsurdAssets SOURCES ${AMPSURD_ASSETS})

target_compile_definitions(Ampsurd PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
  JUCE_USE_MP3AUDIOFORMAT=1)
if(WIN32 OR APPLE)
  target_compile_definitions(Ampsurd PUBLIC JUCE_USE_CAMERA=1)
endif()
if(AMPSURD_ROOT_MODULE)
  target_compile_definitions(Ampsurd PUBLIC AMPSURD_ROOT=1)
else()
  target_compile_definitions(Ampsurd PUBLIC AMPSURD_ROOT=0)
endif()
if(WIN32)
  # ASIO drivers in the standalone app (lowest latency). JUCE bundles the Steinberg ASIO SDK headers,
  # which are dual-licensed (Steinberg ASIO License or GPLv3); AMPSURD uses them under GPLv3,
  # compatible with AMPSURD's AGPLv3. See THIRD_PARTY.md.
  target_compile_definitions(Ampsurd PUBLIC JUCE_ASIO=1)
endif()
if(UNIX AND NOT APPLE)
  # Linux builds are for tests only: no audio driver headers needed
  target_compile_definitions(Ampsurd PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(Ampsurd
  PRIVATE
    ampsurd_core
    AmpsurdAssets
    juce::juce_audio_utils
    juce::juce_video
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_include_directories(Ampsurd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
