# AMPSURD — Roadmap (agreed with the owner, 2026-10-07)

Order (owner, 2026-10-07/08): 1 gate + tuner → 2 Frankenstein → 2b Global EQ → 3 per-amp pan → 3b cabinet IRs → 3c effects → 3d standalone app + player/recorder → 4 release package.
Each step is tested in REAPER before the next starts.

## 1. Noise gate + chromatic tuner (centre area) — IMPLEMENTED 2026-10-07, awaiting REAPER test
- Shown in the large centre area while no amp is in EDIT; EDIT replaces both; closing EDIT brings
  them back. A small GATE indicator stays visible while editing.
- Gate, NS-2 style: detects on the clean DI input, gates AFTER the five amps are blended (removes
  high-gain capture hiss too). Controls: THRESHOLD, DECAY, on/off. On by default, threshold
  -70 dB RMS, decay 120 ms (only acts between notes). Threshold can be dragged on the level meter.
- Tuner: reads the clean DI input, keeps working when AMPSURD's BYPASS is on, optional
  "mute while tuning". On by default. (Not available when the DAW itself bypasses the plugin.)

## 2. "Create Frankenstein" — frequency-split blending — IMPLEMENTED 2026-10-07, awaiting REAPER test
Button in the lower corner. The centre area becomes a frequency map (low → high, same scale as the
EQ/tuner). The user chooses the number of sections (2–5) and assigns a loaded NAM to each section.
- Vertical dividers can be dragged to set where one amp hands over to the next.
- WIDTH 0–90 %: how gradual the hand-over is (abrupt ↔ smooth overlap).
- Muting an amp removes its section; the neighbouring sections meet in the middle of the gap
  (log-frequency). Solo = that amp over the whole spectrum.
- DSP: each amp runs on the full signal (amps are non-linear, so the split happens AFTER the amps),
  then complementary crossover filters (weights sum to 1 at every frequency) are applied to the
  aligned, level-matched outputs. Low-latency IIR crossovers (Linkwitz-Riley family) are preferred
  over linear-phase FIR, which would add 10–40 ms of latency. CPU impact negligible.
- Decided: Frankenstein mode replaces the fader blend while active (faders inactive, % = share of
  the spectrum). Linkwitz-Riley 24 dB/oct tree with shared all-pass compensation, no added latency.

## 2b. Global EQ — IMPLEMENTED 2026-10-08, awaiting REAPER test
Owner's spec 2026-10-07. Input → gate detector → 5 paths (EQ / alignment) → blend or Frankenstein →
GLOBAL EQ → gate → OUTPUT → limiter. Same ten bands and graph as the amp EQ (low cut, 8 bells,
high cut), no alignment; EQ ON / FLAT / CLOSE. Vertical GLOBAL EQ button next to the gate + tuner,
lit and reading "ON" while active. Subdued, non-editable Global EQ curve behind an amp's EQ in EDIT
(only when on and not flat). Complete state stored in presets.

## 3. Stereo width: per-amp PAN — IMPLEMENTED 2026-10-08, awaiting REAPER test
- One PAN control per slot (centre by default), constant-power pan law, stereo output.
- Amps stay mono (no extra CPU). No stereo input processing.
- Centre = exactly as before; loudness compensation counts both channels; limiter stereo-linked.

## 3b. Cabinet IR per slot + IR EQ + slot redesign — IMPLEMENTED 2026-10-08, awaiting REAPER test
For amp-only captures. Capture → IR → IR EQ (same 10 bands, only while the IR is on) → amp EQ →
alignment → blend. Level match / alignment measured including the IR. IR editor = fourth view of
the lower panel. Slots shorter, six buttons (REMOVE = whole slot, click twice). IR path in presets.

## 3c. Effects (Global EQ / FX panel) — IMPLEMENTED 2026-10-08, awaiting REAPER test
Two parallel delays (exact ms or SYNC, feedback, level, tone, ping-pong), reverb (Room, Hall, Plate,
Cathedral, Ambience: decay, pre-delay, tone, level), flanger (rate, depth, feedback, mix). After the
gate, so tails ring out. All in presets.

