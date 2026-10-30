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

## 3g. Shareable presets — IMPLEMENTED 2026-10-14 (session 2q), awaiting test
`.ampsurdpack` (zip: preset + the .nam / IR files it uses + README with a licence reminder; the preset
refers to files by name only). Import unpacks into Documents\AMPSURD\Packs\<name> and loads it. Any
preset whose files moved is found again by file name (next to the preset, then in the AMPSURD folders).

## 3i. Live use: MIDI, undo, A/B — IMPLEMENTED 2026-10-14 (session 2q), awaiting test
- MIDI footswitch / controller (plugin + standalone): Program Change n = n-th preset; MIDI learn for
  next / previous preset, A/B, bypass, gate, Frankenstein, effects on/off, amp mute / solo, input /
  output / amp input as controllers; momentary or latching footswitches; settings per computer.
- Presets switch without reloading captures / IRs that stay the same (fast between rigs that share captures).
- Undo / redo of all settings (50 steps, Ctrl+Z / Ctrl+Y, header buttons), A/B compare with copy.

## 3k. Metronome in the recorder — IMPLEMENTED 2026-10-17, awaiting test

## 3l. Preset banks for MIDI pedals — PROPOSED 2026-10-17 (waiting for the owner's pedal details)
Bank = sub-folder of the preset folder; footswitch actions Bank up / down and Preset 1..N of the bank;
standard Bank Select (CC 0 / 32) + Program Change; banks as groups in the preset menu; header shows
"Bank 2 - Preset 3".

## 3m. Full A2 by default (ECO off) - IMPLEMENTED 2026-10-28
Reverse the 2026-10-16 default: new captures load as Full A2; ECO (Lite) stays one click per amp and the
setting "Load A2 captures as Lite (ECO)" becomes off by default (existing users keep what they chose;
presets keep their own ECO state).

## 3n. AMPSURD IR matrix - IMPLEMENTED 2026-10-28 with the owner's synthetic set (docs/ampsurd_irs), awaiting test
Own cabinet IR set (e.g. 3 speakers x 3 mics x up to 16 positions = 144 IRs) chosen from a pop-up grid
(speaker / mic / position on the axes); audition by click or hover while playing (crossfade, no clicks);
ideally one continuous area: the IR between grid points is interpolated (neighbouring IRs time-aligned,
blended; minimum-phase magnitude morph as an alternative), with "save this IR" as WAV. UI, crossfade,
interpolation and saving: feasible. The blocker is the IRs themselves: they must be MEASURED on real
cabinets and mics (the demo IR in the website screenshots is shaped noise, not a speaker - not usable as a
product). Options: the owner records a set (sine sweep, deconvolution tool can be added), or a licensed set
with written redistribution consent (see 3j).
2026-10-28 evening: the owner sent his synthetic set (AMPSURD_IRs v6, 3 speakers x 3 mics x 16 positions, with
its generator). Ported exactly to C++ (core/CabIrModel), so every point - also between the grid - is calculated
live; map overlay with click / drag / hover / snap, SAVE IR, EXPORT ALL 144. Next: listening tests; maybe a
response curve of the chosen point on the map.

## 3o. Serial chain per amp slot - STEP 1 IMPLEMENTED 2026-10-28 (2 pedal captures before each amp), awaiting test
Behind (or before) each of the five parallel slots a chain of up to 5 blocks: NAM captures (pedal -> amp),
EQ, IR, effects; opened from EDIT. Feasible; the alignment / level match must measure the whole chain
(already measured per slot). Costs: CPU (one A2 ~8-10 % of a core; 25 A2s would not run in real time),
gain staging between captures, presets / packs / undo / UI. Suggested first step: one or two capture
blocks BEFORE the amp in each slot (boost / overdrive pedal captures), with a CPU meter warning.
Step 1 done: EDIT -> BEFORE THE AMP: PEDAL 1 -> PEDAL 2 -> AMP, each ON + LEVEL; measured with the amp.
Later: effects / EQ blocks in the chain, more blocks, captures AFTER the amp (e.g. a power amp / cab).

## 3p. EQ COPY easier to get rid of - IMPLEMENTED 2026-10-28 (all four)
Owner: after an accidental COPY the EQ buttons keep showing COPIED / PASTE, even after a paste - "sticky".
Today: clicking COPIED on the copied EQ cancels (only said in the tooltip); the copy stays after a paste.
Proposed: the copy ends after one paste; Ctrl+Z / the undo arrow and Esc cancel a copy; an unused copy
ends by itself after ~20 s; the button reads "COPIED - CANCEL" so cancelling is visible.

## 3q. EQ presets - IMPLEMENTED 2026-10-28 (mechanism; factory curves: the owner chooses them -> plugin/assets/eq)
A list of factory EQ curves chosen by the owner (e.g. "tighten lows", "less fizz") plus user EQ presets saved
under any name; usable on every EQ (amp, IR, Global). Small files in Documents/AMPSURD/EQ (shareable);
loading = one undo step; factory set shipped with the app. Feasible, small to medium job; ties in with 3p
(the EQ buttons row: COPY / PASTE / a preset menu).

