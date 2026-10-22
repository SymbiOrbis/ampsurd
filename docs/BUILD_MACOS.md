# AMPSURD on macOS

Same code, same features and the same window as on Windows: standalone app (player / recorder,
metronome), VST3 plugin and Audio Unit (AU, for Logic Pro / GarageBand / MainStage).
macOS 10.15 Catalina or newer (Apple Silicon Macs: macOS 11 or newer, as Apple requires); one universal
build for Apple Silicon (M1 ... M4) and Intel Macs. Catalina is built for but not tested by the CI (GitHub
has no Catalina machines): first Catalina testers, please report how it goes.

## Download (testers)

- Public betas: [Releases](https://github.com/SymbiOrbis/ampsurd/releases) - `AMPSURD-<version>-macOS.pkg`
  (from the first beta after 0.9.1).
- Latest build of `main`: GitHub -> Actions -> "Build Windows + macOS" -> the newest green run ->
  Artifacts -> **AMPSURD-Setup-macOS** (GitHub login needed). Unzip it to get the .pkg.

## Install

1. Double-click `AMPSURD-<version>-macOS.pkg`. AMPSURD is **not signed or notarized by Apple** yet, so
   macOS says it cannot verify it - click Done.
2. System Settings -> Privacy & Security -> scroll down -> "AMPSURD-...pkg was blocked" -> **Open Anyway**,
   then your password. (macOS 14 Sonoma and older: right-click the .pkg -> Open -> Open.)
3. The installer lets you pick the parts (Customize): app -> /Applications, VST3 ->
   /Library/Audio/Plug-Ins/VST3, AU -> /Library/Audio/Plug-Ins/Components.
4. First start of the app: allow the **microphone** (= audio input) access, otherwise AMPSURD hears no
   guitar. Changed your mind later: System Settings -> Privacy & Security -> Microphone -> AMPSURD.
5. In the app: Options / settings -> choose your audio interface for input and output. With the
   built-in Mac microphone and speakers the input starts muted (JUCE's feedback protection) - use
   an interface and headphones / monitors.

Files: presets and recordings in ~/Documents/AMPSURD, settings in ~/Library/Application Support.
Uninstall: delete the three items listed in step 3.

Shortcuts on the Mac: Cmd+Z undo, Cmd+Shift+Z or Cmd+Y redo.
Multi-core is OFF by default on the Mac (settings menu, can be switched on): Apple Silicon may put
extra helper threads on its slower efficiency cores; one performance core runs five captures easily.

## What the CI checks on every push (macos-latest, Apple Silicon)

- Universal build (arm64 + x86_64) of the app, VST3 and AU, verified with `lipo`.
- The same tests as on Windows (limiter, gate / tuner, Frankenstein, IR, effects, pitch, recorder,
  NAM bit-exactness `tone_test`, engine, CPU stress, render) on Apple Silicon, and `tone_test` once
  more as Intel code under Rosetta 2.
- Apple's Audio Unit validator: `auval -strict -v aumf Amp5 Amps` must say AU VALIDATION SUCCEEDED.
- The .pkg installer is built (the bundles get an ad-hoc signature only).

Not checked by a machine: real audio interfaces, DAWs, the look on a Retina screen. That is the
checklist below.

## Checklist for a tester with a Mac

1. Install as above; the app starts and shows the same AMPSURD window as on Windows.
2. Audio interface in, headphones out, load a .nam: you hear it; latency feels like on Windows.
3. Five captures, Frankenstein, IRs, effects, gate / tuner: CPU meter readable, no crackles at 128 samples.
   Optional: settings -> multi-core on, compare.
4. PLAYER / REC with metronome: record, play back, UNDO the recording.
5. Logic Pro or GarageBand: AMPSURD appears under Audio Units -> AMPSURD (Logic may scan it on start).
   Insert it on a guitar track, save the project, reopen: the rig comes back.
6. REAPER / Ableton Live (VST3 or AU): the same.
7. Cmd+Z / Cmd+Y, drag an amp to another slot, EQ COPY / PASTE, preset packs export / import.
8. Guitar in the interface's input 2 (e.g. Scarlett Solo INST jack): "GUITAR ON INPUT 2?" appears, one
   click and the guitar is heard (or settings menu -> Guitar input -> the input's name, or Options ->
   Audio / MIDI Settings... -> Guitar input).
9. ROOT (experimental) as in docs/BUILD_WINDOWS.md item 39.
10. GLOBAL default view, gate strip at the bottom, GUITAR PAN / VOL in the player: items 11, 40, 41 there.
11. VIDEO takes as in item 42 there (macOS asks for the camera the first time: allow it).

## Building on a Mac yourself

Xcode (or the Command Line Tools) and CMake 3.25+:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    cmake --build build --target Ampsurd_Standalone Ampsurd_VST3 Ampsurd_AU --parallel
    installer/macos/build_pkg.sh 0.9.1 build/plugin/Ampsurd_artefacts/Release installer/output

## Signing (later)

A signed and notarized Mac release needs an Apple Developer Program membership (99 USD per year,
paid by a person or company; no free open-source route like SignPath exists for Apple). With it the
CI would sign with a Developer ID certificate, notarize with `notarytool` and staple the .pkg -
then macOS installs it without the "Open Anyway" step. Not done; see docs/CODE_SIGNING.md.
