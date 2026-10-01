# MONSTROSITY — Architecture (Milestones 1–3)

## Layers

```
plugin/        JUCE VST3 shell: parameters, state save/restore, temporary UI, loader thread
core/          monstrosity_core — plain C++20, no JUCE. Reusable (e.g. the future single-capture product)
  CaptureModel   one loaded .nam capture: load (NAM Core get_dsp), resample, normalise, process
  CaptureSlot    real-time home of one capture: lock-free hand-over, 20 ms crossfade, safe deletion
  SafetyLimiter  always-on output protection: never above -1 dBFS (1 ms lookahead)
external (CMake FetchContent, pinned commits)
  NeuralAmpModelerCore v0.6.0, AudioDSPTools, Eigen, JUCE 9.0.3
tools/         monstrosity_render, monstrosity_bench, compat_test.py   (correctness / CPU / stress)
tests/         plugin_host_test — loads the built .vst3 like a DAW and renders audio
```

## Compatibility rule

Captures are loaded with NeuralAmpModelerCore's own `nam::get_dsp()` on the unmodified file.
Every architecture NAM Core supports loads: A2 Full, A2 Lite, slimmable A2 containers, A1
WaveNet (standard/lite/feather/nano), LSTM, ConvNet, Linear. No wrapper format, no conversion.
Slimmable files are run at full size. Only requirement: mono in / mono out (all guitar captures).

NAM Core registers architectures through static initialisers; the build links it with
`WHOLE_ARCHIVE`, otherwise the linker silently drops them (caught by `compat_test.py`).

## Signal flow (Milestone 1)

```
DAW input ch 1 → input gain → [CaptureSlot → CaptureModel: (resample to 48k) NAM (resample back)
               → loudness normalise] → output gain → safety limiter (≤ -1 dBFS) → all output channels
```

Internal processing is double precision (NAM Core's default sample type).

## Threads

| Thread | Does | Never does |
|---|---|---|
| Audio | `CaptureSlot::process`: adopt pending capture, run NAM, crossfade | allocate, lock, file I/O, parse, delete |
| Loader (1 background thread) | read & parse `.nam`, build model, allocate, prewarm, `submit()` | touch the active model |
| Message | UI, `collectGarbage()` (deletes retired models), latency reporting | process audio |

Hand-over: `submit()` publishes a fully prepared model through an atomic pointer. The audio
thread swaps it in at a block boundary and crossfades from the old capture for 20 ms (no clicks).
The old capture goes into a lock-free single-producer/single-consumer queue and is deleted on
the message thread. A config mutex (taken only by `prepareToPlay` and the loader, never by the
audio thread) guarantees a capture is never prepared for a stale sample rate / block size.

## Milestone 2 plan (two captures)

- `Engine` in core: fixed array of `kMaxSlots = 5` `CaptureSlot`s (only 2 exposed in M2), shared
  input, per-slot gain smoothing, mute/solo, mixing law under test (see REVIEW §5).
- Diagnostics: per-slot latency, per-slot peak/RMS, inter-capture correlation.

## Milestone 3 plan (alignment)

- Offline analysis on the loader thread when a capture is added: run a fixed guitar-like test
  signal through each capture, estimate relative delay (GCC-PHAT / band-limited
  cross-correlation, sub-sample by interpolation) and polarity.
- Per-slot fractional delay line (Thiran all-pass or windowed-sinc, smooth modulation) +
  polarity; latency of the plugin = max applied delay.
- Manual "FREE" offset on top of AUTO, with reset. All-pass phase-rotation variant prototyped
  in parallel for listening comparison.
