# AMPSURD — Development Log

(Project renamed from MONSTROSITY to AMPSURD on 2026-10-07; older entries were renamed too.)

Newest entry first. Each entry: implemented / files / tested / known issues / build / next.

---

## 2026-10-09 — Session 2l: recorder timeline, punch-in corrections — Claude (Opus 5.5)

Owner's spec: one guitar track; waveform of the take with the backing above it; zoom; navigate;
record while the backing plays; a correction's start can be grabbed and moved in any direction with
seamless, pop-free joins.

- PlayerRecorder: the guitar track is a list of clips (recorded passes). First REC = the take. With a
  take, every PLAY pass is recorded in the background; REC = punch in at the playhead (the old take
  goes quiet from there), REC again / STOP = punch out; un-punched passes are deleted. Clip edges
  (in / out) can be moved anywhere inside the recorded pass (so also before the punch-in point);
  removeClip, UNDO (50 steps), CLEAR TAKE. The clips are rendered in the background into one
  composite take (32-bit float) with 10 ms equal-power crossfades at every edge (centred on the edge
  when audio exists on both sides); playback / export / bounce use the composite. PAUSE keeps the
  pass (seamless continue); moving the playhead ends the pass. Positions are rescaled if the sample
  rate changes. STOP returns to where playback started; |< = start of the song.
- UI: new Timeline (ruler, BACKING lane, GUITAR lane from juce::AudioThumbnail, live waveform of the
  recording, playhead, numbered correction boxes with draggable edges + time readout, right-click =
  remove), zoom - / + / FIT, mouse wheel zoom around the mouse, Shift + wheel scroll, click = go
  there, view follows the playhead. UNDO, CLEAR TAKE (click twice), REC shows PUNCH OUT. The three
  columns became two compact rows (backing / volumes / offset; save options).
- Tests (`recorder_test`, all PASS): all earlier recorder tests (alignment exact to the sample,
  exports, pause, bounce + layers, no clipping, 44.1 kHz backing) + new: take 220 Hz, correction
  punched 1.0-2.0 s while playing 330 Hz -> 220 / 330 / 220 Hz heard; largest sample step 0.0102
  (smooth crossfade; a hard cut would be ~0.4); start edge moved 0.3 s earlier (before the punch-in)
  -> correction heard from 0.7 s; moved later -> original until 1.3 s; UNDO twice / three times
  restores the previous edge / the original take; PLAY without REC leaves nothing behind. Screenshot
  of the panel rendered from the test (RECORDER_SNAPSHOT=dir).
- Found by Windows CI (intermittent click in the composite): JUCE's ThreadedWriter drops audio when its
  buffer is full, which happens when processing runs much faster than real time (tests, offline
  renders). Offline processing now waits for the disk; real time keeps the 2.7 s write-ahead. CI now
  turns failing test lines into annotations (readable without downloading the log). Two CI runs green.

## 2026-10-08 — Session 2k: CPU — silent amps sleep, several cores — Claude (Opus 5.5)

Owner: "the more CPU is used, the more clicking"; crackles disappear after restarting the app.
Long runs here (3 captures + Frankenstein + editor redrawing, 6 min) show no growth of the processing
time, so the build-up is probably outside the engine (power plan / CPU clock / core scheduling) -
asked the owner to watch the CPU and DROPOUTS readouts. Independently, two CPU reductions:

- Silent amps sleep (core `Engine`): a path that cannot be heard (muted, 0 % fader, not in the
  Frankenstein layout, empty slot) stops being processed 0.5 s after its fade-out. When it is needed
  again it runs 100 ms unheard (fresh internal state) and then fades in like a newly loaded capture.
  Tests: CPU freed ~1 s after MUTE; 1.5 s after unmute the output is identical to an amp that never
  slept (-300 dB); fade-in without a click (curvature below the no-sleep reference).
