# AMPSURD — Development Log

(Project renamed from MONSTROSITY to AMPSURD on 2026-10-07; older entries were renamed too.)

Newest entry first. Each entry: implemented / files / tested / known issues / build / next.

---

## 2026-10-07 — Session 2b: first REAPER feedback — Claude (Opus 5.5)

- Windows CI build green; plugin loads in REAPER once Smart App Control is off (0xc0e90002 = unsigned).
- CI artifact now contains the `AMPSURD.vst3` folder itself.
- EQ: band 1 = low cut, band 10 = high cut (12 dB/oct SVF, resonance Q 0.5–2, off at 20 Hz / 20 kHz);
  defaults bit-transparent; graph shows cut bands on the 0 dB line, frequency-only drag.
- Mouse wheel changes the Q of the nearest point (highlighted while hovering).
- Contrast: secondary text #b9bbbe, faint text #8f9195; small labels 1 px larger (EQ scale, CPU, footer).
- Tested: engine_test ALL PASS incl. new cut-band tests; UI re-rendered.
- Note: presets saved before this change keep their band-1/band-10 frequencies; those bands are now cuts.

---

## 2026-10-07 — Session 2: AMPSURD UI/UX spec, five-slot engine, mix law, EQ, alignment, presets — Claude (Opus 5.5)

### Implemented
- Renamed to AMPSURD (plugin code `Amp5`, manufacturer `Amps`). Spec stored verbatim as
  `docs/UI_SPEC.md`. AGPLv3 `LICENSE.md` (spec: open source). `PROJECT_STATUS.md` added.
- core: `Engine` (5 fixed paths, mute/solo-aware percentages, K-weighted covariance mix law),
  `PathAligner` (fractional delay, polarity, 90° all-pass phase rotation), `ParametricEq`
  (10 Simper-SVF bells), `CaptureAnalyzer` (test signal, auto alignment, measured level match,
  covariance). Level match now measured by AMPSURD instead of .nam metadata.
- plugin: 189 parameters, loader thread measures before swap-in, debounced re-measurement on INPUT /
  sample-rate change, linked faders with gesture snapshots, complete-rig presets
  (`Documents/AMPSURD/Presets/*.ampsurd`), missing-file handling, time-aligned BYPASS
  (`getBypassParameter`), constant latency.
- UI per spec: header (AMPSURD, PRESET menu, SAVE, SAVE AS, settings), five fixed slots with
  fixed-size wrapped filenames, vertical linked faders + %, EDIT area (monochrome EQ graph +
  ALIGNMENT AUTO/FREE/TIME/PHASE/RESET), master (INPUT, OUTPUT, meters, LIMIT, BYPASS, CPU),
  footer with three equal logo areas (placeholders; assets folder ready). Embedded Inter font
  (OFL), subset without character-substituting features so filenames render exactly.
- Tests/tools: `engine_test`, `mix_experiment`, `ui_snapshot`, rewritten `plugin_host_test`.

### Tested (Linux build; not yet on Windows/REAPER)
- engine_test ALL PASS (EQ 0.000 dB error; delay 0.2°; rotation ±0.7° 30 Hz–18 kHz; planted 37.3-sample
  offset + inverted polarity found exactly; real-engine loudness within 0.06 dB of target incl. rotation;
  mute/solo).
- mix_experiment (5 captures, 331 mixes): chosen law within ±0.02 dB (`docs/REVIEW.md` §5).
- Real VST3 via host: 1 capture bit-identical to NAM reference delayed by reported 104 samples;
  5 captures OUTPUT +12 dB → peak −1.00 dBFS (48 k) / −1.28 dBFS (44.1 k); latency 104/123;
  BYPASS = dry input delayed by latency, sample-identical; 5 capture paths saved in state.
- ui_snapshot: linked faders total 100 %; preset round trip 189/189 params + 5/5 slots; missing file →
  FILE MISSING; screenshots reviewed (`docs/screenshots/`).
- Regression: compat_test ALL PASS, limiter_test ALL PASS, hot-swap stress clean.

### Known issues
- See `PROJECT_STATUS.md`.

### Build
- Windows: `docs/BUILD_WINDOWS.md`. Linux dev: as before; extra options `-DAMPSURD_BUILD_HOST_TEST=ON
  -DAMPSURD_BUILD_UI_SNAPSHOT=ON`.

### Next recommended step
- Owner: GitHub repo, REAPER checklist (BUILD_WINDOWS Part D), logo files, EQ band-type decision.

---

## 2026-10-01 — Session 1b: no-clipping guarantee — Claude (Opus 5.5)

### Implemented
- `core/SafetyLimiter` — always-on lookahead brick-wall limiter, ceiling -1 dBFS, 1 ms
  lookahead, 120 ms release. Last stage of the plugin (after Output gain).
- Plugin: latency now = capture resampling latency + limiter lookahead; "LIMIT -x dB"
  indicator in the UI; clip LED kept only as a self-check that must never light.