## 3r. AMPSURD tones (.ampsurdtone) - IMPLEMENTED 2026-10-30, awaiting test
One amp's sound as a file (capture, pedals, IR, EQs, levels; no rig settings). Later: a tone pack that carries the
files (like .ampsurdpack), tones in the preset menu, MIDI selection of tones.

## 3s. AMPSURD amp profile (gain captures + tone stack) - IMPLEMENTED 2026-10-30 (.ampsurdamp, docs/AMPSURD_AMP.md), awaiting the owner's recordings
Gain captures every hour from 9 to 3 o'clock (tone at noon) + clean tone-control sweeps (bass / mid / treble);
AMPSURD morphs between gain captures and applies the measured tone stack as an EQ; .ampsurdamp container with the
standard .nam files inside. Recording sheet + AMPSURD_tone_sweep_48k.wav sent to the owner 2026-10-30. Later: a
gain-conditioned model trained from the same recordings.

## 3j. Bundled cabinet IRs — ON HOLD (owner decision 2026-10-14)
No IRs ship with AMPSURD until an author gives explicit written consent to redistribution (Jester
Dyne's packs: pages say "free even for commercial use", no redistribution wording; the site was
unreachable for the owner on 2026-10-14). Users load their own IRs (ADD IR) as before.

## 4. Public release package — installer, releases, update check DONE 2026-10-11; signing pending
(SignPath Foundation application by the owner; fallback: own OV certificate in the owner's name)
- Code signing (SignPath Foundation, free for OSI-licensed open source) so Windows Smart App Control
  accepts the plugin.
- Installer with uninstaller (VST3 + standalone app).
- Standalone Windows app: File (open/save preset), Audio settings, Help (Check for updates — asks
  GitHub Releases for the latest version only when chosen; disclosed in the privacy note; About).

## Backlog from the owner (recorded 2026-10-13) — IMPLEMENTED 2026-10-14 (session 2p), awaiting test
1. **Input and output level per amp** in EDIT (LEVEL: INPUT -24...+24 dB, OUTPUT -24...+12 dB). INPUT keeps
   the amp's loudness automatically: each capture's INPUT -> loudness curve is measured in the background
   (9 points, with its IR), interpolated; tested within 0.2 dB from -15 to +21 dB.
2. **MUTE moves the fader to 0 %**, the others show their real shares; un-muting restores the previous share
   (also after saving / loading a preset). A muted fader cannot be dragged.
3. **Gate: gentler action**: 8 dB hysteresis, 60 ms hold, S-shaped closing (starts at zero slope), 25 ms
   fade-in when a note swells back without a new pick; a new pick still opens in 0.9 ms.
4. **Footer:** "BUY AMPSURD AMP PACKS" bottom left (opens https://www.ampsurd.com/), logos moved left.
5. **Tone vs. the TONE3000 app**: TONE3000's source (MIT, github.com/tone-3000/tone3000-plugin) checked.
   Its default A2 size is **Lite** (namSlimSizeDefault 0.0); otherwise the same NAM Core, a 5 Hz DC blocker
   (inaudible), flat tone stack at 5, loudness normalisation from metadata (+-12 dB), cab IRs normalised to
   unit energy with a -18 dB pad (level only). Measured with the example A2: Lite vs Full differs by
   -1.1 dB at 44 Hz and +0.3...+0.8 dB from 700 Hz to 11 kHz (more mids / presence). New setting
   "Load A2 captures as Lite (ECO) - like TONE3000", on by default; presets keep their own ECO state.
   Owner: compare again (same capture, both Lite, gate off, level matched).

## ROOT (experimental low-end module) — IMPLEMENTED 2026-10-20, needs listening tests
docs/ROOT_MODULE.md: research, design, tests, how to remove it. Next: testers with low-tuned guitars;
keep / change / remove. Then: review of Frankenstein (owner, 2026-10-20).

## DOUBLER (experimental double tracking) — IMPLEMENTED 2026-10-22, needs listening tests
docs/DOUBLER.md: design, tests, how to remove it. Idea from a tester (MIMIQ-style, his REAPER script as
inspiration; own code). Next: listening tests; maybe later a real second take in the player / recorder
(the most authentic double).

## Later / not planned
- Frankenstein FOLLOW (whole note routed by detected pitch) - postponed by the owner.
- Octave / harmony effects (PITCH only re-tunes).
- Stereo input processing (not planned: guitar DI is mono, doubles CPU).
- Two interface inputs at once (two guitars, or guitar + mic): asked 2026-10-20, owner: one input for now
  (input 1 or 2 is selectable since 2026-10-20).
- macOS signing / notarisation: needs an Apple Developer account (99 USD / year). The macOS build itself
  (VST3 + AU + standalone, universal, .pkg) exists since 2026-10-19 (docs/BUILD_MACOS.md); it still needs
  a person with a Mac to test it with an interface and in Logic / GarageBand.
- iPad / iPhone (AUv3 app): not planned - a separate touch layout, App Store only (Apple Developer
  account, review), and the iOS audio-input / sandbox limits.