## 3d. Standalone app + player / recorder (standalone only) — IMPLEMENTED 2026-10-08, awaiting test
Backing track (WAV/FLAC/MP3/OGG) + guitar recording of AMPSURD's output, live guitar audible during
playback, latency compensation + offset, export guitar alone or mixed (WAV 16/24/32f, FLAC; 44.1/48/96
kHz; default 16-bit/44.1), BOUNCE: backing + recording become the new backing track (layers).

## 3e. Tone parity + clicks + Frankenstein NOTES — IMPLEMENTED 2026-10-08, awaiting test
Fast tanh like the official NAM plugin, input calibration (interface dBu), dropout counter, smoother
gate re-open, Frankenstein NOTES mode (split before the amps) + note band.

## 3e+. CPU: silent amps sleep, several cores — IMPLEMENTED 2026-10-08, awaiting test

## 3f. Recorder timeline — IMPLEMENTED 2026-10-09, awaiting test
Waveform of the take with the backing track's waveform above it, zoom, click to navigate, punch-in
recording while the backing plays, draggable start edge of a correction (automatic crossfades, no
pops), undo. One guitar track (no multitrack).

## 3f+. Polish list 2026-10-11 + PITCH — IMPLEMENTED 2026-10-11, awaiting test
Red accent, thicker outlines, icon, smoother gate opening, INPUT -30...+15, EQ band count per EQ +
AUTO LEVEL, ECO per slot, timeline scrollbar, shortcuts (Space / R / W, Home), PITCH (tuning).

## 3h. Background dragon — IMPLEMENTED 2026-10-11 (sketch v3), final artwork can replace the layers

## 3g. Shareable presets (next)
`.ampsurd` (settings only; captures / IRs found again by file name in the capture folders) and
`.ampsurdpack` (zip: preset + the .nam / IR files it uses, with a licence reminder).

## 4. Public release package — installer, releases, update check DONE 2026-10-11; signing pending
(SignPath Foundation application by the owner; fallback: own OV certificate in the owner's name)
- Code signing (SignPath Foundation, free for OSI-licensed open source) so Windows Smart App Control
  accepts the plugin.
- Installer with uninstaller (VST3 + standalone app).
- Standalone Windows app: File (open/save preset), Audio settings, Help (Check for updates — asks
  GitHub Releases for the latest version only when chosen; disclosed in the privacy note; About).

## Backlog from the owner (recorded 2026-10-13, NOT implemented yet - wait for the go-ahead)
1. **Input and output level per amp** in EDIT, with automatic loudness compensation (changing an amp's
   input drive must not change its loudness in the mix; its output level is the deliberate change).
2. **MUTE moves the fader to 0 %**: the other faders show the real percentages without the muted amp;
   un-muting brings the amp back to its previous share and the others are rescaled accordingly
   (today: stored faders unchanged, only the effective percentages change).
3. **Gate: gentler action**, no step-like closing / re-opening (smoother attack and release curves).
4. **Footer:** move "BROUGHT TO YOU BY" a little to the left; new button bottom left
   **"Buy Ampsurd amp packs"** -> opens https://www.ampsurd.com/ in the default browser.
5. **Tone vs. the TONE3000 app** (owner 2026-10-13: TONE3000 sounds fuller, more natural, less scooped,
   juicier). Facts: AMPSURD with one capture is sample-identical to NAM Core (tone_test). Differences
   found in TONE3000's guide: new blocks default to **A2-Lite** (AMPSURD: Full unless ECO), captures with
   calibration data may use input calibration, optional oversampling (2x), per-block normalisation on,
   gate at the chain start, tone stack / EQs, Spread. Next: owner compares with matched settings (ECO,
   same input drive, gate off, single amp, equal loudness); then a recorded null test (same DI through
   both, outputs analysed band by band). TONE3000's plugin is open source: check its processing chain.

## Later / not planned
- Frankenstein FOLLOW (whole note routed by detected pitch) - postponed by the owner.
- Octave / harmony effects (PITCH only re-tunes).
- Stereo input processing (not planned: guitar DI is mono, doubles CPU).
- macOS (VST3 + AU + standalone) after 1.0: needs an Apple Developer account and someone with a Mac
  for testing / notarisation.
