# AMPSURD — Roadmap (agreed with the owner, 2026-10-07)

Order (owner, 2026-10-07/08): 1 gate + tuner → 2 Frankenstein → 2b Global EQ → 3 per-amp pan → 3b cabinet IRs → 4 release package.
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

## 4. Public release package
- Code signing (SignPath Foundation, free for OSI-licensed open source) so Windows Smart App Control
  accepts the plugin.
- Installer with uninstaller (VST3 + standalone app).
- Standalone Windows app: File (open/save preset), Audio settings, Help (Check for updates — asks
  GitHub Releases for the latest version only when chosen; disclosed in the privacy note; About).

## Later / not planned
- Stereo input processing (not planned: guitar DI is mono, doubles CPU).
- ECO mode (A2 Lite), macOS / AU.
