# AMPSURD IRs

AMPSURD's own 4x12 cabinet IRs (owner, 2026-10-28). They are **synthetic**: every IR is a minimum-phase
filter calculated from parametric EQ models - no recorded IR was used as input - so they are original
designs of the project.

- 3 speakers: V30, DV77, SWAMP (own parametric models)
- 3 microphones: SM57, MD421, R121 (own parametric models, about 1" from the grille)
- 16 mic positions: 25 mm (dust-cap edge) to 100 mm (on the cone) from the speaker's centre, every 5 mm
- = 144 IRs, 2048 samples, 48 kHz and 44.1 kHz in the owner's set "AMPSURD_IRs v6"

`make_irs.py` is the owner's generator of that set. AMPSURD does not ship the WAV files: `core/CabIrModel`
is an exact C++ port of the generator (tested against all 288 files by `tools/cab_ir_test`, largest
difference 6e-8 = 24-bit rounding), so the plugin calculates any IR - also between the grid points - at any
sample rate. In the plugin: IR -> AMPSURD IRs (a map: rows = speaker x mic, columns = position; click, drag,
hover to listen, SAVE IR, EXPORT ALL 144). Presets store the point as `ampsurd-ir:<row>,<mm>`.

Between grid points the model's magnitude responses (dB) are blended - rows: the two neighbouring
speaker / mic combinations; columns: the position model is continuous - then made minimum-phase like the
originals. `tests/data/ampsurd_irs` holds five of the original files for the CI test.
