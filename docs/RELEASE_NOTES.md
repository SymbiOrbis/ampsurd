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

macOS: AMPSURD-macOS.pkg (macOS 10.15 Catalina or newer, Apple Silicon and Intel) is signed and
notarized (Developer ID: Radovan Glovna): double-click it and install. The first start of the app asks for
access to the audio input: allow it, or AMPSURD hears no guitar (the camera is asked for only when you use
VIDEO in the player).

## AMPSURD 0.9.2 (beta)

Third public beta. **Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the
standalone app (with player / recorder). **macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the
VST3 plugin and the Audio Unit. Installing over an older version is fine: your presets and recordings
(Documents/AMPSURD) stay untouched when you update or uninstall.

New in 0.9.2:
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
  BOUNCE, record right). One undo history for everything: Ctrl+Z / Ctrl+Y also take back / redo recordings,
  BOUNCE and deleted tracks. GUITAR VOL also sets your guitar on the monitors while
  recording.
- VIDEO takes (standalone app), for spontaneous ideas: film yourself with the computer's camera while you
  play - every take is saved automatically as an MP4 with AMPSURD's sound (GUITAR + BACKING or GUITAR ONLY)
  in Documents / AMPSURD / Recordings, and a notice says so. Record as many as you like, one after another;
  WATCH + PLAY shows the last take or any saved video right in the camera window. VIDEO is new and still has
  known problems with recording - it is being fixed; please report what happens on your computer.
- Blending amps keeps their tone: a 50:50 blend used to sound slightly darker than either amp alone (the
  highs of different amps partly cancel). AMPSURD now measures this per frequency band - also while you play,
  including your amp EQs - and corrects it with a slowly adapting, automatic EQ on the blend (one amp alone:
  no change).
- DOUBLER (EXPERIMENTAL): double tracking from one guitar - 1 to 3 "second takes" left and right, each with
  its own timing, pitch and tone (TIGHT, LEVEL, WIDTH, LOW MONO, CHARACTER). No extra latency. Off by
  default. Feedback welcome.
- SAVE AS can also save a pack (.ampsurdpack: the preset with its NAM captures and IRs) - choose it under
  "Save as type" (Windows) or "Format" (macOS).
- Fixed download names, so a link to the newest version always works.
- MUTE next to BYPASS: the whole output to silence (red while on). Player: delete a track with the X before
  its name (asks first); CLEAR TAKE removed.
- Metronome: sharper HI-HAT click (or BEEP) and VOL up to +12 dB.
- Fix: a burst of invalid audio from a driver could silence AMPSURD until it was restarted.
- macOS: app, VST3 and Audio Unit (macOS 10.15 Catalina or newer, Apple Silicon and Intel), signed and
  notarized by Apple. First macOS release: please report how it works on your Mac.

Highlights: up to five NAM captures blended (any NAM architecture), Create Frankenstein (TONE / NOTES),
per-amp EQ / PAN / cabinet IR with IR EQ, per-amp INPUT (loudness kept automatically) and OUTPUT,
Global EQ, gate + tuner, delays / reverb / flanger / PITCH (re-tuning), input calibration, ECO (A2 Lite,
the default like in TONE3000), MIDI footswitches (program change, MIDI learn), preset packs
(.ampsurdpack), undo / redo, A/B compare, safety limiter (never above -1 dBFS), standalone player /
recorder with punch-in corrections and crossfaded edits, presets.

Downloads (the same names in every release): AMPSURD-Windows-Setup.exe, AMPSURD-Windows.zip (VST3 + app, no
installer), AMPSURD-macOS.pkg, AMPSURD-macOS.zip (app, VST3, Audio Unit).

Earlier: 0.9.1 (2026-10-17) - metronome, moving amps by drag and drop, typed PAN, left / right OUTPUT meter,
UNDO / REDO arrows.