- Several cores (core `ParallelRunner`, new): the amp paths (capture -> IR -> IR EQ -> align -> EQ)
  of one block run on worker threads at the same time; the audio thread works too and only waits for
  jobs a worker already started; generation-tagged job claims; workers spin 0.3 ms then sleep on an
  atomic wait; MMCSS "Pro Audio" + time-critical priority on Windows, denormals flushed. Automatic
  only on computers with >= 4 logical cores (cores - 1 workers, max 4); settings menu "Use several
  CPU cores" (per computer). Tests: 3 amps on 4 workers bit-identical to one core; ThreadSanitizer
  clean (engine_test). Measured here (2 vCPU VM, 1 worker): mean block time 900 -> 590 us with three
  captures; on 2 cores the worker is sometimes paused by the system (rare 30 ms waits), hence the
  4-core minimum. Real gain on the owner's PC still to be confirmed.

## 2026-10-08 — Session 2j: tone check, input calibration, clicks, Frankenstein NOTES — Claude (Opus 5.5)

Response to the user's test report (clicks with one capture, tone "duller / less gain / less clarity"
than the TONE3000 app with the same .nam, Frankenstein not changing the sound, divider 3/4 stuck,
blinking spot, unreadable CPU meter).

- Tone, measured (`tests/tone_test.cpp`, new): the full AMPSURD processor with one capture (gate and
  level match off) vs the same capture run directly through NeuralAmpModelerCore: 48 kHz bit-identical
  (difference -300 dB, every octave band +0.00 dB); 44.1 kHz (resampled) within 0.02 dB 88 Hz-11 kHz.
  So AMPSURD's own processing does not change the tone; the difference must be the drive level, the
  loudness or processing in the other app.
- Official-plugin parity: the official NAM plugin switches NAM Core to its fast tanh approximation at
  start-up; AMPSURD used the exact tanh (difference ~-43 dB on tanh WaveNets, none on A2). AMPSURD now
  does the same as the official plugin (`CaptureModel::setFastTanh`, default on). `ampsurd_render
  --exact-tanh` keeps the bit-exact compatibility test against NAM Core's `render` (all 9 example
  models PASS, <= -124 dB).
