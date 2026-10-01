# MONSTROSITY — Development Log

Newest entry first. Each entry: implemented / files / tested / known issues / build / next.

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
- `tools/`: `monstrosity_render`, `monstrosity_bench` (CPU table + hot-swap stress test),
  `compat_test.py` (compares against NAM Core's reference renderer).
- `tests/plugin_host_test.cpp`: hosts the built `.vst3` and renders through it.
- GitHub Actions workflow building the Windows VST3.

### Tested (Linux cloud build, x86-64; Windows build not yet run)
- **Compatibility/correctness**: all 9 example models shipped with NAM Core (A2 slimmable,
  A2 feature test, A1 standard, LSTM, slimmable WaveNet/container, condition-DSP WaveNet…)
  plus standalone A2-Full-only and A2-Lite-only files extracted from `A2.nam`: MONSTROSITY output
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
  (needs X11/ALSA dev headers for the plugin; `-DMONSTROSITY_BUILD_PLUGIN=OFF` builds core+tools only).
- Correctness test: build NAM Core's `render` tool, then
  `python3 tools/compat_test.py --ref <render> --ours build/tools/monstrosity_render --input <NAMCore>/example_audio/input.wav <models...>`

### Next recommended step
1. User builds on Windows and runs the Milestone 1 checklist in REAPER (BUILD_WINDOWS.md Part D).
2. Milestone 2: core `Engine` with 5-slot array (2 exposed), per-slot level, mixing-law
   measurements with real captures, inter-capture correlation diagnostics.
