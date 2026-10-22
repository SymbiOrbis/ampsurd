# DOUBLER (experimental module)

Double tracking from ONE guitar input: AMPSURD adds 1-3 "second takes" of what you play, left and right,
like a rhythm guitar recorded two or four times. Status: **EXPERIMENTAL**, off by default, may be changed or
removed. Added 2026-10-22 after a tester's suggestion ("a doubler in the style of MIMIQ").

Own code. Inspiration: the tester's REAPER script "Performance Doubler 2.2" (ideas only, no code taken) and
public descriptions of TC Electronic's MIMIQ doubler.

## What makes a double sound real (and what this module does about it)

| A real second take... | DOUBLER |
|---|---|
| is a little late or early, and the offset changes from phrase to phrase | each take plays 8-33 ms later (TIGHT); a new offset is drawn in every pause long enough that nothing audible is being read |
| is never exactly in tune with the first one | the take's delay moves slowly and smoothly: ~0.5 cents (sustained notes, nearly still) to ~2 cents (while picking), at most ~6 cents |
| is not an exact copy | four all-passes at random frequencies per take: the same tone, but no regular comb with your guitar (an exact copy with a moving delay = a flanger) |
| has its own intonation on every note | at every pick attack (onset detector), each take gets its own small pitch offset (relaxes in ~0.3 s), applied when that take plays the attack |
| is picked harder or softer | per-note level +-1.5 dB (CHARACTER) |
| sounds slightly different (pick position, tone) | per take: high shelf +-2.5 dB and a short comb (0.5-2 ms) = a different pick position (CHARACTER) |
| has the same tight low end | LOW MONO: below 120 Hz (adjustable) only your guitar plays, centred |

Rules taken over as ideas: nothing jumps while sound is read (jumps only in silences, with a 5 ms
crossfade); drift is slower in sustained notes, where moving combs would sound like phasing.

2026-10-22 owner: "sounds a bit like a flanger" -> wander halved (+-0.3..2 ms), drift 2x slower and nearly
still in sustained notes (10 %), decorrelation all-passes added.

What it can't do: a copy of one performance is still one performance. It is not as loose as two real
takes: timing varies per phrase, not per note. Summed to mono it combs a little more than real takes do.
The truly real double is a second recorded take: possible later in the player / recorder.

## Signal flow

    amps (blend / Frankenstein) -> ROOT -> noise gate -> DOUBLER -> flanger / delays / reverb -> output

- After the gate: the pauses are clean, so new takes are drawn there.
- Before delays and reverb: the echoes include the doubles.
- Source of the takes: the mid (L+R)/2 of the amp mix. The dry guitar is NOT delayed, so there is no latency.
- Layout: 1 double = your guitar left, take right (WIDTH). 2 = guitar centre, takes left and right.
  3 = guitar slightly left, takes hard left, hard right and right of centre.
- Loudness kept: the dry guitar's highs and the takes are scaled by 1/sqrt(1 + n * level^2), so switching
  it on does not make AMPSURD louder. With LOW MONO the lows are not touched.
- Off: faded out in 30 ms, then not processed at all (bit-identical).

## Controls

ON, DOUBLES (1 / 2 / 3), TIGHT (0-100 %: 100 = 8-13 ms, wander +-0.3 ms, 1 cent per note; 0 = 18-33 ms,
+-2 ms, 5 cents), LEVEL (each take vs. your guitar, -18..+6 dB, default -2), WIDTH (0-100 %), LOW MONO
(OFF, 20-300 Hz, default 120), CHARACTER (0-100 %). The panel shows where the guitar and each take sit
(left / right) and how late each take plays right now.

## CPU

Three takes: ~1 % of one core (16-tap windowed-sinc reads, measured by doubler_test). One A2 amp: 8-10 %.

## Tests

- `tools/doubler_test` (CI, Windows and macOS): off = bit-identical; pitch drift sustained / while picking;
  delay range; dry not delayed; attacks detected; new takes only in silences; no clicks; finite; loudness
  with 1-3 takes within 1 dB; mono sum no more than ~3 dB down; LOW MONO keeps a 50 Hz note centred and level;
  random settings and on / off while playing; CPU.
- `tests/ui_snapshot`: off by default (L = R with centred amps), on = stereo takes, off again = L = R;
  the panel opens from the master row and closes (`21_doubler.png`).
- **Not done yet:** listening tests with real guitars; mono check in a real mix.

## Removing it

1. Build without it: `-DAMPSURD_DOUBLER_MODULE=OFF`. Everything is behind `AMPSURD_DOUBLER`.
2. To delete it for good: `core/include/ampsurd/Doubler.h`, `core/src/Doubler.cpp`,
   `plugin/ui/DoublerPanel.h/.cpp`, `tools/doubler_test.cpp`, this file, the `#if AMPSURD_DOUBLER` blocks
   (PluginProcessor, PluginEditor, ui_snapshot), the DOUBLER button in MasterPanel, the option and the
   lines in the CMakeLists and the CI workflow.
3. Presets saved with it still load: their `dbl*` values are ignored.
