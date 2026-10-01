# MONSTROSITY — Specification Review (2026-10-01)

Review of the ChatGPT handover against the current state of the technology, written before
and during Milestone 1. Product concept is unchanged; items below are technical corrections,
clarifications and decisions.

## 1. Stack verdict: C++ / JUCE / NeuralAmpModelerCore / CMake / VST3 — keep it

| Component | Current state (checked 2026-10-01) | Verdict |
|---|---|---|
| NeuralAmpModelerCore | v0.6.0 (2026-09-18), MIT. Supports A2 (with a hand-optimised A2 fast path), slimmable A2 containers, A1 WaveNet, LSTM, ConvNet, Linear. | Use unmodified, pinned. |
| JUCE | 9.0.3. Dual licence: AGPLv3 or JUCE 9 EULA (Starter tier free). Bundles the VST3 SDK. | Good fit. Licence note below. |
| VST3 SDK | Steinberg relicensed it under **MIT** (bundled copy in JUCE 9 is MIT). | No separate Steinberg agreement needed for the SDK. VST logo/trademark use has its own guidelines. |
| AudioDSPTools | MIT; resampler portions under the iPlug2 zlib-style licence. Same resampler the official NAM plugin uses. | Use for sample-rate conversion. |
| Eigen | MPL-2.0 (file-level copyleft). Used unmodified. | Fine; include notice. |

Realistic alternative: **iPlug2** (the official NAM plugin's framework, zlib-style licence, no
revenue limits). Not recommended now — JUCE has far better documentation, tooling and AI-agent
familiarity — but the audio engine is deliberately framework-independent (`core/`), so a later
switch would only replace the thin `plugin/` layer.

## 2. Incorrect or incomplete assumptions in the handover

1. **NAM captures run at a fixed sample rate (almost always 48 kHz).** At 44.1 or 96 kHz the
   plugin must resample around every capture. This adds latency (measured: 27 samples at
   44.1 kHz, 42 samples at 96 kHz, 0 at 48 kHz) which is reported to the DAW. Captures with the
   same native rate receive *identical* resampling latency, so resampling never misaligns them.
   Recommendation for best quality and lowest CPU: run the project at 48 kHz.

2. **"A2 Full" is often not a separate file.** Current A2 `.nam` files (including TONE3000's
   retrained A2 files) are typically *slimmable containers* holding A2 Lite and A2 Full in one
   file. MONSTROSITY runs them at Full. A possible later ECO mode needs no extra files: it is a
   size switch on the same file. Measured: A2 Lite costs ~1/7 of A2 Full.

3. **NAM itself adds no latency between captures.** NAM models are causal networks with no
   algorithmic delay. Timing/phase differences between captures are *learned into the models*
   (re-amp chain latency calibration, microphone distance in full-chain captures, cabinet and
   filter phase response). Consequences for automatic alignment (Milestone 3):
   - It cannot be derived from reported latency; it must be **measured from the captures'
     outputs** (run the same test signal through each capture and compare).
   - The models are non-linear, so the measured offset can depend on input level and on
     frequency band. The measurement should use a guitar-like signal at the actual input gain
     and focus on the low/low-mid band where cancellation hurts most.
   - **Polarity** must be detected separately (some capture chains invert polarity). Auto
     alignment = integer/fractional delay + polarity, not one knob.
   - Frequency-dependent phase differences (different cabinets/mics) cannot be removed by a
     delay at all; auto alignment only finds the best compromise.

4. **"Movement within one waveform cycle" has no single meaning** for a guitar signal (the
   cycle length depends on the note and on harmonics). Proposed interpretation for the
   Milestone 3 experiment: a time offset of roughly 0–2 ms per capture (fractional-sample
   delay, smooth while turning) plus a polarity switch; an all-pass "phase rotation" variant
   will be prototyped alongside so both can be judged by ear. Positive delays add latency to
   the whole plugin; the engine will delay the other paths instead of moving the mix earlier.

5. **The AudioDSPTools resampler under-reports its own latency by ~2–3 samples** (measured:
   ~1.9 samples at 44.1 kHz, ~3.0 at 96 kHz). Harmless for one capture (≈0.05 ms), but it
   matters if a 48 kHz capture is mixed with a capture trained at another rate. Milestone 3's
   measured alignment will cover it; recorded as a known issue.

