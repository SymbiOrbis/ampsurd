# AMPSURD - features for the website

Screenshots: `website/screenshots/`, PNG at 2x resolution (made with `tests/ui_snapshot`, AMPSURD_WEB_SHOTS=1).
Demo setup: three example captures that come with Neural Amp Modeler (shown as "NAM example A2 / LSTM /
WaveNet"), a demo cabinet IR on amp 2, mix 40 / 35 / 25 %, pans L30 / C / R30, noise gate on. No real
amp names or third-party IRs are shown.

---

### 01_full_window.png
The whole AMPSURD window in its default view: five amp slots on top, GLOBAL (tuner, Global EQ, effects) in the middle, the noise gate and output row at the bottom.
- Everything is on one screen - no hidden pages, no menus to dig through.
- Load up to five amp captures and blend them into one sound.
- Free and open source, as a plugin (VST3, AU on Mac) and a standalone app.

### 02_amp_slots.png
The five amp slots, each with its own fader, PAN and buttons (LOAD NAM, EDIT, IR, SOLO, MUTE, REMOVE, ECO).
- Move one fader and the others follow: the blend always adds up to 100 % and stays equally loud.
- Pan each amp left or right for a wide stereo sound from one guitar.
- SOLO or MUTE any amp to hear what it adds; ECO plays a lighter version of the capture when your computer is busy.

### 03_global_tuner_eq_effects.png
GLOBAL, the default centre of the window: tuner, Global EQ and the effects for the complete sound.
- Shape the finished blend in one place, after all the amps.
- The effects are right next to the EQ - no extra plugins needed for delay or reverb.
- AUTO LEVEL keeps the volume the same while you change the EQ.

### 04_tuner.png
The built-in tuner, showing a guitar string in tune.
- Tune without leaving AMPSURD.
- MUTE OUTPUT silences the sound while you tune.

### 05_gate_input_output.png
The noise gate with its meter, input and output levels, and the main buttons (FRANKENSTEIN, ROOT, DOUBLER, GATE, BYPASS, MUTE).
- The gate keeps pauses quiet, even with high-gain amps, and closes gently so notes don't get cut off.
- A built-in limiter means AMPSURD never clips your output.
- BYPASS compares with your dry guitar; MUTE (red when on) silences everything at once.

### 06_header_presets_undo_ab.png
The top bar: PLAYER / REC, undo and redo, A / B compare, presets, SAVE and SAVE AS.
- Undo and redo any step (Ctrl+Z / Cmd+Z): a sound change, a recording, a deleted track - all in one history.
- A / B: keep two complete rigs and switch between them to hear which one is better.
- Presets save the complete rig: amps, blend, EQs, IRs and effects.

### 07_amp_edit_eq.png
EDIT for one amp: its own 10-band EQ, alignment and input / output level.
- Fix each amp separately - for example take the mud out of one amp and the fizz out of another.
- AMPSURD lines up the amps' timing automatically, so the blend sounds full instead of hollow; FREE lets you shift it by ear.
- Drag a point on the curve to change the EQ - simple and visual.

### 08_eq_copy_paste.png
The EQ of amp 2 with PASTE ready, after copying amp 1's EQ.
- Copy an EQ from one amp and paste it onto another (or onto the Global or IR EQ) in one click.
- FLAT resets an EQ when you want to start again.

### 09_cabinet_ir.png
The cabinet IR editor of one amp: the loaded IR, on / off, and its own EQ.
- Load your own cabinet IR (impulse response) for any amp.
- Each IR has its own EQ, separate from the amp's EQ.
- Switch the IR off to compare with and without it.

### 10_effects_delay.png
DELAY 1 in GLOBAL: time, feedback, level, tone, tempo, SYNC and PING-PONG.
- Two independent delays - for example a short slapback and a longer echo.
- SYNC follows the tempo (TAP it in), PING-PONG bounces repeats between left and right.

### 11_effects_reverb.png
The reverb with five types: ROOM, HALL, PLATE, CATHEDRAL, AMBIENCE.
- From a small room to a huge space, with decay, pre-delay and tone.
- Put your guitar in a space without loading another plugin.

### 12_pitch_retune.png
PITCH: play in a lower tuning without re-tuning your guitar (Eb, D, C#, C or any step).
- Play songs in drop or down tunings on a guitar in standard tuning.
- The tuner shows the note as if the strings were really re-tuned.

### 13_frankenstein.png
FRANKENSTEIN: the sound split into 2 to 5 sections, each played by a different amp.
- Take the lows from one amp, the mids from another and the highs from a third.
- TONE mode splits the sound; NOTES mode gives each range of notes its own amp.
- WIDTH sets how smoothly one amp hands over to the next.

### 14_root.png
ROOT (experimental): a clean, tight low layer under the amps.
- Adds weight and punch to power chords without making the amps muddy.
- Three modes: PARALLEL (from your clean guitar), POST (from the amps) and SUB (an octave below, best on single notes).

### 15_doubler.png
DOUBLER (experimental): one guitar sounds like two to four takes of the same part, left and right.
- The classic "double tracked" rhythm sound from one performance, in real time.
- TIGHT, LEVEL, WIDTH and CHARACTER set how tight and how different the second takes sound.
- LOW MONO keeps the low end centred and tight.

### 16_player_recorder.png
The player / recorder: a backing track, a recorded guitar take, the metronome and saving.
- Load a backing track (WAV, FLAC, MP3, OGG, AIFF) and record yourself over it - punch in corrections, then BOUNCE and record another layer.
- The metronome (hi-hat or beep, count-in, tap tempo) is heard but never recorded.
- Save the guitar alone or with the backing, as WAV or FLAC; recordings are lined up automatically.

### 16b_full_window_player.png
The whole window with the player / recorder open in the centre.
- Practise and record without a DAW: amps, effects and recorder in one window.
- In the standalone app, a video take with your camera can be recorded too (saved as an MP4 with AMPSURD's sound).

### 17_preset_menu.png
The preset menu: Init (empty rig), your saved presets, packs and folders.
- Pick a saved rig with one click; the current one is marked.
- Export a pack: one file with the preset plus its NAM captures and IRs - easy to share or move to another computer.
- Import a pack and everything is in place.
