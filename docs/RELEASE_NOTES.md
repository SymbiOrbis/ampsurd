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

## Next version (not released yet)

- PLAYER: a second recording track, INPUT 2 - a mic, a keyboard or another instrument on another input of your audio
  interface (click INPUT 2 to choose the input). Each track has an ARM switch: GUITAR is armed from the start, INPUT 2
  when you arm it. Record both together, or the guitar first and INPUT 2 later (GUITAR's ARM off). Own VOL and PAN.
- VIDEO takes start where the playhead is (click in the timeline), not only at the song's beginning.
- PLAYER keys: R or Ctrl+R (Cmd+R on Mac) starts recording (also a video take), Space stops it.
- CLOSE in the middle section (EDIT, IR, IR map, GLOBAL, PLAYER, ROOT, DOUBLER, LOG) is red, like an active MUTE.
- LOG: each dropout now says where the time went - for each amp its pedals, capture (GAIN captures), tone + cabinet
  IR and EQ, plus mixing, input / PITCH, effects and output (the biggest parts first).

## AMPSURD 0.9.16 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.16:
- Copy tone / Paste tone in an amp slot's right-click menu: the whole tone of that amp (capture or AMPSURD amp,
  pedals, IR, EQs, INPUT / OUTPUT, ECO) into another slot - without saving a file. Works between AMPSURD windows.
  The target keeps its mix share and PAN; one Undo step.
- Save tone: SAVE AS... - choose the folder and name yourself (SAVE still saves straight into Documents/AMPSURD/Tones).
- LOG: a dropout where AMPSURD's audio thread was busy the whole time is now called a sudden spike (its own work or
  a driver interrupting it) instead of "the computer paused AMPSURD".

## AMPSURD 0.9.15 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.15:
- The AMPSURD IR map has its own names: speakers BRIT 30, HEAVY 77 and BAYOU, microphones DYN, DYN WIDE and RIBBON
  (no brand names). Presets made with the old names load exactly as before; exported IR files get the new names.

## AMPSURD 0.9.14 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.14:
- The new AMPSURD logo: larger in the top bar, and the new app icon (standalone app, installer).

## AMPSURD 0.9.13 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.13:
- An AMPSURD amp with GAIN between two recorded positions plays its two captures on two CPU cores when one is
  free (the sound is exactly the same). With one or two amps this gives the computer more room at small buffers;
  with five busy amps nothing changes.

## AMPSURD 0.9.12 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.12:
- The LOG tells more about each dropout: how long AMPSURD really worked on the block, and how long it waited for a
  helper core. "the computer paused AMPSURD" = Windows / macOS gave the time to something else (another program,
  power saving, a driver); otherwise AMPSURD itself was too slow and a larger buffer or ECO helps.

## AMPSURD 0.9.11 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.11:
- PEDAL 1 / PEDAL 2 buttons on the slot: turn the pedals before the amp on / off without opening EDIT (lit = on).
- The global EQ: "EQ OFF" and the hint text no longer run under the EQ PRESETS button.

## AMPSURD 0.9.10 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.10:
- Full amp names: a long name (e.g. "Framus Cobra + Gerhat Screamer CH3") is shown completely on the slot - the
  text gets smaller instead of ending in "...". The tooltip no longer covers the expanded name.
- AMPSURD amps recorded from the lowest gain (lowest capture at 8:00 or below): GAIN goes down to 7:00 and fades
  to silence below the lowest capture, like the amp's own knob.
- AMPSURD amp builder checks every capture first (loads and plays, timing, similarity to its neighbours,
  duplicates, gain order, clipped tone sweeps). If anything is wrong, the amp is NOT built and the report says
  which capture and why.
- LOG window: a short log shows from the top, CLEAR also resets the dropout summary.

## AMPSURD 0.9.9 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.9:
- LOG: click DROPOUTS (or CPU) in the footer, or settings menu -> Show log. Every dropout is listed with what was
  going on at that moment (loading an amp, measuring, calculating an IR, GAIN between two recorded positions ...),
  whether AMPSURD or the audio driver was late, and the likely cause. Problems are listed too (files not found,
  captures / IRs that could not load, audio settings changes, update check errors). COPY, SHOW FILE, CLEAR; the file
  is Documents / AMPSURD / Logs / AMPSURD_log.txt - easy to send.
- AMPSURD amps: GAIN shows the amp knob's clock position (9:00 ... 15:00).
- AMPSURD IR map: greyscale boxes (light = bright, dark = dark) with clearer steps; drag a row sideways right on its
  boxes to slide it (a click still chooses the point).

## AMPSURD 0.9.8 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.8:
- AMPSURD IR map: every box shows its mic position (mm). Rows can be slid left / right (drag a row's name sideways
  or use its < > arrows) and moved up / down (drag the name) - e.g. a bright V30 + SM57 next to a darker
  V30 + R121 at another position. A row runs 25 -> 100 -> 25 mm, so it can be slid endlessly without a jump. The
  centre of a box = that IR alone, anywhere else = the boxes around it blended. RESET ROWS = the normal map. Presets
  keep the map; older presets sound exactly as before.
