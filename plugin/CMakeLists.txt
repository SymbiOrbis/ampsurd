juce_add_plugin(Monstrosity
  PRODUCT_NAME              "MONSTROSITY"
  COMPANY_NAME              "MONSTROSITY"
  BUNDLE_ID                 "com.monstrosity.monstrosity"
  PLUGIN_MANUFACTURER_CODE  Mnst
  PLUGIN_CODE               Mns1
  FORMATS                   VST3
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  # Do not copy into C:\Program Files\Common Files\VST3 (needs admin rights).
  # REAPER is pointed at the build output folder instead - see docs/BUILD_WINDOWS.md.
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Distortion)

target_sources(Monstrosity PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp)

target_compile_definitions(Monstrosity PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Monstrosity
  PRIVATE
    monstrosity_core
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
