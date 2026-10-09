# AMPSURD — Project Status

_Last updated: 2026-10-08 evening (Claude, session 2j)_

**Product name:** AMPSURD (formerly MONSTROSITY / QUINQUEPLEX).
**What it is:** free, open-source (GNU AGPLv3) guitar plugin: one DI signal → up to five Neural
Amp Modeler captures in parallel → blended into one sound. Windows VST3 first.
**Current UI/UX specification:** `docs/UI_SPEC.md` (verbatim, 2026-10-07).

> Everything below marked **tested** was run on a Linux build of the same code (unit tests,
> the real VST3 binary loaded by a test host, rendered UI screenshots). **Windows:** since
> 2026-10-07 GitHub Actions builds the Windows x64 VST3 with MSVC on every push and runs
> `limiter_test`, `engine_test`, `gate_tuner_test`, `frankenstein_test` and a capture load/hot-swap stress test there — first run passed.
> **Not yet tried in REAPER by a person.**

## First REAPER test by the owner (2026-10-07, Windows 11, 48 kHz)

Loads and runs after switching off Windows Smart App Control (unsigned plugin; code signing via
SignPath Foundation planned for public release). Fader linking, mixing, capture switching, SOLO/MUTE
(mute overrides solo — intended), FREE alignment, limiter, presets, project recall and missing-file
handling all OK. CPU ≈ 20 % with five captures. Changes requested and made: brighter small text,
EQ bands 1/10 as low/high cut, mouse wheel adjusts the nearest EQ point.

## Owner test of the standalone app (2026-10-08 evening, ASIO 48 kHz / 128)

Reported: clicks with one capture that got worse over time and stopped after restarting the app;
tone duller / less gain / less mid fullness and high-end clarity than the TONE3000 app with the same
.nam (partly a lower input level); Frankenstein not changing the sound across a scale; divider 3/4
stuck; blinking spot; CPU meter unreadable. Session 2j response: measured AMPSURD = NAM Core
bit-identical (tone_test), fast tanh like the official plugin, input calibration, dropout counter,
smoother gate, Frankenstein NOTES mode + note band, divider fix, averaged CPU meter. Session 2k:
silent amps no longer use CPU; amp paths run on several cores (>= 4-core computers). Awaiting
re-test (BUILD_WINDOWS.md checklist 18-20). Frankenstein "FOLLOW" (route the whole note by detected
pitch) proposed, postponed by the owner. Session 2l (2026-10-09): recorder timeline with waveforms,
zoom, navigation, punch-in corrections with movable edges and crossfaded joins, undo.

## Implemented and tested

