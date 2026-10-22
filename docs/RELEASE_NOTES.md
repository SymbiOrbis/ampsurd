This is a public beta. Please test it and report problems via GitHub Issues.

Windows warning: this beta is NOT code-signed yet (signing through the
SignPath Foundation is in progress). Windows 11 Smart App Control blocks
unsigned software and offers no per-app exception, so the installer only
runs with Smart App Control turned off. On some Windows versions it cannot
be turned back on without reinstalling Windows. If that matters to you,
wait for the signed release.
SmartScreen may also show "Windows protected your PC": More info, Run anyway.

Feedback: please share your experience in [GitHub Discussions](https://github.com/SymbiOrbis/ampsurd/discussions/1)
("AMPSURD beta feedback") or report bugs as [Issues](https://github.com/SymbiOrbis/ampsurd/issues).
If you like AMPSURD, a star on GitHub helps a lot: it's how a small
open-source project qualifies for free code signing, so Windows stops
blocking it.

macOS: AMPSURD-<version>-macOS.pkg (macOS 10.15 Catalina or newer, Apple Silicon and Intel) is NOT signed or
notarized by Apple either. Double-click it; when macOS says it cannot verify it, open System Settings,
Privacy & Security, scroll down and click "Open Anyway" (macOS 14 and older: right-click the .pkg, Open).
The first start of the app asks for access to the audio input: allow it, or AMPSURD hears no guitar.

## AMPSURD 0.9.1 (beta)

Second public beta for Windows 10 / 11 (64-bit): **AMPSURD-Setup-0.9.1.exe** installs the VST3
plugin and the standalone app (with player / recorder). Installing it over 0.9.0 is fine: your presets
and recordings (Documents\AMPSURD) stay untouched when you update or uninstall.

New in 0.9.1:
- Metronome in the standalone player / recorder: tempo (type it or TAP), time signature (2/4 ... 7/8) or
  EVEN (every click the same), volume, count-in of 1 or 2 bars before recording. The click is heard,
  never recorded.
- Move an amp to another slot by dragging it by its name (everything moves with it; Ctrl+Z undoes it).
- PAN can be typed (click the value: L30, R20, C).
- OUTPUT meter with separate left / right bars.
- UNDO / REDO as arrow buttons (no longer overlapping PLAYER / REC); Ctrl+Z / Ctrl+Y.
- Author shown in About.
- EQ COPY / PASTE between any EQs (amp, IR, Global).
- Guitar input: choose the input of your interface the guitar is plugged into (e.g. the guitar jack of a
  Focusrite Scarlett Solo is input 2) - in the settings menu, or in the app's new Audio / MIDI Settings
  window (Options), which lists the inputs one by one. If only the other input has a signal, AMPSURD
  offers "GUITAR ON INPUT 2? USE INPUT 2" (one click). In a DAW: input 1 or 2 of the track.
- ROOT (EXPERIMENTAL): makes the lowest note of a power chord deeper - a clean, compressed low layer
  under the amps (PARALLEL / POST / SUB, GROWL, BASS EQ). Off by default. Feedback welcome.
- "CREATE FRANKENSTEIN" is now just "FRANKENSTEIN".
- NOISE GATE always at hand: its meter, THRESHOLD and DECAY sit above INPUT at the bottom, visible while
  you edit amps and mix. The centre area shows GLOBAL by default: TUNER, Global EQ and the effects.
- Player: PAN for your guitar - the take is recorded where you pan it (stereo layering: record left,
  BOUNCE, record right). GUITAR VOL also sets your guitar on the monitors while recording.
- VIDEO takes (standalone app): film yourself with the computer's camera while you record and save an MP4
  with AMPSURD's sound (GUITAR + BACKING or GUITAR ONLY) - REC / STOP and SAVE MP4 right in the camera
  window. Please report whether picture and sound are together (SOUND OFFSET fixes it).
- Blending amps keeps their tone: a 50:50 blend used to sound slightly darker than either amp alone (the
  highs of different amps partly cancel). AMPSURD now measures this per frequency band - also while you play,
  including your amp EQs - and corrects it with a slowly adapting, automatic EQ on the blend (one amp alone:
  no change).
- DOUBLER (EXPERIMENTAL): double tracking from one guitar - 1 to 3 "second takes" left and right, each with
  its own timing, pitch and tone (TIGHT, LEVEL, WIDTH, LOW MONO, CHARACTER). No extra latency. Off by
  default. Feedback welcome.
- Metronome: sharper HI-HAT click (or BEEP) and VOL up to +12 dB.
- Fix: a burst of invalid audio from a driver could silence AMPSURD until it was restarted.
- macOS: app, VST3 and Audio Unit (macOS 10.15 Catalina or newer, Apple Silicon and Intel).

Highlights: up to five NAM captures blended (any NAM architecture), Create Frankenstein (TONE / NOTES),
per-amp EQ / PAN / cabinet IR with IR EQ, per-amp INPUT (loudness kept automatically) and OUTPUT,
Global EQ, gate + tuner, delays / reverb / flanger / PITCH (re-tuning), input calibration, ECO (A2 Lite,
the default like in TONE3000), MIDI footswitches (program change, MIDI learn), preset packs
(.ampsurdpack), undo / redo, A/B compare, safety limiter (never above -1 dBFS), standalone player /
recorder with punch-in corrections and crossfaded edits, presets.

Portable zips (no installer) are attached as well.