- Input calibration (new, like the NAM plugin's "Calibrate input"): settings menu -> "Calibrate input
  to each capture" + "My interface's input level" (dBu, default +12 = NAM default). Each capture that
  stores `input_level_dbu` gets input trim = interface - capture (limited +-24 dB, smoothed 20 ms,
  exactly 1.0 when off = bit-identical). Level match / alignment are measured at the calibrated drive
  (re-measured when the setting changes). Stored per computer (AMPSURD.settings), not in presets.
  Slot tooltip shows the capture's recording input level.
- Clicks: `tests/soak_test.cpp` (new) runs the real processor for many simulated minutes (one capture,
  with IR, with all effects; UI-thread spectrum/tuner polling in parallel): mean callback time flat
  (270-350 us of 2667 us at 128 samples), no growth over time. New footer readout "DROPOUTS n" (blocks
  AMPSURD finished too late + the audio driver's missed buffers in the standalone app) so a click can
  be identified as a dropout. Gate: second smoothing stage (2 ms) so closing / re-opening on a
  sustaining note has no corner (new test PASS); a new pick attack still opens in 0.98 ms.
- Frankenstein: NOTES mode (split the guitar BEFORE the amps: each note range plays through its own
  amp - what the user expected) next to TONE (split the output spectrum, as before); TONE/NOTES
  buttons in the header; narrow band under the graph showing the played note's position (from the
  tuner) on the frequency axis + live input spectrum; divider drag limited only by visible neighbours
  (fixes 3/4 stuck); repaint only on change.
- CPU meter averaged and updated once per second.
- Tests: all core tests PASS (limiter, gate/tuner, frankenstein incl. NOTES, ir, fx, engine incl. NOTES
  loudness), compat test PASS, tone_test PASS (also with calibration), soak stable.

## 2026-10-08 — Session 2i: standalone app + player / recorder — Claude (Opus 5.5)

- Standalone Windows app (`plugin/standalone/StandaloneApp.cpp`, JUCE standalone wrapper with
  AMPSURD defaults: guitar input NOT muted on first start; device latency handed to the recorder).
  CI builds it and uploads AMPSURD-Standalone-windows-x64.
- `PlayerRecorder` (standalone only; created when wrapperType == Standalone): backing track (WAV,
  FLAC, MP3, OGG, AIFF; any rate), guitar take = AMPSURD's final output recorded to a 32-bit float
  WAV (Documents/AMPSURD/Recordings/Takes), PLAY / PAUSE (also while recording: one seamless take) /
  STOP / REC (replaces the take, click twice) / |<. Live guitar always audible. Sum limiter (stereo,
  -1 dBFS) so backing + guitar never clip (+1 ms monitoring latency in the app only). Takes are
  shifted by AMPSURD latency + device input/output latency + 1 ms + OFFSET, so they land where they
  were played. Export guitar alone (aligned to the backing's start) or mixed: WAV 16/24/32f, FLAC
  16/24, 44.1/48/96 kHz (default WAV 16-bit 44.1 kHz), TPDF dither, limited. BOUNCE: backing + take
  -> new backing (32-bit float, session rate) for layering.
- Playback chain per file: reader -> BufferingAudioSource (read-ahead thread) -> ResamplingAudioSource.
  Found in testing: JUCE's read-ahead plays silence when it is late; PLAY / seek now wait until the
  first second is buffered, and offline rendering (isNonRealtime) waits for the disk.
- core `StreamResampler` (polyphase sinc, chunk-independent, bit-identical in any chunk size).
- UI: PLAYER / REC button in the header (blinks "REC m:ss" while recording, also with the panel
  closed), player panel (backing / guitar recording / save columns, meters, typed values), dark
  styled drop-down boxes (also fixes the FX note box).
- Tested: `recorder_test` (new, in CI) through the real processor with a simulated 300-sample
  interface: backing clicks exactly where expected; a "musician" playing in time with what is heard
  -> every recorded note exactly on its click (7/7 samples); playback = click + note; all five export
  formats at 44.1/48/96 kHz readable, right length, first note at 0.5000 s, peak <= -1 dBFS; pause /
  resume -> one take; BOUNCE + second layer -> click + both takes at 0.5 s; full-scale backing +6 dB +
  loud guitar -> -1.00 dBFS; 44.1 kHz backing in a 48 kHz session exact (live and export).
  `fx_test`: resampler level exact, residual -114 dB, aliasing -103 dB, chunk-independent.
  Standalone app started on a virtual display: runs, PLAYER / REC present. VST3 regression unchanged.
- Not testable here: real audio interfaces (driver-reported latency accuracy -> OFFSET), MP3 decoding
  (no MP3 encoder in this environment to make a test file).

---

## 2026-10-08 — Session 2h: effects (2 delays, reverb, flanger) — Claude (Opus 5.5)

- Owner's decisions: two parallel delays with exact times (e.g. 500 and 756 ms), optional SYNC;
  reverb types Room, Hall, Plate, Cathedral, Ambience (no Spring); flanger; all in presets; panel =
  Global EQ panel extended. Player / recorder: standalone app only (next), live guitar audible while
  the recording plays back, one-button BOUNCE (backing + recording become the new backing track).
- core `Effects`: `Delay` (Hermite-interpolated line, exact time incl. fractions, a time change
  crossfades old -> new time over 50 ms, repeats filtered (TONE high cut + 80 Hz low cut), MONO/stereo
  or PING-PONG, spill-over: OFF stops feeding, repeats ring out, then idle). `Reverb` (8-line FDN,
  Householder matrix, 4 input allpasses per side, modulated lines, in-loop damping, early
  reflections with separate L/R times, decay gains from RT60, pre-delay changes crossfade, type change
  ducks 15 ms and fades the guitar back in). `Flanger` (stereo, LFOs 90 deg apart, feedback fades with
  the effect, line kept filled while off). S-shaped fades everywhere. Order: gate -> flanger -> delays
  (parallel) -> reverb -> OUTPUT -> limiter.
- plugin: 28 parameters (429 total) incl. TEMPO (used when the host has no tempo; TAP in the UI);
  SYNC uses the host tempo in a DAW. Values can be typed (e.g. "756", "7.5k").
- UI: GLOBAL EQ / FX panel = EQ left, effects right (selector DELAY 1 / DELAY 2 / REVERB / FLANGER, lit
  while on). Vertical button reads e.g. "EQ / FX   EQ + DLY + REV".
- Found during testing: linear fade corners and a cold flanger line produced small clicks (up to
  -77 dBFS above 4 kHz); S-shaped fades, warm line and crossfaded pre-delay brought every change down
  to the level of steady playing. A test-print bug (argument evaluation order) was fixed too.
- Tested: `fx_test` (new, in CI): all off bit-identical; delay echoes at exactly sample 24000 / 36288
  (500 / 756 ms), 756.3 ms between samples; feedback ratio 0.495; ping-pong sides; repeats ring out
  after OFF, then idle and bit-transparent; RT60 measured within 12 % of DECAY for all five types
  (1 s and 3 s); stereo correlation -0.00; cathedral 12 s stable; all changes while playing click-free
  (content above 4 kHz within 3 dB of steady playing); CPU all effects 2.4-4 % of a core. Sanitizers
  (ASan/UBSan) clean on effects and IR code. VST3 with everything at maximum feedback, OUTPUT +12 dB:
  -1.00 dBFS. Regression unchanged; presets 429/429.

---

## 2026-10-08 — Session 2g: cabinet IR per slot, IR EQ, slot redesign — Claude (Opus 5.5)

- Owner's decisions (2026-10-08): IR per slot for amp-only captures; IR EQ = the same 10-band EQ
  (low cut, 8 bells, high cut), only for that IR, saved with presets; REMOVE empties the whole slot
  (NAM + IR), click twice, also kept in EDIT; square-ish slots, bold capture name, bigger numbers,
  six buttons; IR editor as a fourth view of the lower panel; muted slots keep running.
- core: `Convolver` — zero-latency non-uniformly partitioned convolution (64-tap direct head,
  64-sample FFT partitions up to 4096, 2048-sample partitions beyond with all work - FFT passes,
  products, inverse FFT passes - spread evenly over the following block). `prepareImpulseResponse`
  (leading silence trimmed, Kaiser-sinc resampling to the session rate, max 1 s with fade-out, unit
  energy). `IrSlot` (lock-free hand-over like CaptureSlot; new / re-enabled IRs warm up silently for
  up to 2048 samples, then 20 ms crossfade; bypassed or without IR the convolver does not run).
  Engine: capture → IR → IR EQ → alignment → EQ → … (IR EQ neutralised unless IR loaded and on).
- plugin: per slot `s1_irOn`, `s1_ir_eqOn`, `s1_ir_b1..10_freq/gain/q` (401 parameters). IR loaded on
  the loader thread (WAV/AIFF/FLAC, first channel), info string; `irPath` stored per slot in presets
  (sibling lookup, missing-file state). Measurement now = capture + IR; capture renders are cached
  so an IR change / bypass re-measures that slot in a fraction of a second. Sample-rate change
  rebuilds the IRs. REMOVE (unloadCapture) also removes the IR.
- UI: slots 300 px (was 340; lower panel taller): number 20 px semibold, bold capture name, quiet
  "+ IR name" line, PAN row, buttons LOAD NAM | EDIT | ADD IR / SOLO | MUTE | REMOVE. IR button:
  ADD IR / IR (lit) / IR OFF / IR ?; filled while the IR editor is open. Audio files dropped on a
  slot load as its IR. IR editor: left = IR name, file info, LOAD/REPLACE IR, IR ON, REMOVE IR
  (drop target); right = IR EQ (EQ ON / FLAT), dimmed while there is no active IR.
- Tested: `ir_test` (new, in CI) — convolver = direct convolution for 1..48000-sample IRs with
  random blocks (error 1e-14), zero latency, 1 s IR ≈ 2 % of a core with no block typically above
  ~4 % of its time; resampled IR keeps its response within 0.04 dB; slot: no IR / bypassed =
  bit-identical and idle, IR on = exact convolution, load / replace / bypass / remove while playing
  click-free. `engine_test`: slot = capture through its IR (6e-16), IR EQ after the IR exact, IR EQ
  without IR and bypassed IR bit-identical. VST3: level match with IR -17.84 vs -17.93 dB without
  (target -18; without level match this IR would be +5.8 dB). Presets 401/401 + 5/5 IRs; older
  presets load without IR. Regression unchanged (bit-identical, -1.00 dBFS, bypass identical).

---

## 2026-10-08 — Session 2f: per-amp PAN (stereo output) — Claude (Opus 5.5)

- Engine: stereo output. PAN per amp, constant power with the centre at exactly 0 dB on both sides
  (L = √2·cos θ, R = √2·sin θ; centre forced to exactly 1/1). Separate smoothed L/R gains (25 ms).
- Mix law with PAN: G uses the loudness of both channels together (BS.1770: sum of channel powers),
  i.e. C_ij weighted by (L_i·L_j + R_i·R_j)/2; all centred = the previous formula exactly.
- Frankenstein: shared band split, per-channel phase compensation (both channels reconstruct).
- Global EQ on both channels; gate applies the same gain to both; SafetyLimiter stereo-linked
  (gain from the louder channel, applied to both, so the image never shifts while limiting).
- plugin: `s1..5_pan` (−100..+100, shown C / L35 / R20; 241 parameters); stereo out bus written
  L/R; mono out bus = (L+R)/2. UI: PAN row under each filename (double-click = centre).
- Tested: `engine_test` — pan law constant power (4e-16); centred L and R bit-identical to the mono
  output; stereo loudness on target for 5 pan settings (worst 0.06 dB); hard L/R: left = amp A only,
  right = amp B only (rest −280 dB); Frankenstein + pan OK. `limiter_test` stereo: never above
  −1 dBFS, L/R gain identical (2e-16). VST3 in host: centred → L == R and bit-identical to the NAM
  reference; one capture hard left → L exactly +3.01 dB (×1.414214), R = 0; five panned captures at
  +12 dB → −1.00 dBFS. Preset round trip 241/241.
- Next: release package (signing, installer, standalone app with update check).

---

## 2026-10-08 — Session 2e: Global EQ — Claude (Opus 5.5)

- Owner's spec (2026-10-07): a Global EQ on the complete sound, reusing the amp EQ, opened from a
  vertical GLOBAL EQ button next to the gate + tuner; status visible while closed; the Global EQ
  curve shown subdued and non-editable behind an amp's EQ in EDIT; stored in presets.
- core: Engine step 5 = Global EQ (same `ParametricEq`) after the blend / Frankenstein; then gate,
  OUTPUT, limiter. `ParametricEq::neutralised()` (OFF glides to flat, cuts included) and `isFlat()`.
- plugin: 31 parameters `geq_eqOn` (default OFF) + `geq_b1..10_freq/gain/q` (236 total), group
  "Global EQ" for automation. EQ targets 0-4 = amps, 5 = Global EQ (same ID scheme, prefix `geq`).
- UI: `GlobalEqPanel` (GLOBAL EQ heading, EQ ON / FLAT / CLOSE, same graph); `GlobalEqButton`
  (vertical, right of the gate + tuner and of Frankenstein; lit background + filled dot + "ON",
  "ON (FLAT)" when on but flat, neutral + "OFF" when off). In amp EDIT: thin grey Global EQ curve,
  drawn only when on and not flat, no points, not clickable, with a one-line legend.
- Fixed while testing:
  - Amp EQ OFF switched off the bells but left the low/high cut active (since the cut bands were
    introduced). Now OFF bypasses all ten bands, gliding (no click).
  - A high cut leaving its 20 kHz end stop (EQ ON, or dragging it) started from an empty filter
    state - a one-sample dip, i.e. a small click. It now starts from the settled state.
  - Presets saved before a parameter existed left that parameter at whatever was loaded before;
    missing parameters now load at their defaults (old presets: Global EQ off/flat, Frankenstein off).
  - Race: a capture still being measured could finish after a newer preset replaced that slot and
    overwrite it (seen as "missing file" not shown). The load now checks it is still current.
- Tested: `engine_test` — Global EQ OFF (bands set) bit-identical; ON = blend through the verified
  EQ (difference 0.0); on/off while playing click-free; amp EQ OFF incl. cuts bit-identical to flat.
  ui_snapshot: preset round trip 236/236 + 5/5 slots, older preset → Global EQ off and flat,
  missing file OK; screenshots 09-12. Regression: VST3 single capture bit-identical to NAM, 5
  captures +12 dB → -1.00 dBFS, bypass identical; limiter/gate/tuner/Frankenstein ALL PASS.

---

## 2026-10-07 — Session 2d: "Create Frankenstein" (frequency-split blending) — Claude (Opus 5.5)

- core: `Frankenstein` — layout (2–5 sections, amp per section, dividers, WIDTH 0–90 %, muted /
  unloaded amps removed and neighbours meeting in the log-frequency middle, solo = full spectrum) and
  `FrankensteinMixer`: Linkwitz-Riley 24 dB/oct crossover tree (8 crossovers / 9 bands, two crossover
  points per divider; the band between them is shared with alpha = 0.5·(1 − 2^(−4h)) so both amps are
  exactly −6 dB at the divider for every WIDTH), shared all-pass phase compensation (perfect
  reconstruction), 25 ms glides of frequencies and weights, fixed-size arrays (no audio-thread allocation).
- Engine: applied after alignment / EQ / level match, mix law replaced by the spectrum split while
  active; 30 ms crossfade when switching on/off; percentages show each amp's share of the spectrum.
- plugin: 12 new parameters (frankOn, frankSections, frankWidth, frankAmp1–5, frankDiv1–4; 205 total).
- UI: CREATE FRANKENSTEIN button in the master row; centre area becomes a frequency map with section
  buttons 2–5, WIDTH slider, EXIT; hatched hand-over zones; draggable dividers (merged ones dashed);
  click a section to choose its amp; faders inactive while Frankenstein is on. Master row re-spaced
  (BYPASS was clipped), hint moved to the header.
- Tested: `frankenstein_test` ALL PASS — same amp everywhere → flat within 0.0000 dB; sections always
  add up flat; 1 kHz split −49 dB out of band, −6.0/−6.0 dB at the divider; WIDTH 90 % keeps amp A at
  −12.3 dB one octave above (−24.8 dB at 0 %); mute → neighbours meet at 632 Hz; solo flat; 2000 blocks
  of random changes bounded. `engine_test` with real captures: on/off while playing clean, loudness
  −0.08 dB. Regression: VST3 single capture bit-identical to the NAM reference, 5 captures +12 dB →
  −1.00 dBFS, bypass sample-identical, limiter/gate/tuner ALL PASS. Screenshots 07/08.
- CI now also builds and runs `frankenstein_test`.
- Next: per-amp PAN (stereo output), then the release package.

---

## 2026-10-07 — Session 2c: noise gate + tuner — Claude (Opus 5.5)

- core: `NoiseGate` (key = DI after INPUT gain, applied after the mix; 4 ms RMS detector, 4 dB
  hysteresis, 15 ms hold, 0.2 ms opening, exponential decay to -60 dB in DECAY), `PitchDetector`
  (YIN on a lock-free ring buffer filled by the audio thread, analysed on the UI timer; decimated by
  2 with full-rate refinement for high notes).
- plugin: params gateOn / gateThreshold (-70 dB) / gateDecay (120 ms) / tunerMute (193 total);
  mute only applies while the tuner is visible; tuner fed before BYPASS.
- UI: centre area = NOISE GATE (level meter with draggable threshold marker, OPEN/CLOSING/CLOSED
  text) + TUNER (note, octave, cents scale, IN TUNE, Hz); EDIT replaces it, CLOSE button added;
  GATE button in master row.
- Found and fixed during testing: peak detector opened on -80 dB hiss at the -70 dB threshold
  (now RMS); first RMS version held the gate open 420 ms (now symmetric 4 ms); opening sped up so
  the gate is fully open before the amp signal arrives; a duplicate gate update had been inserted
  into the covariance code (message thread) - removed.
- Tested: gate_tuner_test ALL PASS; VST3: gate off → bit-identical to NAM reference; 5 captures
  +12 dB → -1 dBFS; bypass sample-identical; engine/limiter/compat ALL PASS; preset round trip 193/193.
- Roadmap order changed by owner: Frankenstein before pan, release package last.

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