6. **Input level matters a lot for amp captures.** Many `.nam` files contain a calibrated input
   level (dBu). The Studio 26c's input sensitivity determines how hard each capture is driven.
   Milestone 1 provides an Input gain knob; proper input calibration (as in the official NAM
   plugin) is a polish item for later — flagged here because it influences how the mix sounds.

7. **CPU**: five A2 Full captures are feasible on one audio thread. Measured on a 2.8 GHz cloud
   Xeon core at 48 kHz / 64 samples: one A2 Full ≈ 9 % of one core, five ≈ 49 %. The Ryzen-based
   AI X1 Pro should be considerably faster. Parallelising captures across CPU cores is possible
   later but adds real-time risk; not planned unless measurements demand it.

## 3. Licensing implications (flag early, decide before public release)

- **JUCE 9 Starter** licence is free and allows closed-source products, but only while total
  annual revenue stays at or below USD 20,000. The EULA counts revenue *from all sources,
  including indirect revenue* for individuals. Capture-pack sales that are promoted through the
  free plugin may well count. Options when that threshold approaches: JUCE Indie
  (USD 40/month or USD 800 perpetual, up to USD 300k), or release MONSTROSITY's source under
  AGPLv3 (then no fee), or switch the thin plugin layer to iPlug2. **Not blocking now.** Get
  your own legal advice before release.
- **NeuralAmpModelerCore, AudioDSPTools, nlohmann/json, VST3 SDK**: MIT — keep their copyright
  notices in the installer/About box. **Eigen**: MPL-2.0 — notice + where to get the source.
- **Third-party `.nam` files**: each creator sets their own licence; TONE3000 has its own
  terms. MONSTROSITY never copies captures into presets or plugin state (it stores the file
  path) and the repository never contains third-party captures.
- **Name**: "Neural Amp Modeler"/"NAM" may be used descriptively ("loads NAM captures"), but
  should not be used as if it were MONSTROSITY's own brand.

## 4. Decisions (answered by the owner 2026-10-01)

1. **Loudness normalisation ON by default — accepted, with the condition "no digital clipping
   is acceptable".** Implemented as an always-on output **safety limiter** (see
   `core/include/monstrosity/SafetyLimiter.h`):
   - Output can never exceed **-1 dBFS** (sample peak). Proven by `tools/limiter_test` and by
     the real VST3 with Input +24 dB and Output +12 dB on several captures.
   - It turns the level down smoothly instead of clipping (a sine 12 dB over the ceiling comes
     out with 0.03 % distortion vs 35 % if it were clipped). Below -1 dBFS it is bit-transparent.
   - Cost: 1 ms lookahead latency (48 samples at 48 kHz), reported to the DAW.
   - The -1 dB margin also covers inter-sample ("true") peaks for practically all guitar material.
   - Limits of the guarantee: MONSTROSITY cannot undo clipping that already happened before it,
     i.e. in the audio interface's converter (keep the Studio 26c input LED out of the red) or
     in later plugins/master bus in the DAW.
   - Mixing note for Milestone 2: percentages that total 100 % form a weighted average, so the
     mix itself can never peak higher than the loudest individual capture.
2. **GitHub — existing account, new repository.** No separate GitHub user is needed: a
   repository is its own isolated space. Pending: owner creates the empty repo and connects it.
3. **Licence — owner is willing to go open source.** Open source is *not strictly required* while
   total annual revenue (incl. capture packs) stays ≤ USD 20,000 under the JUCE Starter licence.
   Recommended: publish MONSTROSITY's own code under **AGPLv3** (the licence JUCE offers for open
   source use), which removes the revenue limit entirely at no cost. The name, logo, artwork and
   all capture packs stay the owner's property and are not covered by the code licence.
   Pending owner confirmation before a LICENSE file is added.

## 5. Mixing law (to be tested in Milestone 2, not assumed)

Working hypothesis: user sees percentages that always total 100 %, applied to
loudness-normalised captures, followed by the master output level. For well-aligned captures
the output level then stays roughly constant regardless of how many are active; for poorly
correlated captures the level drops (up to ~7 dB with five equal captures). Milestone 2 will
measure both cases with real captures and only then fix the law.