| Area | Status | How it was tested |
|---|---|---|
| NAM loading (A2 Full, A2 Lite, slimmable A2, A1, LSTM, ConvNet…, unmodified .nam) | done | `compat_test.py`: all 9 NAM Core example models match the official renderer (≤ −124 dB, most bit-identical) |
| Single capture through the real VST3 | done | output **bit-identical** to the official NAM reference, delayed by exactly the reported latency |
| Five fixed slots, load / remove / swap with 20 ms crossfade | done | VST3 host test with 5 captures; 93 hot-swaps under random block sizes, no glitches/NaN |
| Linked, normalised mix faders + percentages | done | moving fader 1 to 42 % → others 14.5 % each, total 100 %; engine normalises automation too |
| Mix law (constant perceived loudness) | done, measured | `mix_experiment`: within ±0.02 dB of target over 331 mixes (vs. −4.6 dB for plain percentages) — `docs/REVIEW.md` §5 |
| Level match (measured by AMPSURD, K-weighted) | done | 5 captures 14 dB apart via file metadata → all at −18.0 dB |
| SOLO / MUTE (stored mix kept) | done | `engine_test` |
| EDIT selection, 10-band EQ per path: band 1 low cut, bands 2–9 bells, band 10 high cut (12 dB/oct); wheel changes the nearest point; EQ OFF bypasses all bands | done | measured response = drawn curve (0.000 dB error); default EQ bit-transparent; low cut never boosts |
| AUTO alignment (offset + polarity) | done | finds a planted 37.3-sample offset + inverted polarity exactly |
| FREE: TIME (fractional delay ±1 ms), PHASE (rotation ±180°), RESET | done | delay accurate to 0.2°, rotation flat ±0.7° from 30 Hz–18 kHz; mix law holds with rotation |
| Master: INPUT, OUTPUT, meters, BYPASS (time-aligned dry) | done | bypass output = dry input delayed by latency, sample-identical |
| No digital clipping (−1 dBFS safety limiter) | done | 5 captures, OUTPUT +12 dB, 44.1/48 kHz → peak −1.00 / −1.28 dBFS |
| Complete-rig presets: PRESET menu, SAVE, SAVE AS, DAW project state | done | round trip 429/429 parameters + IR files (older presets: new parameters at defaults) + 5/5 slots; missing capture → FILE MISSING, no crash |
| Fixed five-slot layout, fixed-size filename typography with wrapping | done | screenshots in `docs/screenshots/` (filenames shown character-exact, e.g. "4x12") |
| Branding footer with three equal logo areas | placeholders | real logos: drop files into `plugin/assets/logos/` (see README there) |
| Resizable window (scales whole UI, fixed aspect) | done | rendered at 1200x800 and 1800x1200 |
| Noise gate, NS-2 style (detects on DI, gates after the amps), on by default at -70 dB RMS / 120 ms; GATE button in master row | done, awaiting REAPER test | `gate_tuner_test`: bit-transparent while playing, closed on -80 dB hiss, fully open 0.98 ms after a note starts (before the amp signal, 1.17 ms); off = bit-identical |
| Chromatic tuner on the clean DI (works when BYPASS is on), MUTE OUTPUT while shown | done, awaiting REAPER test | correct note F#1..E6 (46 Hz-1.3 kHz), worst error 0.14 cents; no false notes on hiss |
| Centre area: gate + tuner; EDIT replaces them, CLOSE returns | done | screenshots |
| Create Frankenstein: 2–5 frequency sections, amp per section, draggable dividers, WIDTH 0–90 %, muted amp's section closes (neighbours meet in the middle), solo = full spectrum | done, awaiting REAPER test | `frankenstein_test`: same amp in all sections → flat 0.0000 dB; split −6.0/−6.0 dB at the divider, −49 dB out of band; random dragging bounded; real captures on/off clean, loudness −0.08 dB; no added latency |
| Global EQ on the complete sound (same 10 bands as the amps), GLOBAL EQ button with ON/OFF state, grey Global EQ curve behind an amp's EQ | done, awaiting REAPER test | `engine_test`: OFF bit-identical, ON = verified EQ curve exactly, on/off click-free; presets 236/236; older presets load with it off |
| Per-amp PAN, stereo output (constant power, centre unchanged), stereo-linked limiter | done, awaiting REAPER test | centred: L = R bit-identical to before; hard left = +3.01 dB left, silence right; loudness on target for any panning (≤ 0.06 dB); 5 panned captures +12 dB → −1.00 dBFS |
| Cabinet IR per slot (zero latency, WAV/AIFF/FLAC, any rate), IR EQ (10 bands), IR editor, IR in presets; slot redesign with six buttons | done, awaiting REAPER test | `ir_test`: exact convolution, zero latency, ~2 % CPU for a 1 s IR, click-free load/replace/bypass/remove; level match includes the IR (−17.84 vs −17.93 dB); presets 401/401 + IRs |
| Effects after the gate: 2 parallel delays (exact ms / SYNC, ping-pong), reverb (Room, Hall, Plate, Cathedral, Ambience), flanger; GLOBAL EQ / FX panel | done, awaiting REAPER test | `fx_test`: echoes exact to the sample, RT60 within 12 %, all changes click-free, all off bit-identical; worst case -1.00 dBFS; presets 429/429 |
| Standalone Windows app (input live by default) with PLAYER / REC: backing track (WAV/FLAC/MP3/OGG), guitar recording, pause, export WAV/FLAC 16/24/32f at 44.1/48/96 kHz, BOUNCE for layers | done, awaiting test on a real interface | `recorder_test`: recorded notes land exactly on the backing's clicks (simulated interface latency), all formats correct, never above -1 dBFS; app starts (virtual display) |

