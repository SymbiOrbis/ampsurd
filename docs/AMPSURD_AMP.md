# AMPSURD amp (.ampsurdamp)

One amp with its own knobs (owner, 2026-10-30): GAIN moves smoothly between captures of the amp at different gain
settings, BASS / MID / TREBLE are the amp's measured tone controls. Not a NAM file: a package of its own that
AMPSURD loads into a slot like a capture (LOAD NAM, drag and drop, presets, tones, packs).

## What is inside

A zip file:
- `amp.xml` - name, author, the GAIN position of every capture, the tone-control curves (1/6-octave grid,
  20 Hz - 20 kHz, dB relative to all knobs at noon, for each measured position of each knob)
- the gain captures as **standard .nam files** (each also works alone in any NAM player)

## How AMPSURD plays it

- GAIN (0-10, the amp's scale: 7 o'clock = 0, noon = 5, 5 o'clock = 10): the two captures around the knob position
  are played and blended (core: `CaptureModel::makeMorph`); on a captured position only that capture runs. When it
  loads, AMPSURD measures every capture once: loudness, polarity and timing relative to the one nearest noon, so
  GAIN changes the character, not the level (like AMPSURD's per-amp INPUT). Since 0.9.7 the timing is sub-sample
  (whole samples + a first-order allpass) and the blend of two steps is made louder by exactly what partly different
  distortion cancels in the sum (measured per pair at 50/50, `pairLoss`; gain 1/sqrt(1 - 2w(1-w)·pairLoss)) - the
  Framus Cobra amps (2026-11-01) stay within 0.1 dB across GAIN (before: dips up to 1.1 dB). Code:
  `CaptureAnalyzer::calibrateMorph`. Between two steps the amp costs twice the CPU of one capture.
- BASS / MID / TREBLE: the measured curves are interpolated between the measured positions, added (the knobs are
  treated as independent) and applied as a short minimum-phase filter after the amp and before the cabinet IR
  (core: `ToneStack`). All at noon = no filter at all (bit-identical). Only measured knobs are shown on the slot.
- The knobs are parameters of the slot (`sN_ampGain`, `sN_ampBass`, `sN_ampMid`, `sN_ampTreble`): automatable,
  saved with presets and tones, one undo step per move.

## Making one (settings menu -> "Build an AMPSURD amp from recordings...")

Put into one folder, named as in the recording sheet (AMPSURD_amp_profile_recording_sheet.xlsx):
- the trained gain captures: `...gain_HHMM....nam` - HHMM = the GAIN knob's clock position (0900, 1000, 1200,
  1300 or 0100 = 1 o'clock ...), tone knobs at noon while recording
- the tone sweeps (optional, 48 kHz WAV): `T00_ref_all_noon.wav`, `T13_ref_all_noon.wav` (references, all at noon),
  `Tnn_bass_min.wav`, `Tnn_bass_0900.wav`, `Tnn_bass_1500.wav`, `Tnn_bass_max.wav`, the same for `mid` and `treble` -
  each the whole `AMPSURD_tone_sweep_48k.wav` recorded through the amp with the gain at minimum

Choose the folder, type the name (and author): the file appears in Documents/AMPSURD/Amps. The report says what
was used, which sweeps had to use the quiet sweep (the loud one distorted) and how much the two references differ
(more than 1 dB = something changed in the chain during the recordings).

The sweep analysis (core: `ToneSweep::analyse`): the recording is divided by the known sweep (latency up to
0.5 s does not matter), averaged in 1/6-octave bands; the loud sweep is used when it agrees with the quiet one
(shape and level within 1 dB), otherwise the quiet one. Each knob position is taken relative to the average of
the references, so the load box and interface cancel out.

## Limits

- Blending two gain captures approximates the in-between gain; close steps (every hour on the knob) sound right.
- The tone stack is applied after the amp: exact for amps whose tone stack follows the preamp distortion (e.g.
  5150 III), an approximation where the tone stack drives a later gain stage; knob interactions are not modelled.
- Presence / resonance are not part of version 1 (stay where they were recorded).

## Tests

`tools/amp_profile_test` (CI): sweep file identical, a known tone curve measured back within 0.3 dB, distortion
detected, interpolation, minimum-phase filter, GAIN morph (on a step = that capture, halfway = average, no jumps
while turning). `tests/ui_snapshot` (local): builds an amp from synthetic recordings, loads it, knobs on the slot,
BASS / GAIN change the sound as measured, presets keep the knobs.


## The capture check (since 2026-11-05)

Before anything is written, the builder loads every gain capture, plays AMPSURD's test signal through it and
compares each one with its neighbours. **The build stops - no file is written - when any of them is faulty**, and the
message names the file and the reason:

- it does not load, plays invalid numbers or (almost) silence;
- it is more than 1 ms early or late against its neighbours (a timing fault, typically from training), or sounds unlike
  both neighbours (similarity below 0.6; real neighbours are mostly above 0.85) - with 3 or more captures;
- it is the same capture as another one (same weights hash, or the same file content);
- it has less gain than the capture before it (NAM's own gain estimate falls by more than 0.05: files named wrong?);
- a tone sweep clips (reaches 0 dBFS): its tone curve would be wrong.

Two captures for the same GAIN position stop the build too. Found on the owner's EVH 5150 III recordings: the boosted
CH2 13:00 capture was 3.5 ms early (TONE3000 training) - the only one in nine amps (EVH x 6, Framus Cobra x 3).
Code: plugin/AmpProfileFile.cpp (checkCaptures); test: ui_snapshot "AMP BUILDER CAPTURE CHECK" (NAM Linear captures
with known faults).


## Starting from silence (since 2026-11-06)

Recorded down to the bottom of the gain knob (the lowest capture at 8:00 or below, e.g. 7:45 - below it the amp is
practically silent), the builder marks the amp `silenceFrom="0"`: GAIN then reaches down to 7:00, which is silence,
and between 7:00 and the lowest capture it works like a volume knob (amplitude t², t = 0..1: half way = -12 dB). From
the lowest capture up, nothing changes (the captures are level-matched as before). Sets that start higher (e.g. 9:00)
keep their range. The model keeps playing the lowest capture there (CaptureModel::morphFadeGain / setMorphSilenceFloor;
level match and the INPUT curve are measured at full level); the fade is the slot's output gain
(AmpsurdProcessor::ampFadeGain). ui_snapshot "AMPSURD AMP FROM SILENCE".