- AMPSURD amps: the GAIN knob turns only over the recorded positions (e.g. 9:00 to 3:00), no dead travel.
- AMPSURD amps: hovering the amp's name shows the whole name (the knobs hid part of it).

## AMPSURD 0.9.7 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.7:
- AMPSURD amps (.ampsurdamp): turning GAIN keeps the volume steady between the recorded positions too. Before, the
  blend of two differently distorted captures could be up to about 1 dB quieter halfway between them; the captures
  are now lined up more precisely (to a fraction of a sample) and the blend is made louder by exactly what it loses.
- Build an AMPSURD amp from recordings: sweep file names may start with the channel number (CH1_T00_ref_all_noon).

## AMPSURD 0.9.6 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.6:
- AMPSURD amps (.ampsurdamp): the amp's INPUT in EDIT now keeps the amp's loudness at any GAIN setting. Before, this
  was measured only at the GAIN the amp had when it loaded, so after a big GAIN move turning INPUT could change the
  volume slightly. Now it is measured again in the background shortly after GAIN stops moving.

## AMPSURD 0.9.5 (beta)

**Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the standalone app.
**macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio Unit. Installing over an
older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.5:
- Fix (standalone app, above all on macOS): file dialogs (Save as, Import / Export pack, LOAD NAM ...) could not be
  typed into or browsed - the name could not be changed and only Documents could be used. AMPSURD kept taking the
  keyboard back for its player shortcuts while a dialog was open.

## AMPSURD 0.9.4 (beta)

Fifth public beta. **Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the
standalone app. **macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio
Unit. Installing over an older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.4:
- AMPSURD amps (.ampsurdamp): an amp with real knobs - GAIN moves smoothly between captures of the amp at different
  gain settings, BASS / MID / TREBLE are the amp's measured tone controls. Load it like a NAM. Make your own from
  recordings (settings menu -> "Build an AMPSURD amp from recordings...", docs/AMPSURD_AMP.md).
- TONES (.ampsurdtone): save one amp's whole sound - capture, pedals, cabinet IR (also AMPSURD IRs), its EQ and the
  IR's EQ, INPUT / OUTPUT - under a name, and load it into any amp with one click (EDIT -> TONES, right-click a
  slot, LOAD NAM, or drop the file on a slot). The rest of the rig stays as it is.
- Pedals before an amp are visible on the slot: one frame behind it per pedal, like a stack of paper (a bypassed
  pedal's frame is dimmed).

## AMPSURD 0.9.3 (beta)

Fourth public beta. **Windows 10 / 11 (64-bit):** AMPSURD-Windows-Setup.exe installs the VST3 plugin and the
standalone app. **macOS 10.15 or newer:** AMPSURD-macOS.pkg installs the app, the VST3 plugin and the Audio
Unit. Installing over an older version is fine: presets and recordings (Documents/AMPSURD) stay untouched.

New in 0.9.3:

- AMPSURD IRs: AMPSURD's own 4x12 cabinet IRs - 3 speakers (V30, DV77, SWAMP) x 3 microphones (SM57, R121,
  MD421) x mic positions from 25 to 100 mm - on one map (IR -> AMPSURD IRs). Click, drag or just hover to
  hear them; between the dots AMPSURD calculates the sound in between, like moving the mic. Save any point
  as a WAV, or export all 144. Synthetic (calculated) IRs, no recordings; presets remember the point.
- Pedals before an amp: up to two captures (boost, overdrive, preamp...) in front of each amp (EDIT ->
  BEFORE THE AMP), each with ON and LEVEL. The amp's alignment and level match include them.
- EQ PRESETS on every EQ: save a curve under any name and load it into any EQ (factory curves to follow).
- EQ COPY is easy to get rid of: CANCEL COPY, Ctrl+Z, Esc, or it ends by itself after 20 s; a copy ends with
  its paste.
- New A2 captures load in their full version by default (ECO = Lite is one click away).
- Metronome: even at its loudest it no longer pushes the guitar down - the click is mixed after the safety
  limiter, into the headroom the guitar leaves (before, every loud click turned the whole mix down).
- Fixes: FRANKENSTEIN shows itself with one press (it could switch on and off unseen behind another view);
  on macOS the pack import / export dialog could not be clicked; a recorded VIDEO take could not be played
  in the camera window on Windows ("Can't create window").

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

Earlier: 0.9.4 (2026-10-30) - AMPSURD amps (.ampsurdamp), tones (.ampsurdtone), pedals shown as stacked frames.
0.9.3 (2026-10-29) - AMPSURD IRs, pedals before an amp, EQ presets, easier EQ COPY, Full A2 by default.
0.9.2 (2026-10-24) - first macOS release, VIDEO takes, blend tone, DOUBLER, SAVE AS pack, MUTE.
0.9.1 (2026-10-17) - metronome, moving amps by drag and drop, typed PAN, left / right OUTPUT meter,
UNDO / REDO arrows.