Latency: 104 samples (2.17 ms) at 48 kHz, 123 at 44.1 kHz (constant; includes 1 ms limiter
look-ahead and 1 ms alignment reserve). CPU: five A2 Full captures ≈ 40–48 % of one 2.8 GHz cloud
core at 64–128 samples; the full plugin with 5 mixed captures ran at 30 % of real time.

## Still mock-up / planned / open

- **Windows build + REAPER listening test** — next step, needs the owner (see below).
- Real footer logos (official artwork only).
- Mix-law and alignment validation with real amp captures (only NAM test models so far).
- Product decision pending: default window size on small screens.
- Later: installer, ECO (A2 Lite) mode, macOS/AU, About/Easter egg.

## Files changed in session 2

Renamed everything to AMPSURD. New: `core/{Engine,PathAligner,ParametricEq,CaptureAnalyzer}`,
`plugin/ui/{Theme,Components}`, `plugin/assets/`, `tools/{engine_test,mix_experiment}.cpp`,
`tests/ui_snapshot.cpp`, `docs/UI_SPEC.md`, `docs/screenshots/`, `LICENSE.md` (AGPLv3),
`PROJECT_STATUS.md`. Rewritten: `plugin/PluginProcessor.*`, `plugin/PluginEditor.*`,
`tests/plugin_host_test.cpp`, docs.

## Known issues

- Loading a capture takes ~0.3 s longer than before (it is measured before it is heard).
- After changing INPUT, captures are re-measured in the background (~1–2 s for five); level match
  and alignment then glide to the new values.
- PHASE rotation is accurate to ±2° from 20 Hz at 44.1/48 kHz; at 96 kHz accuracy drops below ~40 Hz.
- AudioDSPTools resampler under-reports latency by ~2–3 samples at 44.1/96 kHz; AUTO alignment
  absorbs it between captures, the DAW-reported latency is off by those samples.
- Default window 1200x800 does not fit 768-pixel-high laptop screens (it can be resized smaller).
- Windows build and tests pass in CI, but the UI (fonts, file dialogs, high-DPI scaling) has not been seen on a Windows screen yet.

## Build & test

- Windows: `docs/BUILD_WINDOWS.md` (or download the CI build once GitHub is set up).
- Tests (any platform): `tone_test <a.nam> [rate] [block] [interfaceDbu]` (AMPSURD vs NAM Core),
  `soak_test <a.nam> [minutes]` (CPU over time), `limiter_test`, `gate_tuner_test`, `frankenstein_test`, `ir_test`, `fx_test`, `recorder_test`, `engine_test <a.nam> <b.nam>`, `mix_experiment <2-5 .nam>`,
  `compat_test.py`, `plugin_host_test`, `ui_snapshot` — see `docs/DEVLOG.md`.

## Roadmap

See `docs/ROADMAP.md`: gate + tuner (done) → Create Frankenstein (done) → Global EQ (done) → per-amp pan (done) → cabinet IRs (done) → effects (done) → standalone + player/recorder (done) → release package.

## Next concrete step

1. Done: repository https://github.com/SymbiOrbis/ampsurd, Windows CI build green.
2. Owner: download the VST3 from GitHub Actions (BUILD_WINDOWS.md, top), then the REAPER checklist in `docs/BUILD_WINDOWS.md` Part D with real captures; send notes.
3. Owner: provide the three logo files; decide on EQ band types.
4. Claude: fix whatever the re-test finds; then shareable presets (ROADMAP 3g), release package (4).
