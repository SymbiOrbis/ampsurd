# AMPSURD — Roadmap (agreed with the owner, 2026-10-07)

Order (owner, 2026-10-07): 1 gate + tuner → 2 Frankenstein → 3 per-amp pan → 4 release package.
Each step is tested in REAPER before the next starts.

## 1. Noise gate + chromatic tuner (centre area) — IMPLEMENTED 2026-10-07, awaiting REAPER test
- Shown in the large centre area while no amp is in EDIT; EDIT replaces both; closing EDIT brings
  them back. A small GATE indicator stays visible while editing.
- Gate, NS-2 style: detects on the clean DI input, gates AFTER the five amps are blended (removes
  high-gain capture hiss too). Controls: THRESHOLD, DECAY, on/off. On by default, threshold
  -70 dB RMS, decay 120 ms (only acts between notes). Threshold can be dragged on the level meter.
- Tuner: reads the clean DI input, keeps working when AMPSURD's BYPASS is on, optional
  "mute while tuning". On by default. (Not available when the DAW itself bypasses the plugin.)

## 2. "Create Frankenstein" — frequency-split blending
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
- Open design point: Frankenstein mode replaces the fader blend while active (faders inactive).

## 3. Stereo width: per-amp PAN
- One PAN control per slot (centre by default), constant-power pan law, stereo output.
- Amps stay mono (no extra CPU). No stereo input processing.

## 4. Public release package
- Code signing (SignPath Foundation, free for OSI-licensed open source) so Windows Smart App Control
  accepts the plugin.
- Installer with uninstaller (VST3 + standalone app).
- Standalone Windows app: File (open/save preset), Audio settings, Help (Check for updates — asks
  GitHub Releases for the latest version only when chosen; disclosed in the privacy note; About).

## Later / not planned
- Stereo input processing (not planned: guitar DI is mono, doubles CPU).
- ECO mode (A2 Lite), macOS / AU.
