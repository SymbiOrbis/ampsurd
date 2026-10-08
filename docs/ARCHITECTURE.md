# AMPSURD — Architecture

(Formerly MONSTROSITY. UI/UX specification: `docs/UI_SPEC.md`.)

## Layers

```
plugin/                 JUCE VST3 shell
  PluginProcessor       189 automatable parameters, loader thread, measurements, linked faders,
                        complete-rig presets, bypass, latency
  PluginEditor          fixed 1200x800 logical layout, scaled as a whole when resized
  ui/Theme              colours, embedded Inter font (SIL OFL), flat LookAndFeel
  ui/Components         header, 5 slots, mix faders, EQ graph, alignment panel, master, footer
  assets/fonts, assets/logos   embedded with juce_add_binary_data
core/  (ampsurd_core - plain C++20, no JUCE; reusable for the future single-capture product)
  CaptureModel          one .nam capture (NeuralAmpModelerCore get_dsp, any architecture), resampling
  CaptureSlot           lock-free hand-over, 20 ms crossfade, deferred deletion
  Engine                5 fixed paths + mixer and mix law, Frankenstein, Global EQ
  Frankenstein          frequency-split blending (Linkwitz-Riley tree, perfect reconstruction)
  NoiseGate, PitchDetector   NS-2 style gate (key = DI), YIN tuner
  Convolver             zero-latency non-uniform partitioned convolution (cabinet IRs), IR preparation
  IrSlot                per-slot IR: lock-free hand-over, warm-up + crossfade, bypass, idle when unused
  Effects               FxChain: flanger, 2 parallel delays (exact ms or SYNC), FDN reverb (5 types)
  Resampler             streaming polyphase sinc SRC (export, backing tracks)
plugin/player/PlayerRecorder   standalone only: backing track, take recording, export, bounce
plugin/standalone/StandaloneApp  the Windows app (JUCE standalone wrapper, input not muted)
  PathAligner           fractional delay (TIME), polarity, phase rotation (PHASE)
  ParametricEq          low cut + 8 bells + high cut, Simper SVF, smoothed, bit-transparent when flat
                        (one per amp + the Global EQ)
  CaptureAnalyzer       test signal, measurement, auto alignment, level match, covariance
  SafetyLimiter         always-on, never above -1 dBFS
tools/   ampsurd_render, ampsurd_bench, compat_test.py, limiter_test, engine_test, mix_experiment
tests/   plugin_host_test (hosts the built .vst3), ui_snapshot (renders the editor to PNG)
```

## Signal flow

```
DAW input ch 1 → INPUT gain ─┬→ slot 1: capture → [cab IR → IR EQ] → align (time/polarity/phase) → EQ → p1·G·level1·PAN1 ─┐
                             ├→ slot 2 … slot 5 (same)                                            ├→ Σ
                             ├→ DI → gate detector, tuner                                         │
                             └→ dry delay line (for BYPASS)                                       │
Σ left / Σ right (fader blend, or Frankenstein frequency split) → GLOBAL EQ (L, R)
  → gate (opens/closes from the DI, same gain on L and R)
  → FLANGER → DELAY 1 + DELAY 2 (parallel) → REVERB (after the gate, so tails ring out) → OUTPUT gain
  → safety limiter (≤ -1 dBFS, stereo-linked) → (crossfade with dry when BYPASS) → output L / R
PAN: constant power, centre = 0 dB on both sides (centred amps: L and R identical to the former mono
output), hard left/right = +3 dB on one side. Mix law G counts the power of both channels (BS.1770).
Mono output bus: (L + R) / 2.
Standalone app only: output → record the take (32-bit float WAV) → + backing x vol + take x vol →
stereo safety limiter (-1 dBFS, +1 ms) → device. Takes are placed at song position (recording
position - AMPSURD latency - device in/out latency - 1 ms - OFFSET), i.e. where they were played
against the backing that was heard. Export: same mix, rendered offline, limited, resampled
(StreamResampler) to 44.1/48/96 kHz, TPDF-dithered to 16/24 bit or 32-bit float, WAV or FLAC.
Cabinet IR (optional, per slot): zero latency; level match, AUTO alignment and the mix law are measured
on capture + IR (re-measured when the IR is loaded, replaced, removed or bypassed). IR EQ = the same
10-band EQ, only active while the IR is on.
```

Latency (reported to the DAW, constant while playing) = resampler latency (only when host rate
≠ capture rate) + 8 samples (fractional-delay centre) + 1 ms alignment reserve + 1 ms limiter.
48 kHz: 104 samples (2.17 ms). 44.1 kHz: 123 samples.

## Mix law

`p_i` = stored fader value / sum over audible loaded slots (mute/solo aware). Faders are linked
in the UI (moving one rescales the others, keeping their proportions; total stays 100 %), but the
engine normalises anyway, so host automation of a single fader is safe. Loudness compensation G is
computed from the measured K-weighted covariance (docs/REVIEW.md §5).

## Measurement ("analysis") — never on the audio thread

1. On load (loader thread): the capture is loaded, a 3 s guitar-like DI test signal (× INPUT gain)
   is rendered through it, its spectrum is stored, the model state is reset, the whole rig is
   re-analysed, alignment / level match / covariance are published — and only then the capture is
   swapped in. So nothing jumps after a capture becomes audible.
2. Rig analysis: reference = lowest-numbered loaded slot. Every other capture's offset and
   polarity vs. the reference = maximum of the 70–1200 Hz band correlation (sub-sample by parabolic
   interpolation, ±5 ms search; correlation < 0.3 → "too different", left unaligned).
3. Re-measured when INPUT changes by > 0.5 dB (after 0.4 s of no change) or the sample rate changes.

## Alignment controls (per slot)

- **AUTO** (default): measured offset + polarity applied.
- **FREE**: AUTO plus manual **TIME** (±1 ms, fractional delay — a real time shift, so its
  phase effect grows with frequency) and **PHASE** (±180°, frequency-independent phase rotation
  without delay, via a 90° all-pass pair; when any slot uses it, all slots pass through the same
  all-pass network so they stay comparable). **RESET** returns to AUTO with zero offsets.

## Threads

| Thread | Does | Never does |
|---|---|---|
| Audio | build settings from parameter atomics, engine, gains, limiter, bypass | allocate, lock, file I/O, parse, FFT |
| Loader (1) | load, render test signal, FFT, rig analysis, submit captures, re-measure | touch a capture that is playing |
| Message | UI, presets, linked faders, covariance updates for TIME/PHASE, latency, garbage collection | process audio |

Lock order (non-audio threads only): configMutex → derivedMutex → analysisMutex; statusLock is
never held while taking another lock.

## State / presets

One preset = the complete rig: all 189 parameters + one capture **path** per slot (never the
capture data). Same XML for DAW projects and `.ampsurd` preset files
(`Documents/AMPSURD/Presets`). Missing captures: the path is kept and the slot shows FILE
MISSING; a capture with the same file name next to the preset file is used automatically.
