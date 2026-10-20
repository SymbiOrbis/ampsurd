# ROOT (experimental module)

Makes the lowest note of a distorted, low-tuned power chord sound deeper, denser and more dominant -
without a pitch shifter, octave tracking or FFT. Idea: a tester's brief (2026-10-20, "Fundamental
Extractor / Low-End Root Enhancer"). Status: **EXPERIMENTAL**, off by default, may be changed or removed.

## What the research says (October 2026)

1. **Power-chord physics.** A distorted root + fifth (f, 1.5 f) produces a difference tone at 0.5 f,
   one octave below the root, and fills in the harmonic series of that low tone - the reason a
   distorted power chord sounds "bigger" than its notes. Mostly even-order (asymmetric) nonlinearities
   make it; symmetric clipping makes odd harmonics and only a weak 0.5 f.
   ([Distorted Sounds: Unlocking the Physics of Modern Music, arXiv 2504.04919, 2025](https://arxiv.org/pdf/2504.04919);
   [ampbooks: intermodulation in guitar amps](https://ampbooks.com/mobile/tutorials/imd/))
   Our own measurement (tools/root_test): with GROWL 100 % a drop-B power chord gets the octave-below
   tone at about -12 dB re the root; a major third gets it about 7 dB weaker but adds a tone TWO
   octaves down (0.25 f) - the known mud of distorted thirds; a single note gets none.
2. **Virtual bass research** (making low notes feel stronger with harmonics): time-domain nonlinear
   devices (NLD) suit transient-rich material (riffs); phase-vocoder methods suit steady tones but
   smear attacks and add latency; hybrids score best in listening tests but are not real-time.
   Recent work uses a smooth asymmetric curve (continuous first derivative) and a crossover around
   100-250 Hz. ([Moliner, Rämö, Välimäki, DAFx 2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf);
   [Giampiccolo et al., IEEE SPL 2023](https://re.public.polimi.it/bitstream/11311/1246177/1/GiampiccoloEtAl_SPLVBE2023.pdf))
   -> ROOT uses a time-domain, smooth, asymmetric curve on the low band only: real time, no latency.
3. **Metal mixing practice** for low-tuned guitars: tame the band below ~150-200 Hz with fast, heavy
   compression on that band only (palm-mute "woof"); bass is split into a clean low layer and a
   distorted upper layer around 150-200 Hz; a sub layer at 40-60 Hz is sometimes added.
   ([Nail The Mix: low-tuned guitars](https://nailthemix.com/how-to-mix-low-tuned-guitars))
   -> ROOT is a clean, compressed low layer from the DI guitar, under the amps.
4. **True sub-octave** (dividing the frequency by two, as analogue octave dividers / subharmonic
   synths do: flip-flop on zero crossings, low-pass, envelope) works on single notes, not reliably on
   chords. ([CCRMA: subharmonics](https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Subharmonics.html))
   -> only as the SUB mode, labelled experimental.

## How it works

    source ─ LP 24 dB/oct @ FREQ ─┬─────────────────── (1 - GROWL) ─┐
                                  └─ asymmetric curve ─ LP ─ GROWL ──┴─ TIGHT ─ BASS EQ ─ COMP ─ ROOT ─ (+) amps' output L/R
    SUB mode: + divider (zero crossings of the band, flip-flop, low-pass) x envelope

- Placed after the amps / Frankenstein / Global EQ mix point inside the engine output, **before the
  noise gate** (the gate silences the layer too), before the effects. Mono, centred.
- PARALLEL: source = the DI guitar (after INPUT and PITCH). POST: source = the amps' output.
  SUB: PARALLEL + the divider tone.
- GROWL keeps the layer's loudness (slow RMS match of the saturated vs. the clean band).
- No oversampling needed: the curve only sees the band below FREQ (<= 250 Hz) and a low-pass at
  2.5 x FREQ follows it, far below Nyquist. **No latency.**
- Off: faded out in 20 ms, then not processed at all - AMPSURD is bit-identical to a build without it.

Controls: ON, PARALLEL / POST / SUB, POLARITY, ROOT (layer level, -24..+12 dB), FREQ (60-250 Hz),
GROWL (0-100 %), TIGHT (20-100 Hz high-pass), COMP (0-100 %: threshold -12..-30 dBFS, ratio 1..8,
2 ms / 100 ms, auto make-up), BASS EQ (SUB 50 Hz shelf, LOW 100 Hz, LOW-MID 250 Hz, +-12 dB). The panel
shows the layer's curve (filters + EQ + level), its level and the compressor's gain reduction.

## Tests

- `tools/root_test` (CI, Windows and macOS): off = bit-identical; asymmetric curve; GROWL 0 = no new
  tones; GROWL 100 = octave below a power chord's root; third / single note behave as the physics says;
  SUB divides a single note; filters and BASS EQ; COMP evens a 20 dB jump to 3.5 dB; fade-out; stability
  at full-scale noise with every boost at maximum.
- `tests/recorder_test` (CI): through the real processor - off by default, layer on, silent when off.
- `tests/ui_snapshot`: panel opens from the master row and closes (`17_root.png`).
- **Not done yet:** listening tests with real low-tuned guitars, A/B at matched loudness.

## Removing it

1. Build without it: `-DAMPSURD_ROOT_MODULE=OFF` (top-level CMakeLists.txt option). Everything ROOT is
   behind `AMPSURD_ROOT`; the build without it is checked.
2. To delete it for good: remove `core/include/ampsurd/RootEnhancer.h`, `core/src/RootEnhancer.cpp`,
   `plugin/ui/RootPanel.h/.cpp`, `tools/root_test.cpp`, this file, the `#if AMPSURD_ROOT` blocks
   (PluginProcessor.h/.cpp, PluginEditor.h/.cpp, tests) and the ROOT button in MasterPanel, the option
   and the lines in core/CMakeLists.txt, plugin/CMakeLists.txt, tools/CMakeLists.txt and the CI workflow.
3. Presets saved with ROOT still load: their `root*` values are simply ignored.