- `tools/limiter_test` (guarantee, transparency, distortion); `plugin_host_test` accepts
  output/input gain arguments and prints output peak.
- Docs: owner decisions recorded in `REVIEW.md` §4.

### Tested
- limiter_test: 60 s of extreme signal (spikes to +60 dBFS, random blocks) at 44.1/48/96 kHz →
  max output exactly -1.000000 dBFS. Below ceiling: bit-identical to input delayed 48 samples.
  Sine +12 dB over ceiling: 0.026 % distortion (35 % if clipped).
- Real VST3: default gains → output identical to engine (just delayed 48 samples); Input +24 dB
  and Output +12 dB with A2, LSTM and A1 captures at 48 and 44.1 kHz → peak -1.0 dBFS.
  Latency reported 48 (48 kHz) / 71 (44.1 kHz).
- Regression: compat_test ALL PASS; hot-swap stress clean.

### Known issues
- Same as Session 1. Limiter ceiling is sample-peak (not oversampled true-peak); -1 dB margin.

### Next recommended step
- Owner: create GitHub repo (see chat), confirm AGPLv3, build on Windows + REAPER checklist.
- Then Milestone 2.

---

## 2026-10-01 — Session 1: review + Milestone 1 (one capture) — Claude (Opus 5.5)

### Implemented
- Specification review: `docs/REVIEW.md` (stack verified, corrections, licensing, decisions).
- Architecture: `docs/ARCHITECTURE.md`.
- CMake project with pinned dependencies fetched automatically (JUCE 9.0.3, NeuralAmpModelerCore
  v0.6.0 @0b3d3c9, AudioDSPTools @0827c6c, Eigen @bc3b398).
- `core/` (no JUCE): `CaptureModel` (loads any NAM-Core-supported `.nam` unmodified, runs
  slimmable files at Full, resamples when host rate ≠ model rate, loudness normalisation,
  latency), `CaptureSlot` (lock-free hand-over, 20 ms crossfade, deferred deletion).
- `plugin/`: VST3 with Input / Output / Normalise parameters, LOAD/REMOVE, background loader,
  latency reporting, state save/restore by file path, missing-file message, CPU %, meters,
  clip indicator. UI intentionally plain.
- `tools/`: `ampsurd_render`, `ampsurd_bench` (CPU table + hot-swap stress test),
  `compat_test.py` (compares against NAM Core's reference renderer).
- `tests/plugin_host_test.cpp`: hosts the built `.vst3` and renders through it.
- GitHub Actions workflow building the Windows VST3.

### Tested (Linux cloud build, x86-64; Windows build not yet run)
- **Compatibility/correctness**: all 9 example models shipped with NAM Core (A2 slimmable,
  A2 feature test, A1 standard, LSTM, slimmable WaveNet/container, condition-DSP WaveNet…)
  plus standalone A2-Full-only and A2-Lite-only files extracted from `A2.nam`: AMPSURD output
  matches NAM Core's reference `render` to within float rounding (≤ −124 dB, most identical)
  at block sizes 1, 32, 64, 256.
- **Resampling**: at 44.1 and 96 kHz output matches the 48 kHz reference within −52/−58 dB once
  the resampler's true latency is accounted for; latency reported 27 / 42 samples.
- **Real VST3 binary**: loaded via JUCE's VST3 host, capture restored through the plugin state
  (as a DAW does), output bit-identical to the engine after the 20 ms fade-in; saved state
  contains the capture path; latency reported 0 @48 kHz, 27 @44.1 kHz.
- **Stress**: 175 capture swaps (A2 / A1 / LSTM / Lite / empty) during processing with random
  block sizes 1–256 at 44.1 kHz: no crash, no NaN/Inf.
- **CPU** (one 2.8 GHz Xeon core, single thread): A2 Full ≈ 8–10 % realtime per capture
  (48 kHz, 32–256 samples); five A2 Full ≈ 47–55 %; five A2 Lite ≈ 9 %.

### Known issues
- AudioDSPTools resampler under-reports latency by ~2–3 samples (44.1/96 kHz). Handle in M3.
- Latency change on capture load is reported via `setLatencySamples`; some hosts only apply new
  latency after stop/start of playback.
- Plugin uses input channel 1 only (by design: mono guitar DI).
- Not yet built/tested on Windows or in REAPER.

### Build
- Windows: `docs/BUILD_WINDOWS.md`.
- Linux (dev): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
  (needs X11/ALSA dev headers for the plugin; `-DAMPSURD_BUILD_PLUGIN=OFF` builds core+tools only).
- Correctness test: build NAM Core's `render` tool, then
  `python3 tools/compat_test.py --ref <render> --ours build/tools/ampsurd_render --input <NAMCore>/example_audio/input.wav <models...>`

### Next recommended step
1. User builds on Windows and runs the Milestone 1 checklist in REAPER (BUILD_WINDOWS.md Part D).
2. Milestone 2: core `Engine` with 5-slot array (2 exposed), per-slot level, mixing-law
   measurements with real captures, inter-capture correlation diagnostics.
