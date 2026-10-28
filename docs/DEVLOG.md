# AMPSURD — Development Log

(Project renamed from MONSTROSITY to AMPSURD on 2026-10-07; older entries were renamed too.)

Newest entry first. Each entry: implemented / files / tested / known issues / build / next.

---

## 2026-10-28 — Session 3z: tester fixes, AMPSURD IRs, pedals before the amps, EQ presets - Claude (Opus 5.5)

- Owner's list of the day (ROADMAP 3m-3q) + a Mac tester (M4 Pro, macOS Tahoe, Audient EVO8): pack import dialog
  could not be clicked; FRANKENSTEIN needed ~5 presses; "phasey" blend with several amps; latency "the same
  as my plugin". Owner: implement everything, no significant latency on any modern PC / Mac.
- FRANKENSTEIN: the button switched Frankenstein on / off while EDIT, IR, the player, ROOT, DOUBLER or GLOBAL
  covered it (so nothing seemed to happen). Now: on + shown -> off, otherwise on AND shown
  (AmpsurdEditor::frankensteinPressed). ui_snapshot FRANKENSTEIN button test.
- macOS file dialogs: pack import / export and "Load NAM..." from a menu open 150 ms after the menu has closed
  (a dialog opened inside the menu's callback can stay unclickable on macOS). Not reproducible here (Linux);
  the tester should confirm.
- Phasey blend: automatic alignment checked at 44.1 / 48 / 96 kHz (same offset and polarity within 0.014 ms,
  ui_snapshot ALIGN AT ANY RATE). Nothing wrong found; different mics / cabinets cannot be aligned by a delay
  (REVIEW 2.3). Asked for the tester's captures / an EDIT screenshot.
- Full A2 by default: "Load A2 captures as Lite (ECO)" off unless the user switched it on before.
- EQ COPY: one paste per copy (all buttons back to COPY); CANCEL COPY on the copied EQ (button 100 px), Ctrl+Z
  (undo takes back the copy first), Esc, or 20 s. ui_snapshot EQ COPY test extended.
- EQ PRESETS (button in every EQ graph): factory curves embedded from plugin/assets/eq/*.ampsurdeq (empty until
  the owner chooses them), the user's own in Documents/AMPSURD/EQ, "Save this EQ as..." (name), load = one undo
  step. ui_snapshot EQ PRESETS test.
- AMPSURD IRs: the owner's synthetic set (AMPSURD_IRs v6, make_irs.py) ported exactly to core/CabIrModel
  (cab_ir_test: all 288 files, largest difference 6e-8; 5 reference files in tests/data). Any point between the
  grid (rows blended in dB, position continuous), any sample rate, ~7 ms per IR. CabIrMatrix overlay (IR ->
  AMPSURD IRs): rows speaker x mic, columns 25-100 mm, click / drag / HOVER TO LISTEN (no undo steps: the
  processor's irPreview flag) / SNAP TO GRID, SAVE IR (= the owner's file to 1 LSB), EXPORT ALL 144. Presets
  store "ampsurd-ir:<row>,<mm>", packs need no file. loadCabIr() + adoptIr() (shared with file IRs).
- Pedals before the amps (serial chain step 1): Engine pedals[slot][2] (CaptureSlots before the amp: crossfaded
  ON / bypass, smoothed LEVEL, a bypassed pedal does not run = bit-identical), params sN_ped1On/Level,
  sN_ped2On/Level, loadPedal / removePedal, the amp measured through its pedals (load, re-measure, INPUT curve via
  a new `pre` hook in CaptureAnalyzer::driveCurve), input calibration into the first active pedal, latency incl.
  pedal resampling, presets (pedal1Path / pedal2Path), packs, undo, REMOVE. UI: EDIT row "BEFORE THE AMP"
  (PEDAL 1 -> PEDAL 2 -> AMP: name / load / drop, ON, LEVEL), slot line "name ->". engine_test (bypass
  bit-identical, on changes the sound, no click when switched on), ui_snapshot PEDALS test.
- Latency (ui_snapshot LATENCY): AMPSURD adds 2.2 ms at 48 kHz (8-sample aligner + 1 ms FREE headroom + 1 ms
  limiter look-ahead), 2.8 ms at 44.1 kHz (resampling to the captures' 48 kHz), 2.5 ms at 96 kHz; +5 ms with
  PITCH. CPU (ampsurd_bench here, a slow cloud core): one Full A2 ~9 % of a core, five ~50 %; AMPSURD spreads
  the amps over several cores - 64-128 sample buffers are realistic on a modern PC / Mac.
- Also: .gitignore let tests/data/ampsurd_irs/*.wav through (CI cab_ir_test had found 0 files).
- Tests here: ui_snapshot with / without AMPSURD_FORCE_PLAYER (all PASS), recorder_test, tone_test, engine_test,
  cab_ir_test. Not testable here: macOS dialogs, real listening (IR map, pedals).

## 2026-10-24 — Session 3y: release 0.9.2 (Windows + macOS) - Claude (Opus 5.5)

- Owner: publish the installers for Mac and Windows (VIDEO recording still has problems - details pending).
- Version 0.9.2 (CMakeLists, examples in AMPSURD.iss / build_pkg.sh); RELEASE_NOTES: a 0.9.2 section with
  everything since 0.9.1 (VIDEO marked as having known recording problems; first macOS release); README:
  macOS .pkg available now. Release made by the workflow (workflow_dispatch publish_release on main): normal
  release "AMPSURD 0.9.2 (beta)" with AMPSURD-Windows-Setup.exe, AMPSURD-Windows.zip, AMPSURD-macOS.pkg,
  AMPSURD-macOS.zip. Next: owner signs the Windows files (installer/sign_release.ps1 -Version 0.9.2).

## 2026-10-24 — Session 3x: video takes for spontaneous ideas - Claude (Opus 5.5)

- Owner: after a saved video take, REC asked REPLACE? (it only meant the take in the player - the MP4 is never
  overwritten) and it was unclear whether saved videos can be watched in the window; the feature is for
  spontaneous ideas only, so SAVE MP4 can go, but a notice after recording that the video is saved is wanted.
- PlayerPanel: remembers which take's camera file was saved (savedTakeVideo); REC on a saved video take starts a
  new take at once (status: where the previous one is). REPLACE? only for a take that would be lost (audio
  take, failed save).
- Camera window: SAVE MP4 removed (SAVE AS -> MP4 VIDEO in the player still exists); notice over the picture:
  "SAVING THE VIDEO..." -> green "VIDEO SAVED: <name>" for 6 s (red if it failed); WATCH box: "Last take (with
  AMPSURD's sound)" + the saved videos in Recordings, newest first (re-read after a save and every ~2 s); PLAY
  plays the chosen saved video with its own sound (the player stays stopped); SHOW FILE reveals the chosen
  video. Window 600 x 590.
- Tested (Linux, no camera): ui_snapshot with and without AMPSURD_FORCE_PLAYER - all PASS, new check CAMERA
  WINDOW (no SAVE button, WATCH = last take + the .mp4 files in Recordings: 0 and 1 file); 20_camera_window.png
  with the notice. recorder_test ALL PASS. Not testable here: recording and playing real videos (owner, Windows).

## 2026-10-24 — Session 3w: fixed download names, normal releases, website material — Claude (Opus 5.5)

- Owner: ampsurd.com downloads straight from GitHub. Release files now have the same names in every release:
  AMPSURD-Windows-Setup.exe (Inno OutputBaseFilename), AMPSURD-Windows.zip (VST3 + AMPSURD.exe),
  AMPSURD-macOS.pkg, AMPSURD-macOS.zip (app + VST3 + AU, signed and stapled). The release job creates normal
  releases (--latest, no --prerelease; "beta" stays in the title / notes), so
  https://github.com/SymbiOrbis/ampsurd/releases/latest/download/<name> always gives the newest.
- sign_release.ps1: downloads AMPSURD-Windows.zip (older releases: the two versioned zips), signs, builds
  AMPSURD-Windows-Setup.exe (an older tag's versioned installer name is renamed), uploads the fixed names and,
  for an old-style release, deletes its unsigned versioned files.
- Check for updates: unchanged logic (newest by version tag from /releases; asset names and the
  pre-release flag do not matter) - parsing moved to latestFromReleases(), ui_snapshot test with a normal
  release with fixed names, an older pre-release and a draft -> 0.9.2.
- Docs: README, BUILD_WINDOWS / BUILD_MACOS, CODE_SIGNING, RELEASE_SIGNING, RELEASE_NOTES.
- Note: v0.9.1 (published as a pre-release with versioned names) does not answer the /latest/download links;
  they work from the next release on (or if v0.9.1 is changed - asked the owner).
- Website material: tests/ui_snapshot has a web shots mode (AMPSURD_WEB_SHOTS=1 AMPSURD_FORCE_PLAYER=1,
  `ui_snapshot <dir> <a.nam> <b.nam> <c.nam>`): a demo rig (the three NAM Core example captures under honest
  names, a synthetic demo cabinet IR, mix 40/35/25, pans L30/C/R30, Global EQ, gate), the whole window and
  precise 2x crops of each feature (slots, GLOBAL, tuner, gate / output row, header, EDIT EQ, COPY / PASTE, IR,
  delay, reverb, pitch, Frankenstein, ROOT, DOUBLER, player with backing, take and metronome, preset menu drawn
  with AMPSURD's look). 18 PNGs in website/screenshots/, plain-language texts in website/FEATURES.md (checked
  against the code / tooltips). Camera window not shot (no camera here).

## 2026-10-24 — Session 3v: macOS signing + notarization in CI — Claude (Opus 5.5)

- Owner: Apple Developer account (Individual, Team 93DWMMFZ87, valid until 2027-10-24) and the repository
  secrets exist; sign and notarize the macOS build.
- CI (macos job): new step imports both .p12 (secrets trimmed of whitespace / CR LF, base64-decoded) into a
  temporary keychain (random password, partition list for codesign / productbuild), finds the identities
  "Developer ID Application/Installer ... (93DWMMFZ87)", writes the notary key (.p8) to the runner's temp
  folder; without the secrets (forks / PRs) a notice and the old unsigned build. A last step (always) deletes
  the keychain and the key files.
- installer/macos/build_pkg.sh: with AMPSURD_SIGN_APP the bundles are signed (inner code first, then the
  bundle; --options runtime --timestamp; the app with entitlements-app.plist: audio input + camera - the
  camera because VIDEO takes need it under the hardened runtime; Info.plist already has the microphone and
  camera texts), the .pkg with Developer ID Installer (--timestamp, pkgutil --check-signature); with the notary
  key: notarytool submit --wait (log printed on failure), staple the .pkg and the three bundles (fallback:
  notarize the bundles separately if a ticket is not found), verify codesign --verify --deep --strict,
  stapler validate (all), spctl -a -vv (app), spctl -a -t install -vv (.pkg) - any problem fails the job;
  only then the zips. The installer's welcome page says "Signed and notarized by Apple (Developer ID:
  Radovan Glovna)" or the old unsigned note. "macOS build: notarized / unsigned" in the log (notice).
- Docs: RELEASE_NOTES (macOS: signed and notarized, no "Open Anyway"), README, CODE_SIGNING, BUILD_MACOS.
- Still needed: a real-Mac test (audio interface, Logic / GarageBand).

## 2026-10-24 — Session 3u: ONE undo history (Ctrl+Z also undoes takes) — Claude (Opus 5.5)

- Owner (approved): Ctrl+Z must undo any step, also a take - no categories of actions; the player's own UNDO
  button is redundant (the header arrows and the shortcut do it) -> removed.
- PlayerRecorder history is now snapshots (backing file + VOL, clips, video) with undo AND redo; steps:
  recording / punch, edge move, deleting the guitar track, BOUNCE, deleting the backing (X / REMOVE),
  LOAD BACKING. onNewStep reports each step to the processor.
- AmpsurdProcessor keeps the order of all steps ('S' sound / rig, 'P' player): undo() / redo() (Ctrl+Z,
  Ctrl+Shift+Z / Ctrl+Y, the header arrows) take back / redo whatever was done last; a new step of either
  kind ends the redo branch of both. While recording the player step is not undone (nothing happens).
- Tooltips / status texts say Ctrl+Z; the backing VOL slider follows a restored value.
- ui_snapshot ONE HISTORY: sound, take deleted, sound -> Ctrl+Z x3 in reverse order, Ctrl+Y x3 forward;
  DELETE BOXES uses Ctrl+Z and checks the player has no UNDO button. recorder_test unchanged (BOUNCE / CLEAR
  undo through the player's history).
- CI caught a crash (tone_test, both OSes): the line connecting the player to the history was placed under
  an `if` without braces, so it ran without a player too (= the plugin in a DAW) and dereferenced null. Fixed
  (braces); my local runs had all used AMPSURD_FORCE_PLAYER=1. CI now reports every test's exit code and
  last lines as annotations (crashes without [FAIL] were invisible), the tools included.

## 2026-10-24 — Session 3t: player / video fixes after the owner's test — Claude (Opus 5.5)

- Owner: the recorder's mix does not play, the video does not start recording. His Recordings (folder
  access): the MP4 mux works (two 26 MB MP4s from last night); today's video attempts stopped after 0.15 s
  (5 KB / 77 KB camera files), and a 504 MB mix export (a 10-minute take) ran just before.
- Causes found and fixed:
  1. PLAY and REC were silently ignored while a SAVE AS / BOUNCE runs (busy). Now the status says so with the
     progress ("Still saving ... %").
  2. The automatic MP4 rendered the whole song's sound on the message thread (a long song froze the window);
     now rendered in the background thread together with the mux.
  3. Replacing a video take cleared the old take in the background; REC could then start as a PUNCH-IN into
     the old take. startVideoTake now waits for the cleared take (renderNow) and refuses while saving.
  4. Camera-window PLAY stopped when the video's isPlaying() lagged (Windows): now it ends at the video's last
     frame or when the player stops.
- recorder_test: CLEAR + REC = a new take (no punch-in); UNDO twice restores the old take.
- Open (asked the owner): one undo history for everything (Ctrl+Z also undoes takes).

## 2026-10-23 — Session 3s: SAVE AS can write a pack — Claude (Opus 5.5)

- Owner: SAVE AS should also offer .ampsurdpack (first asked for a small menu, then: just extend the file
  type of the SAVE AS dialog). The dialog's file types are now "*.ampsurd;*.ampsurdpack": a name ending in
  .ampsurdpack writes the pack (preset + used NAMs / IRs + README, the same as PRESET -> Export pack, with the
  licence note afterwards), anything else the preset (.ampsurd). PRESET -> Export / Import unchanged.
  Tooltip: "Save the preset, or a pack with its NAM captures and IRs". JUCE showed both patterns as ONE entry
  in the Windows "Save as type" list - owner: they must be selectable one or the other. New SaveDialog
  (own code): Windows = the system IFileSaveDialog with two entries ("AMPSURD preset (*.ampsurd)", "AMPSURD
  pack with NAMs and IRs (*.ampsurdpack)", the extension follows the type); macOS = NSSavePanel with a
  "Format" pop-up (the extension follows it); the chosen type decides the extension. Linux test builds keep
  JUCE's chooser. Compiled for Windows / macOS by CI only.
- ui_snapshot SAVE AS: both types offered; .ampsurdpack -> pack with 2 captures + preset + README;
  .ampsurd -> a plain preset.

## 2026-10-23 — Session 3r: the video gets its sound automatically; delete boxes next to the tracks — Claude (Opus 5.5)

- Owner: video and track are recorded, but separately - the video has no sound. Checked his Recordings
  (folder access granted): the camera WMV (WMV3 1280x720, 33 s, ~30 fps) and the take WAV exist, but no MP4
  was ever written. The WMV carries no frame rate (r_frame_rate 1000/1 = the ASF time base); the Windows mux
  passed that to the H.264 encoder, which rejects it. Fix: a frame rate outside 5..120 fps -> 30 fps (frames
  keep their own time stamps). And the MP4 WITH the sound is now made automatically when a video take ends
  (when the camera file is complete); SAVE MP4 makes another one (e.g. after changing SOUND OFFSET or the
  sound choice). The take stays a separate audio track as before.
- Delete boxes: right next to each track, centred on it (owner); labels back at the left.
- Not testable here (Media Foundation is Windows-only): the owner's test decides; a failure shows the exact
  error text in the camera window / player status.

## 2026-10-23 — Session 3q: delete boxes, MUTE, the owner's process rule — Claude (Opus 5.5)

- Owner rule: answer questions and ASK before implementing a proposed solution (PROJECT_STATUS). The
  DROPOUTS change (3p) was implemented without asking; the owner agreed afterwards.
- Player: CLEAR TAKE removed; a square X (DeleteBox) before BACKING and GUITAR, tracks moved right (label
  column 80 -> 104 px). X -> "Delete the ... track?" over the lane with DELETE (red) / CANCEL; guitar = clearTake
  (UNDO brings it back), backing = removeBacking. Disabled while recording / when empty.
- Master row: MUTE right of BYPASS - the whole output (incl. the player) to exact silence with a linear 20 ms
  fade, back the same way; red when on (LookAndFeel property redWhenOn); not a parameter, so never saved
  in a preset / project. Input / output blocks 240 px, BYPASS 80 px to make room.
- ui_snapshot: MUTE (0.34 -> 0 -> 0.33, not in the state), DELETE BOXES (2 boxes, no CLEAR TAKE, CANCEL keeps,
  DELETE removes, UNDO restores; 19b_player_delete.png).
- Open questions to the owner: X on a bounced backing = delete it (now) or go back to the backing before the
  bounce? Keep the REMOVE button next to LOAD BACKING (now the same as the X)?

## 2026-10-23 — Session 3p: DROPOUTS counter without the start-up one — Claude (Opus 5.5)

- Owner: why is there always 1 dropout, what does it mean, does it need to be shown? It counts audio blocks
  that were late (AMPSURD's own load measurer + the standalone driver's missed buffers) = audible clicks; it
  is shown only when > 0. The constant 1 is almost certainly the start: the driver / the first blocks after
  the audio starts (or after an amp is loaded) are often late once. Now not counted: the first 3 s after
  prepareToPlay and 2 s after an amp finished loading. Tooltip on the footer explains it (larger buffer, ECO,
  fewer amps). Not testable here (no real driver); owner to confirm it stays hidden.

## 2026-10-23 — Session 3o: UNDO takes back a BOUNCE — Claude (Opus 5.5)

- Owner: not clear how to delete a bounced track. Before: BOUNCE replaced the backing and cleared the undo
  history; only REMOVE (the whole backing, original song included) was left. Now the bounce remembers the
  backing file + VOL, the guitar track's clips, its undo history and the video; UNDO first undoes recordings
  made after the bounce, then the bounce itself (bounce files stay in Recordings). Tooltips / status say so.
  recorder_test UNDO BOUNCE: 1st UNDO = the 2nd take, 2nd UNDO = the bounce: backing 'clicks48' and the
  1st take back (0.700 = 0.5 + 0.2).

## 2026-10-23 — Session 3n: camera window - PLAY, saving without a dialog — Claude (Opus 5.5)

- Owner: saving the video does not work, the buttons do not react, the window only closes with X. Most
  likely cause: the save dialog (and the "Replace?" dialog) opened behind the camera window, which stays on
  top, and blocked the app. No dialogs any more: SAVE MP4 writes "AMPSURD video <date time>.mp4" straight
  into Documents / AMPSURD / Recordings (status: the name, or the error text to report), SHOW FILE reveals
  it; REC with a take there shows REPLACE? on the button (REC again within 3 s).
- Owner: play the video back before saving. PLAY in the camera window: juce::VideoComponent plays the
  take's camera file muted, the player plays the song from the matching position (song time = video time -
  lead, the same sync as the saved MP4, incl. SOUND OFFSET); ends with the video, STOP, REC or SAVE.
  Windows / macOS only (JUCE has no video playback on Linux).
- Not testable here (no camera / video playback in the Linux build): needs the owner's test on Windows.

## 2026-10-22 — Session 3m: DOUBLER less flanger, camera window in front — Claude (Opus 5.5)

- Owner: the DOUBLER sounds a bit like a flanger. Cause: an exact copy with a slowly moving delay summed with
  the guitar in each speaker = a moving comb = a flanger. Wander halved (+-0.3..2 ms), drift 2x slower
  (OU 2.5 s) and nearly still in sustained notes (10 % speed), plus four decorrelation all-passes per take
  (700 Hz - 7 kHz, random, drawn with the take): no regular comb any more. doubler_test: sustained drift
  0.46 cents sd (was 2.28), while picking 2.10 (was 3.84); limits tightened (sustained max < 4 cents).
- Owner: when saving the video, the camera window is behind AMPSURD and cannot be brought to the front;
  saving does not work. Camera window always on top of AMPSURD; the MP4 save dialog is opened with the
  camera window as its parent (in front of it). Asked the owner for the status text if saving still fails.

## 2026-10-22 — Session 3l: blend tone learned while playing — Claude (Opus 5.5)

- Owner (newest build): the 50:50 blend still loses highs. Checked the whole plugin path (ui_snapshot, new
  check BLEND TONE in the plugin): with the test captures the 50:50 blend is within 0.6 dB of its amps in
  every octave - the correction works as designed. What it cannot know: the amps' own EQs, the IR EQs and
  how the guitar is really played (it is computed from the test signal).
- New BlendToneTracker (core/BlendToneEq): while playing, every audible path (as it is mixed: after IR,
  EQ, alignment, level match) is split into the 10 bands (band-pass filters) and the per-band covariance of
  the paths is averaged (plain average for the first seconds, then ~6 s moving average; blocks below
  -60 dBFS ignored). The Engine computes the blend EQ from it like from the measured covariance; the
  learned share fades in over the first 4 s of playing. Relative to its own broadband G, so the loudness law
  is unchanged. A new capture or another alignment resets that slot (CaptureSlot swap counter), sleeping /
  waking amps keep what was learned. Only with band data (the plugin always passes it).
- EQ fitting: designs once per iteration; refit only when a band moves by > 0.1 dB.
- engine_test: amp B with its own EQ (-6 dB at 5 kHz, unknown to the measurement), 50:50 after ~12 s of
  playing: within 0.92 dB in every octave, 100 % learned. Older tests unchanged (PAN hard L/R bit-exact,
  SLEEP, MIX loudness 0.07 dB).

## 2026-10-22 — Session 3k: owner's notes on the last build — Claude (Opus 5.5)

- GUITAR PAN is now RECORDED (owner: stereo layering - click centre, take left, BOUNCE, take right). The pan
  is applied to the live guitar always (centre = untouched) and before the take is written; the take plays
  back, exports and bounces as recorded (no pan applied again). GUITAR VOL unchanged (monitors while
  recording + playback level, never the file). recorder_test: file hard left, playback with PAN centre still
  left only.
- Camera window: REC / STOP, SAVE MP4..., sound choice (synced with the player's GUITAR + BACKING /
  GUITAR ONLY), running time in the REC VIDEO tag, the player's status line at the bottom. REC there with a
  take present asks "Replace it?" (dialog) instead of the 3 s double press. A video take starts at the
  song's start. Window 600 x 560.
- Timeline scroll bar 16 -> 8 px (owner: half).
- A / B / COPY TO B explained (chat). Blend tone measured at 1/6 octave (blend_spectrum ... fine): before
  the correction -4.9 dB at 4.5 kHz, with it within +-1.6 dB (mostly +-0.5). Owner still hears fewer highs:
  asked which build / captures / IRs before a finer (continuous) correction curve.

## 2026-10-22 — Session 3j: DOUBLER (experimental double tracking) — Claude (Opus 5.5)

- Owner: a tester suggested a doubler like MIMIQ and sent his REAPER script ("Performance Doubler 2.2").
  Answered: one input is enough, ~1 % CPU. Owner: use it as inspiration, write our own code. Done: own
  implementation, ideas only (no code taken) - docs/DOUBLER.md.
- core Doubler: mid of the amp mix into a delay line; 1-3 takes, each read 8-33 ms later (TIGHT) with a
  16-tap Kaiser-windowed-sinc fractional read; the delay wanders smoothly (OU process -> tanh -> two 2 Hz
  smoothers; faster while picking, a quarter in sustained notes); per-note intonation (cents, relaxes in
  0.3 s) and pick strength at every detected attack, applied when the take plays it; per-take tone (high
  shelf +-2.5 dB, pick-position comb 0.5-2 ms); new timing / tone only in silences longer than any read
  point (5 ms crossfade); LOW MONO (LR4, lows only from the dry guitar); loudness kept (1/sqrt(1 + n g^2) on
  the highs); dry not delayed (no latency); layout 1 = dry L / take R, 2 = dry C / takes L+R, 3 = 4 takes.
  Off: 30 ms fade, then untouched.
- Processor: dbl* parameters (off by default), after the gate, before the effects. CMake option
  AMPSURD_DOUBLER_MODULE (default ON) -> AMPSURD_DOUBLER.
- UI: DoublerPanel (ON, DOUBLES 1/2/3, TIGHT, LEVEL, WIDTH, LOW MONO, CHARACTER, stereo picture with each
  take's current delay). Master row: DOUBLER button; input / output blocks narrower again (26 px each),
  buttons 6 px apart, FRANKENSTEIN 128 px, so BYPASS still fits.
- Tests: tools/doubler_test (14 checks, CI Windows + macOS): pitch drift 2.3 cents sd sustained (max 7.5),
  3.8 sd while picking (max 15.5); 26 / 26 attacks; 25 redraws in 26 pauses; no clicks; loudness within
  1 dB; mono sum >= -2.9 dB; LOW MONO 50 Hz +-0.1 dB; random settings stable; CPU 3 takes 1.1 % of a core.
  ui_snapshot DOUBLER (21_doubler.png).
- Not done: listening tests (owner, tester).

## 2026-10-22 — Session 3i: blend tone EQ (a blend keeps the tone of its amps) — Claude (Opus 5.5)

- Owner approved the fix for the darker 50:50 blend (Session 3h). New `core/BlendToneEq`: the mix law is
  applied once more in 10 frequency bands (octaves up to 2.4 kHz, half octaves above; the measured
  covariance is now also split into these bands by `CaptureAnalyzer::covariance`). Band b needs G_b / G on
  top of the broadband G so the blend has, in every band, the percentage-weighted average power of its amps.
  A static, smoothed (50 ms) EQ on the fader blend applies it: low shelf 150 Hz, 8 bells one band wide, high
  shelf 9.6 kHz (Simper SVF), filter gains fitted so the cascade hits the wanted value at each band centre;
  +-6 dB per band; bands with practically no energy are left alone.
- Like G it changes only with the mix / captures / alignment, never with the playing. One amp alone, an
  unmeasured amp, NOTES mode or Frankenstein TONE (which has its own band split): no EQ, bit-identical.
  Applied after the fader sum, before the Frankenstein crossfade and the Global EQ; same EQ on L and R.
- Measured (tools/blend_spectrum, octave bands 250 Hz - 8 kHz, blend vs. the average of its two amps):
  A2 + LSTM 50:50 worst -2.5 dB -> -0.6 dB (125 Hz +0.8); A2 + WaveNet -2.2 -> -0.5; LSTM + WaveNet
  +2.5 -> -1.1; A2 + LSTM 70:30 / 30:70 worst 1.1 / 2.7 -> 0.2 / 0.7 dB.
- engine_test (CI, both OSes): BLEND TONE one amp alone bit-identical with and without band data; 50:50
  A2 + WaveNet A1 within 0.43 dB in every band (2.14 dB without); the MIX loudness test now runs with the
  tone EQ and still matches the target within 0.06 dB.
- Not done: listening test by the owner with his own captures.

## 2026-10-22 — Session 3h: metronome HI-HAT, video diagnostics, blend tone measured — Claude (Opus 5.5)

- Owner: the click is quiet even at maximum. VOL range -40..+12 dB (the player limiter keeps the sum at
  -1 dBFS); click sound HI-HAT (white noise differentiated twice = +12 dB/octave, 22 / 35 ms decay, same
  noise every click) or BEEP; HI-HAT is the default of the player window (saved). recorder_test HI-HAT:
  ~35000 zero crossings/s, EVEN clicks identical, +12 dB louder and limited. (The old METRONOME test now sets
  BEEP itself: the player window opened by an earlier test applies its saved sound.)
- Owner: the video "probably was not recorded". Camera errors now reach the status line, and 2.5 s after
  STOP the camera file is checked (missing / empty -> a clear message, otherwise its size and how to save).
- Owner: a ~50:50 blend of two amps has fewer highs than either amp alone. tools/blend_spectrum measures it
  (level match + alignment + G as in the engine, octave bands vs. the power average of both): with the
  example models the 250 Hz - 1 kHz bands add coherently (+0.6..+0.8) and get +1.2..+2.1 dB, while
  4-8 kHz add incoherently or partly cancel (-0.4..+0.1) and end up -0.7..-2.5 dB: the blend is 2-4 dB
  darker in the highs relative to the mids. Cause: one broadband loudness compensation G for bands that add
  differently (different amps' highs are less correlated; tiny time offsets comb at high frequencies).
  Proposed fix (owner to decide): frequency-dependent compensation G(f) from the cross-spectra.

## 2026-10-22 — Session 3g: VIDEO takes — Claude (Opus 5.5)

- Owner: record the PC's camera too (with / without video), two saving options; one continuous take is
  fine; sound of the video = the same GUITAR + BACKING / GUITAR ONLY choice; camera in its own window.
- VideoCapture over juce::CameraDevice (JUCE_USE_CAMERA on Windows / macOS; Windows: DirectShow WMV at
  JUCE's fixed ~0.9 Mbit/s, up to 1280 x 720; macOS: AVFoundation .mov). CameraWindow (own LookAndFeel):
  camera list (last one remembered), preview, REC VIDEO tag, SOUND OFFSET +-500 ms (saved).
- Player: VIDEO button; with VIDEO on REC from STOP = a video take (a second REC within 3 s replaces an
  existing take - UNDO restores it), REC / STOP ends it; punch-in on a video take is refused; CLEAR TAKE
  drops the video. R key goes through the same logic. Header shows VIDEO TAKE.
- Sync: the audio thread stores the wall clock (epoch ms) and song position when the pass starts; with
  the camera's first-frame time (JUCE) and half the device latency + limiter as output latency this gives
  the timeline position of video frame 0; SOUND OFFSET adds to it.
- Save: format MP4 VIDEO -> renderMix (same as audio export) -> muxVideoToMp4: Windows Media Foundation
  (SourceReader NV12 -> SinkWriter H.264 ~6 bit/pixel/s + AAC 192k, 48 kHz via Lagrange resampling,
  interleaved), macOS AVMutableComposition + AVAssetExportSession (HighestQuality, MPEG-4).
- Camera permission text (Info.plist), PRIVACY.md. Tests here: sync arithmetic and take rules
  (recorder_test, CI on both OSes), camera window render. Not tested with a real camera (none here):
  the owner tests on Windows, a Mac tester on macOS.
- Owner: in EDIT the OUTPUT row did not fit any more (centre area 40 px lower since the gate strip).
  LEVEL heading removed, INPUT / OUTPUT moved up, alignment rows 4 px tighter; the loudness note fits too.
  IR editor, ROOT and Frankenstein checked in screenshots.

## 2026-10-22 — Session 3f: GUITAR track PAN / monitor volume; NaN robustness — Claude (Opus 5.5)

- Player: PAN of the GUITAR track (slider under the track name, timeline label column 80 px; same law as
  the amps' PAN). GUITAR VOL and PAN apply to the take on playback, bounce and export, and to the live
  guitar on the monitors while recording (incl. count-in and REC pause) - after the guitar was recorded,
  so the file is unchanged; smoothed, bit-transparent otherwise. recorder_test: -6 dB + hard left ->
  monitors L 0.142 / R 0, file 0.200 / 0.200, playback L 0.142 / R 0.
- Bug found through a "LIMIT -65.1" in a screenshot: one block of NaN / inf input (the test passed an
  uncleared buffer) poisoned all filter / amp states - AMPSURD stayed silent until restarted. Fixed:
  input sanitized (non-finite -> 0, clipped to +-64), and a NaN / inf found before the limiter makes
  the message thread prepare the chain again (suspendProcessing + prepareToPlay). ui_snapshot ROBUST and
  RECOVERY tests.
- Layout (owner): NOISE GATE as a strip in the master section, above INPUT (meter with draggable
  threshold, OPEN / CLOSED, THRESHOLD and DECAY on the right; GATE button below switches it), always
  visible. TUNER moved to the left column of GLOBAL (renamed from GLOBAL EQ / FX; title in the EQ row,
  effects got the room). GLOBAL is the default centre view; the gate + tuner view (CentrePanel) is
  gone. Frankenstein on -> its map with the vertical GLOBAL button; GLOBAL then has CLOSE back to it.
  The tuner runs while GLOBAL is visible. Master section 52 -> 92 px; EQ hint text adapts to width.

## 2026-10-21 — Session 3e: Audio / MIDI settings with single inputs — Claude (Opus 5.5)

- Mac tester: JUCE's Audio/MIDI Settings window only offers "Input 1 + 2" (stereo pairs), so a single
  input cannot be chosen there (the AMPSURD settings menu choice was not found).
- Standalone: AMPSURD's own Audio / MIDI Settings window (Options menu; JUCE's Options button gets a
  transparent click catcher because its listener is private): "Guitar input" combo with the device's
  input names on top, the mute (feedback) switch, and JUCE's device selector with inputs one by one.
  Choosing device input n makes it the only active input (-> processor channel 1, guitar input 1), saved
  with the device state. The settings menu's Guitar input lists the same device inputs (standalone) or
  input 1 / 2 (plugin). "Reset to default state" reconnects the new processor (latency, x-runs, inputs).
- Builds; not testable here (no audio device / display on the Linux build machine) - CI builds both OSes,
  a person has to try it.

## 2026-10-20 — Session 3d: ROOT (experimental low-end module) — Claude (Opus 5.5)

- Tester's brief "Fundamental Extractor / Low-End Root Enhancer"; owner: build it in the app as a
  separate, removable module next to Frankenstein, research the current state first.
- Research (docs/ROOT_MODULE.md, sources there): distorted root + fifth makes a 0.5 f difference tone
  (mostly even-order); virtual-bass research: time-domain NLD for transients, PV smears attacks /
  latency, hybrids not real-time, smooth asymmetric curve, crossover 100-250 Hz; metal practice: fast
  compression of the band below 150-200 Hz, clean low layer + distorted top; dividers only reliable on
  single notes.
- core RootEnhancer: LR4 low band at FREQ, asymmetric curve (x/(1+x) | tanh(2.25x)/2.25) + LP 2.5 x FREQ +
  DC block + RMS loudness match (GROWL), SUB divider (flip-flop on zero crossings, LP, envelope),
  TIGHT HP, BASS EQ (50 shelf / 100 / 250), COMP (thr -12..-30 dBFS, ratio 1..8, auto make-up), level,
  polarity; 20 ms fades; off = bit-identical, idle. No latency, no oversampling needed.
- Processor: root* parameters (off by default), after the engine, before the gate. CMake option
  AMPSURD_ROOT_MODULE (default ON) -> AMPSURD_ROOT; the build without it compiles (checked).
- UI: RootPanel (own files): ON, PARALLEL / POST / SUB, POLARITY, ROOT / FREQ / GROWL / TIGHT / COMP,
  layer curve + meter + COMP reduction, BASS EQ. Master row: FRANKENSTEIN (no "CREATE") + ROOT;
  input / output blocks 26 px narrower each to make room.
- Tests: tools/root_test (11 checks, CI on Windows + macOS), recorder_test ROOT (CI), ui_snapshot ROOT
  (17_root.png). Finding: a distorted major third also yields a (weaker) octave-below tone plus a tone two
  octaves down - the physics claim "only fifths" holds for 2nd order only.
- Not done: listening tests with real guitars. Next (owner): Frankenstein review.

## 2026-10-20 — Session 3c: guitar input 1 / 2 — Claude (Opus 5.5)

- First Mac tester (Scarlett Solo): the guitar was only heard through a DI box into the XLR input, because
  AMPSURD read interface input 1 only and the standalone audio settings show inputs 1+2 as one pair.
- Setting "Guitar input: input 1 / input 2" (settings menu; a setting of this computer, saved in
  AMPSURD.settings; falls back to input 1 when the host / device gives one input channel). processBlock
  reads the chosen channel; raw peaks of the chosen and the other input are kept for the editor.
- Hint: when only the other input has a signal (> -40 dBFS while the chosen one stays < -60 dBFS for 1 s),
  "GUITAR ON INPUT 2?  USE INPUT 2" replaces the INPUT slider and meter (5 s hold); one click switches.
- Tests: recorder_test (runs in CI on Windows and macOS) GUITAR INPUT: guitar on input 2 silent with
  input 1, heard with input 2, other-input peak seen; ui_snapshot: hint shown, click selects input 2
  (16_guitar_input_hint.png). Docs: BUILD_WINDOWS 38, BUILD_MACOS 8, RELEASE_NOTES.
- Owner rule from now on: every feature / look change is implemented for both Windows and macOS.

## 2026-10-19 — Session 3b: macOS 10.15 Catalina — Claude (Opus 5.5)

- Owner asked whether AMPSURD runs on Catalina. Deployment target 11.0 -> 10.15 (JUCE supports 10.11+,
  std::filesystem needs 10.15; the arm64 half stays 11 as Apple requires). The only macOS-11 API was
  std::atomic::wait / notify in ParallelRunner: on Apple (AMPSURD_NO_ATOMIC_WAIT) sleeping workers poll
  every 0.2 ms instead; the audio thread's wake-up becomes a no-op (still lock-free).
- Tested here with the polling mode forced on Linux: engine_test all PASS incl. MULTI-CORE bit-identical,
  ampsurd_bench stress OK. CI checks the x86_64 slices say minos 10.15. Not run on a real Catalina Mac.
- Installer allows 10.15; README / BUILD_MACOS / RELEASE_NOTES say 10.15 Catalina or newer.

## 2026-10-19 — Session 3a: Windows code signing with Certum — Claude (Opus 5.5)

- Owner has a Certum "Open Source Code Signing in the Cloud" certificate (Open Source Developer Radovan
  Glovňa, RSA 3072, until 2027-10-19) via SimplySign Desktop; signing must run on his PC, not in CI.
- installer/sign_release.ps1 (Windows PowerShell 5.1, ASCII only): checks signtool / Inno Setup 6 / gh
  and the certificate (by subject, /sha1 thumbprint, else /a); downloads the release's unsigned VST3 and
  standalone zips plus the tag's source archive; signs AMPSURD.exe and the DLL in AMPSURD.vst3; builds the
  installer from the tag's AMPSURD.iss with SignTool=ampsurdsign + SignedUninstaller=yes injected (ISCC /S,
  sha256, Certum TSA); signtool verify /pa /all + Get-AuthenticodeSignature (Valid, Certum subject,
  timestamp) on every file; zips; gh release upload --clobber; replaces the "NOT code-signed" warning
  in the release notes with "Windows: Signed by Open Source Developer Radovan Glovňa (Certum) ..."
  plus a SmartScreen-reputation sentence. -NoUpload for a trial run.
- Tested here: PowerShell parse, the notes replacement on the real v0.9.1 body, the [Setup] injection,
  the ISCC /S string. Not tested: real signing / Inno / upload (needs the owner's PC).
- Docs: RELEASE_SIGNING.md (owner steps), CODE_SIGNING.md (Certum now, SignPath later option), README
  (signed only when the release page says so), installer/README.md.
- Repo RELEASE_NOTES.md keeps the unsigned warning: CI publishes unsigned, the script swaps it.
- First real run by the owner (2026-10-19 20:33): v0.9.1 re-signed - DONE, all checks passed; the
  release now has the signed installer + zips (uploaded 18:33 UTC) and the "Signed by ..." notes.

## 2026-10-19 — Session 2z: macOS build — Claude (Opus 5.5)

- Owner asked for the app on Apple: same code, features, layout and design. macOS 11+, universal
  (arm64 + x86_64): standalone app, VST3 and Audio Unit (aumf, MIDI in for footswitches).
- CMake: CMAKE_OSX_DEPLOYMENT_TARGET 11.0 (std::atomic::wait), AU format on Apple, microphone
  permission text. ParallelRunner: ARM FPCR.FZ denormal flush, QoS USER_INTERACTIVE on Apple.
  Multi-core off by default on macOS (worker threads are not in the audio workgroup yet; can be
  switched on). Tooltips say Cmd+Z / Cmd+Shift+Z on the Mac (shortcuts already used Cmd).
- installer/macos: build_pkg.sh (ad-hoc signed bundles, component pkgs, non-relocatable, choices,
  licence, welcome page, AU registrar refresh) -> AMPSURD-<version>-macOS.pkg + zips.
- CI: macos job (same tests as Windows + tone_test under Rosetta + auval -strict), release moved to a
  separate job that publishes Windows and macOS files together.
- First green run 39225736530: universal binaries, all tests on Apple Silicon + Rosetta, auval
  AU VALIDATION SUCCEEDED, .pkg ~30 MB. (pkgbuild does not list a .vst3 as a bundle without the
  BundleIsRelocatable key: the script now adds it.)
- Not tested by a person on a Mac. iPad / iPhone not done (ROADMAP: AUv3 app, App Store).

## 2026-10-18 — Session 2y: EQ COPY / PASTE — Claude (Opus 5.5)

- Every EQ (amp EQ 1-5 in EDIT, IR EQ 1-5, Global EQ) has a COPY button next to FLAT (EqCopyButton).
  Clipboard in the processor, session only (copyEq / pasteEq / clearEqClipboard): EQ on / off, band count
  and every band's frequency / gain / Q, as normalised values (all EQs share the parameter ranges).
  Source shows COPIED (click = cancel), all others PASTE -> SURE? (accent) -> paste within 3 s; SURE?
  expires after 3 s and a click anywhere else cancels it (global mouse listener). The EQ glides
  (ParametricEq smoothing); one undo step (snapshot history).
- Owner: after a paste the target shows COPY again (the copy stays for the other EQs; COPY there copies
  that EQ).
- ui_snapshot EQ COPY / PASTE: amp 1 -> amp 3 and -> Global EQ equal (float rounding of the range
  conversion < 0.001 %), SURE? timeout does not paste, undo restores, COPIED click clears.

## 2026-10-17 — Session 2x: public beta 0.9.1 — Claude (Opus 5.5)

- Version 0.9.1 (CMake), release notes for 0.9.1 (not-signed warning and feedback paragraph stay first),
  CODE_SIGNING: 0.9.x betas unsigned. Published with the build workflow's publish_release run.

## 2026-10-17 — Session 2w: move amps between slots by mouse — Claude (Opus 5.5)

- Drag an amp by its name / number onto another slot (JUCE drag and drop; the editor is the container,
  slots are targets, highlighted while hovering). AmpsurdProcessor::swapSlots(a, b): swaps every s<a>_* /
  s<b>_* parameter (incl. IR EQ s<n>_ir_*), the SLOT entries (capture, IR, muted share), remaps the
  Frankenstein section amps, then restores through the normal restore (the two captures reload and are
  re-measured, ~1-2 s; one undo step).
- ui_snapshot MOVE: amp 1 -> slot 3 and back (capture, pan, mix share follow). The mouse gesture itself was
  not driven by a test.

## 2026-10-17 — Session 2v: metronome in the player / recorder — Claude (Opus 5.5)

- PlayerRecorder: metronome added after the guitar is written to disk (heard, never recorded), before the
  sum limiter. Grid from the song start (bar 1 = 0:00) while playing / recording, free running while
  stopped (practice), silent while paused. Accented first beat (1.6 kHz), 6/8 secondary accent on 4,
  45 ms sine clicks. Count-in (0 / 1 / 2 bars) for REC from STOP: the song waits, nothing is recorded,
  the count-in ends on the beat grid at the start position. A jump into the middle of a beat does not click.
- PlayerPanel row: CLICK, tempo (AMPSURD's TEMPO parameter, shared with delay SYNC; type or TAP), 2/4 ... 7/8,
  VOL, COUNT-IN, beat dots (+ "COUNT-IN n"). Click settings stored per computer.
- recorder_test: METRONOME grid + accent (a click every 24000 samples at 120 BPM), count-in 1 bar then
  recording, recorded take contains no click (peak 0). All tests pass.
- EVEN (owner): every click the same sound (no accent); a count-in bar is then 4 clicks.
- MIDI banks proposed to the owner (folders as banks, bank up / down, preset 1-5 in bank, Bank Select);
  waiting for the owner's pedal details before implementing.

## 2026-10-14 — Session 2u: undo / redo as arrow icons — Claude (Opus 5.5)

- Owner: in the standalone app PLAYER / REC overlapped UNDO. UNDO / REDO are now curved-arrow icon buttons
  (32 px); header checked in a render with the player button. Ctrl+Z / Ctrl+Y tested on the editor
  (ui_snapshot "UNDO keys"); the recording is not part of this undo (it has its own UNDO).
- v0.9.0 had 0 downloads: the release was rebuilt from this commit (build workflow input replace_release
  deletes the release and its tag, then publishes again).

## 2026-10-14 — Session 2t: beta feedback channels — Claude (Opus 5.5)

- README + release notes (v0.9.0 release and docs/RELEASE_NOTES.md): Discussions / Issues links and the
  star request, below the not-signed warning (which stays first in the release notes).
- .github/workflows/maintenance.yml (Run workflow by hand): update-release-notes (gh release edit from
  docs/RELEASE_NOTES.md; used for v0.9.0) and create-feedback-discussion ("AMPSURD beta feedback",
  text docs/FEEDBACK_DISCUSSION.md, category General). Claude's environment may not change repository
  settings: the owner enables Discussions (Settings -> General -> Features) and pins the discussion.
- Checked README / release notes / docs: no claim that AMPSURD is signed (installer/README wording fixed).

## 2026-10-14 — Session 2s: public beta 0.9.0 released — Claude (Opus 5.5)

- Release notes start with the owner's beta / not-signed warning; README and CODE_SIGNING.md state that
  releases are NOT code-signed yet (no claim of signing anywhere).
- CI green on aab5913. Pushing a tag is not allowed from Claude's environment, so the workflow got a
  manual "publish_release" run (main only, refuses an existing release, creates tag v<CMake version>).
  Run 38617499224 created tag v0.9.0 on 7e7e765 and the pre-release "AMPSURD 0.9.0 (beta)" with
  AMPSURD-Setup-0.9.0.exe and the VST3 / Standalone zips. Unsigned.
- This fulfils SignPath Foundation's "already released" condition (project doc CODE_SIGNING_PLAN).

## 2026-10-14 — Session 2r: PAN by keyboard, stereo output meter — Claude (Opus 5.5)

- PAN value under each amp is an editable label: click, type L30 / R20 / C (or -30 / 20 / 0), Enter.
- OUTPUT meter split into L and R bars (per-channel peaks from processBlock; mono bus: both the same).
- Checked in ui_snapshot renders; all checks pass.

## 2026-10-14 — Session 2q: MIDI footswitches, preset packs, undo / redo, A/B — Claude (Opus 5.5)

- **MIDI** (NEEDS_MIDI_INPUT on; acceptsMidi): processBlock queues CC / Program Change in a lock-free FIFO,
  the 30 Hz housekeeping acts (plugin/RigControl.cpp). Program Change n -> n-th preset of the preset
  folder (sorted). Settings -> MIDI footswitch / controller: MIDI learn for 30 actions (next / previous
  preset, A/B, bypass, gate, Frankenstein, delays / reverb / flanger / pitch, amp 1-5 mute / solo,
  input / output / amp input as continuous controllers), momentary (act on press, 127 after < 64) or
  latching (every change; on / off follows the value), clear all. Stored per computer (midiMap,
  midiProgramChange, midiLatching). Header shows the last MIDI action / learn prompt for 3 s.
- **Preset restore keeps unchanged captures / IRs** (same file already loaded or loading): instant preset
  switches between rigs that share captures; ECO / INPUT differences are applied by the housekeeping.
  File references resolve by name: next to the preset, then Documents\AMPSURD (incl. Packs) recursively.
- **Packs** (.ampsurdpack, zip): export = preset with file names only + captures + IRs + README (licence
  reminder); duplicate names get " (2)". Import: entries with folders rejected, unpacked into
  Documents\AMPSURD\Packs\<name> (numbered if it exists), preset loaded. Preset menu: Export / Import /
  Open packs folder (the stray "Check for updates" item in the preset menu was removed).
- **Undo / redo + A/B**: snapshot history (FNV hash of all parameters + files; a change that stays put
  for 0.4 s = one step, 50 steps), restored through the same restore (no reload of unchanged captures);
  history restarts when the host loads a state. A/B = two complete rigs of the session, B starts as a
  copy, COPY TO B / A. Header: UNDO, REDO, A, B, COPY; Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z.
- Tests (ui_snapshot, Linux): UNDO (two changes undone + redone, captures stay loaded), A/B, MIDI learn +
  footswitch toggling, PACK export (3 captures + preset + README, no folders) / import (loaded from the
  packs folder). All earlier checks pass.
- Jester Dyne IR packs (Emerald: Marshall 1960AX Greenback, 6 IRs; Brutal: modified BG412S, 15 IRs; WAV
  44.1 / 48 kHz): pages say "free even for commercial use"; no CC0 / redistribution wording found online.

## 2026-10-14 — Session 2p: owner's backlog (per-amp level, mute -> 0 %, gentler gate, shop button, TONE3000) — Claude (Opus 5.5)

- **Per-amp INPUT / OUTPUT** (EDIT -> LEVEL, params `s{n}_drive` -24...+24 dB, `s{n}_level` -24...+12 dB).
  INPUT multiplies the slot's input trim (with calibration); loudness is kept by a measured curve:
  `CaptureAnalyzer::driveCurve` renders the test signal at -24...+24 dB (6 dB steps, with the slot's IR)
  on a background thread ("AMPSURD level curves", low priority, own model copy), `loudnessAtDrive`
  interpolates. Compensation = curve(0) - curve(INPUT) (level match off) or curve(measured INPUT) -
  curve(INPUT) (level match on: the level gain already holds the measured level). Applied as part of the
  slot's level gain (also in the mix-law covariance); recomputed when INPUT / calibration / ECO / IR change.
  An amp's INPUT change also re-measures alignment and mix law (debounced, like the main INPUT).
  Defaults (0 dB) are bit-identical to before. AlignPanel compacted (AUTO / FREE / RESET in one row).
- **MUTE -> fader 0 %** (button and automation, handled in the 30 Hz housekeeping): the muted share is
  remembered (saved in presets as slot property `mutedShare`), the others are rescaled to 100 %;
  un-mute restores it. Muted amps are left out of linked-fader rescaling; muted fader is dimmed and
  not draggable; a capture loaded into an empty slot clears a leftover MUTE. Older presets with a muted
  amp whose fader is up keep it on un-mute.
- **Gate**: hysteresis 4 -> 8 dB, hold 15 -> 60 ms, closing = -60 dB * (t / DECAY)^2 (zero slope at the
  start) then fully closed, gentle 25 ms raised-cosine fade-in when a note swells back without a pick,
  pick attack: 0.9 ms S-curve as before.
- **Footer**: BUY AMPSURD AMP PACKS (bottom left, opens https://www.ampsurd.com/), logo group 34 px left.
- **TONE3000 comparison**: read their open-source plugin. Default A2 size = Lite. New per-computer setting
  "Load A2 captures as Lite (ECO) - like TONE3000" (default on) sets ECO when a capture is loaded by hand.
- Tests (Linux): gate_tuner_test +3 (fading wobbling note closes once; closing -1.5 dB after 20 ms, old
  -9 dB; swell-back fade-in 26 ms; all three failed with the old gate), engine_test INPUT (two captures,
  two guitar levels: within 0.18 dB, uncompensated up to 11.4 dB), ui_snapshot MUTE (0 / 50 / 50 ->
  saved / reloaded muted -> 42 / 29 / 29 restored) and INPUT compensation; tone_test compares the full
  model (Lite measured: -1.1 dB at 44 Hz, +0.3...+0.8 dB 700 Hz - 11 kHz). All other tests pass.
- Code signing research: project doc CODE_SIGNING_PLAN (SignPath Foundation first, Certum Open Source
  as fallback; no business registration needed).

## 2026-10-11 — Session 2o: Windows release package — Claude (Opus 5.5)

Owner: publisher = Radovan Glovňa (Slovak sole trader being set up).
- Version 0.9.0 (single source: project(AMPSURD VERSION) in CMakeLists.txt).
- installer/AMPSURD.iss (Inno Setup 6): VST3 -> C:\Program Files\Common Files\VST3, standalone app ->
  Program Files + Start menu (+ optional desktop shortcut), components VST3 / app, uninstaller (removes
  the VST3 too; presets / recordings in Documents\AMPSURD are never touched), icon (assets/icon/ampsurd.ico),
  licence page. CI builds AMPSURD-Setup-<version>.exe + portable zips on every push (artifact
  AMPSURD-Setup-windows-x64, ~10 MB); a pushed tag v<version> (must equal the CMake version) creates a
  GitHub pre-release with notes from docs/RELEASE_NOTES.md. Code signing step: placeholder in the workflow.
- Check for updates... (settings menu): asks api.github.com for the releases only when clicked, highest
  version incl. pre-releases, offers the release page (plugin/UpdateCheck.h; version compare tested).
- docs/CODE_SIGNING.md (SignPath Foundation policy: roles, what is signed, verification), PRIVACY.md
  (no data collection; only the update check on request), README download / policy links,
  BUILD_WINDOWS: installer first.
- Next: owner applies at signpath.org/apply; on approval add the SignPath signing action + API token secret.

## 2026-10-11 — Session 2n: the dragon in the background — Claude (Opus 5.5)

Concept: project doc DRAGON_BACKGROUND (owner's five-headed dragon, sketch v3). Implemented:
- tools/dragon/make_dragon.py: the approved sketch's geometry, placed on the 1200 x 800 layout,
  rendered as layers at 2x (plugin/assets/dragon, ~1.1 MB embedded): body (wings, tail, torso, legs,
  necks; head silhouettes cut out so heads cover the necks) + per head asleep / awake; eye positions.
  Writes preview_*.png when given UI snapshots.
- UI: DragonLayer composes body + heads once into a cached image (rebuilt only when a head changes)
  and draws it under all components at ~10 % (white lines, opacity 0.095); the lower panel, master
  row and footer get an 80 % background cover (EQ graph and sliders stay clean). Heads: empty slot =
  asleep (head 5 smokes), capture loaded = awake, SOLO or EDIT / IR editor = red eye (accent). A
  selected slot's fill is now painted by the editor under the dragon (head stays visible there).
  Settings menu: "Show dragon" (on by default, per computer).
- Note for the artwork: head 2's eye lands behind long capture names (text is drawn over it).

## 2026-10-11 — Session 2m: owner's polish list + PITCH — Claude (Opus 5.5)

Owner (tone now on par with TONE3000 after re-capturing; buffer question postponed) asked for:
- Look: thicker outlines (2 px) of the main sections (slots, centre area, master row); light blood
  red (#D23C42, theme `accent`) for AMPSURD, the slot numbers and the title of the open panel
  (EDIT / AMP n, IR / AMP n, GLOBAL EQ / FX, PLAYER / RECORDER, NOISE GATE, TUNER, FRANKENSTEIN);
  footer "BROUGHT TO YOU BY" + names in the light text colour; "DARK MATTERS".
- App icon: AMP over SURD, Inter SemiBold, accent red on the AMPSURD background
  (plugin/assets/icon, JUCE ICON_BIG / ICON_SMALL).
- Gate: a new note opens with a raised-cosine S-curve (0.9 ms, still before the amp attack arrives)
  instead of an exponential snap (largest slope change 0.0027 vs ~0.1).
- INPUT range -30 ... +15 dB.
- EQ: each EQ (amps, IR EQs, Global EQ) has its own band count 2 ... 10 (parameter `*_bands`,
  default 10, 2 = low + high cut only); hidden bells are flat; bells nobody used are spread over
  80 Hz ... 8 kHz when the count changes. AUTO LEVEL always on (core ParametricEq): output gain =
  -(average power gain of the curve over a guitar spectrum, 30 Hz-16 kHz, band-pass weighted),
  limited +12 / -18 dB, recomputed only when the bands change, smoothed; flat EQ untouched. Test:
  +9 / -9 / +12 / -12 dB bands change pink "cab" noise by +0.1 / +0.1 / +0.5 / +0.1 dB.
- ECO per slot (owner: active only where an A2 Lite exists): `s{n}_eco`; slimmable files (A2 Full +
  Lite in one file, e.g. NAM Core's A2.nam example) reload with SetSlimmableSize(0) (not real-time
  safe -> reloaded off the audio thread and crossfaded like any capture swap, re-measured); the ECO
  button is disabled for other captures; tooltip shows "ECO available" / "ECO running".
- Recorder: wide horizontal scrollbar under the guitar lane; keyboard shortcuts in the standalone
  app: Space = play / stop, R = record / punch in / punch out, W or Home = to the start (buttons and
  boxes no longer keep the keyboard focus, so Space also stays the DAW's transport in a DAW).
- PITCH (new, owner: as little delay as possible, before the amps): core PitchShifter - delay line
  read at 2^(semitones/12), jumps by one pitch period (autocorrelation on a 12 kHz copy every 256
  samples, top of the correlation peak + parabola, then the best-matching fractional splice point)
  with raised-cosine crossfades. -12...+12 semitones + cents, quick buttons Eb / D / C# / C.
  Tests (pitch_test): pitch within 2.1 cents (82 / 110 / 330 Hz, -12...+12 st); a shifted sine's
  artefacts 51.8 dB below the note; A + E chord shifted exactly; delay of a note's start <= 6.9 ms
  (82 Hz and 330 Hz), low power chord 8.6 ms; ON/OFF without a click; OFF bit-identical; 1.6 % CPU.
  Reported latency +5 ms while on. The tuner keeps showing the real strings.
- All tests PASS (core, engine incl. ECO + AUTO LEVEL, compat, tone_test, recorder_test incl. keys).
- Frankenstein (owner, 16:15): SECTIONS offers only as many sections as there are loaded, unmuted
  amps; the other buttons are darker and inactive; the stored choice returns when more amps play.

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
