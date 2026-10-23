# Getting AMPSURD onto Windows 11

## Easiest: the installer

1. Open https://github.com/SymbiOrbis/ampsurd/releases and download **AMPSURD-Setup-<version>.exe**
   (before the first release: Actions -> newest green run -> artifact **AMPSURD-Setup-windows-x64**).
2. Run it. It installs the VST3 plugin (C:\Program Files\Common Files\VST3) and the standalone app
   (Start menu: AMPSURD). Updating = running the newer installer; your presets and recordings stay.
3. If SmartScreen says "Windows protected your PC": **More info -> Run anyway** (until the releases are
   code-signed and have a reputation).
4. In the app: settings icon -> **Check for updates...** tells you when a newer version exists.

## Without installer: download the ready-made plugin (no tools needed)

GitHub builds the Windows VST3 automatically after every change.

1. Open https://github.com/SymbiOrbis/ampsurd/actions and click the newest run with a green tick.
2. Scroll to **Artifacts** at the bottom and click **AMPSURD-VST3-windows-x64** (you must be signed
   in to GitHub). A zip file downloads.
3. Before unzipping: right-click the zip → **Properties** → tick **Unblock** (if shown) → **OK**.
   Then unzip it. Inside is a folder called `AMPSURD.vst3`.
4. Copy that whole `AMPSURD.vst3` folder into `C:\Program Files\Common Files\VST3`
   (Windows asks for administrator permission — click **Continue**).

**"Bad Image ... Error status 0xc0e90002" when REAPER scans:** Windows 11 *Smart App Control*
blocks plugin files that are not digitally signed. Check **Windows Security → App & browser
control → Smart App Control settings**. If it is **On**, AMPSURD (and many other free plugins)
cannot load until either Smart App Control is switched off or AMPSURD is code-signed (planned
before public release). Read the warning Windows shows before switching it off.
5. In REAPER: **Options → Preferences → Plug-ins → VST → Re-scan**, then continue with Part C
   step 4 below. (When a newer build arrives, close REAPER, replace the folder, reopen.)

**Standalone app (with backing-track player and recorder):** in the same Actions run, download
**AMPSURD-Standalone-windows-x64**, Unblock the zip (as above), unzip, double-click `AMPSURD.exe`
(Smart App Control must be off, as for the plugin). Click **Options** (top left) → **Audio device
type: ASIO** → your interface's ASIO driver (e.g. "Universal Control ASIO" for PreSonus), buffer
64-128 samples. Without ASIO use "Windows Audio (Exclusive Mode)" or "(Low Latency Mode)" with a
small buffer - plain "Windows Audio" (shared mode) adds a lot of delay.
The input is not muted by default. Close REAPER first if it uses the same interface exclusively.

The rest of this page is only needed if you want to build it yourself.

# Building AMPSURD yourself (step by step)

You need to do the one-time setup (Part A) only once. After that, building is two commands.

## Part A — one-time setup (about 30–45 minutes, mostly downloading)

1. **Install Visual Studio Community** (free).
   1. Open https://visualstudio.microsoft.com/vs/community/ and click **Download**.
   2. Run the installer. When the "Workloads" screen appears, tick **Desktop development with C++**.
   3. Leave the options on the right as they are and click **Install**. Restart Windows if asked.
2. **Install Git and CMake.**
   1. Press the **Windows key**, type `Terminal`, press **Enter**.
   2. Paste this line and press **Enter**, accept any prompts:
      `winget install --id Git.Git -e ; winget install --id Kitware.CMake -e`
   3. Close the Terminal window (so it picks up the new programs).
3. **Put the project somewhere simple**, e.g. unzip `ampsurd.zip` to `C:\dev\ampsurd`
   (avoid OneDrive folders and paths with spaces).

## Part B — build the plugin

1. Press the **Windows key**, type `Developer PowerShell`, open **Developer PowerShell for VS**.
2. Type these lines one at a time, pressing **Enter** after each:
   ```
   cd C:\dev\ampsurd
   cmake -S . -B build -A x64
   cmake --build build --config Release --target Ampsurd_VST3 ampsurd_bench mix_experiment
   ```
   The first `cmake -S` downloads JUCE, NAM Core and Eigen (5–10 minutes the first time).
   The build takes another few minutes. It is finished when the prompt returns without the
   word `error`.
3. The plugin is now here:
   `C:\dev\ampsurd\build\plugin\Ampsurd_artefacts\Release\VST3\AMPSURD.vst3`

If anything prints `error`, copy the last ~30 lines and send them to me.

## Part C — use it in REAPER (one-time setup, then it updates automatically)

1. In REAPER: **Options → Preferences → Plug-ins → VST**.
2. Next to "VST plug-in paths" click **Edit path list… → Add path…** and choose
   `C:\dev\ampsurd\build\plugin\Ampsurd_artefacts\Release\VST3`
3. Click **Re-scan → Re-scan all plug-ins**, then **OK**.
4. On your guitar track, click **FX → Add**, search `AMPSURD`, double-click it.
5. Click **LOAD NAM** in a slot (or drag a `.nam` file onto it). Hover over the name to see the
   full path and capture type.

Every time you rebuild (Part B step 2, last line), close and reopen the REAPER project so
REAPER picks up the new version.

## Part D — AMPSURD listening checklist (please report back)

1. Set REAPER to **48 kHz** (Options → Preferences → Audio → Device). Load ONE A2 capture you
   know well. Settings icon (top right) → untick **Level match**. Does it sound like the same capture
   in the official NAM plugin at the same input level? (Then switch Level match back on.)
2. Load two to five captures. Move a fader: do the others move down so the total stays 100 %?
   Does the overall loudness stay about the same while you move it?
3. Switch captures while playing: any click, dropout or glitch?
4. SOLO and MUTE: do they behave like a mixer, and do the faders keep their positions?
5. EDIT on one amp: drag EQ points (left/right = frequency, up/down = gain, wheel = width,
   double-click = reset). Does the sound follow the curve?
6. Alignment: note what AUTO shows for each amp (offset, polarity, "low-end match"). Switch to
   FREE and turn TIME and PHASE slowly while playing — interesting / useful / useless?
7. Note the **CPU** value (bottom right) with five captures at 64 and 128 samples buffer size.
8. Turn **OUTPUT** fully up and play hard: the **LIMIT** box lights up; REAPER's track meter must
   never go above −1 dB.
9. Save a preset (SAVE AS), load **Init** from the PRESET menu, then load your preset again: is
   everything back? Save the REAPER project, close and reopen: is everything back?
10. Rename one capture file and reopen the project: that slot should say FILE MISSING, no crash.

11. Tuner + gate: the tuner is the left column of GLOBAL (the default view of the centre area): play
    an open string — note and cents. The NOISE GATE is always visible at the bottom (above INPUT, GATE
    button switches it): stop playing on a high-gain rig — the hiss between notes should disappear,
    while notes keep their attack and sustain. Drag the white marker on the gate meter (or THRESHOLD)
    to just above the level shown when you are not playing - also while editing an amp or mixing.

12. Frankenstein (button in the master row, bottom): load 2–5 different captures, click
    **FRANKENSTEIN**. Choose 2–5 sections at the top. Click inside a section to choose
    which amp plays there. Drag the vertical dividers left/right while playing (e.g. a tight amp
    for the lows, a fuzzy one for the highs). Turn **WIDTH** from 0 % (abrupt hand-over) to 90 %
    (smooth overlap). MUTE one amp: its section should disappear and the neighbours meet in the
    middle. Listen for clicks while dragging and switching; does the loudness stay about the same
    when you switch Frankenstein on/off? **EXIT** returns to the normal fader blend.

13. Global EQ: click the vertical **GLOBAL EQ** button right of the tuner. Switch **EQ ON**, shape
    the complete sound (e.g. low cut at 80 Hz, a little less 250 Hz). CLOSE: the button now stays
    lit and reads **GLOBAL EQ ON**. Press EDIT on an amp: a thin grey line in its EQ graph shows the
    Global EQ (it cannot be grabbed). Save a preset, change the Global EQ, reload the preset: is it
    back exactly? Switch EQ ON/OFF while playing: any click?

14. PAN (row under each filename): load two different captures, pan one left and one right
    (drag; double-click = centre). Does the sound get wide, and does the loudness stay about the
    same? Centred, everything should sound exactly as before. Also try PAN with Frankenstein on.

15. Cabinet IR: load an amp-only capture (no cabinet), click **ADD IR** under it and load a cab IR
    (WAV) - or drop the WAV onto the slot. The button lights up ("IR") and the IR name appears under
    the capture name. Is the level about the same as your full-rig captures? Switch **IR ON** off
    and on, REPLACE the IR and REMOVE it while playing: any click? Shape it with the IR EQ on the
    right. Save a preset, reload it: IR and IR EQ back? REMOVE in a slot needs two clicks.

16. Effects: in **GLOBAL** (the default centre view; from Frankenstein the vertical GLOBAL button). Choose DELAY 1, switch ON, click the TIME value and
    type 500; DELAY 2: ON, type 756. Try PING-PONG and SYNC (follows REAPER's tempo). REVERB: try the
    five types (CATHEDRAL!), DECAY, PRE-DELAY. FLANGER. Change values while playing: any click?
    Switch a delay or the reverb OFF while it rings: the tail should fade out naturally.

17. Standalone app, PLAYER / REC (button at the top): LOAD BACKING (a song or click track, WAV /
    MP3 / FLAC) - its waveform appears in the BACKING lane. **REC** records the take (GUITAR lane).
    STOP, PLAY: does your take sit exactly in time with the backing? If it is consistently early or
    late, set OFFSET (ms) and tell me the value.
    **Corrections:** click in the timeline a few seconds before a mistake, PLAY, and press **REC**
    just before the mistake (punch in) - the old take goes quiet, play the part again - press REC
    again after it (PUNCH OUT) or STOP. The correction appears as a numbered box. Drag its left or
    right edge to move where it starts / ends (also earlier than where you pressed REC, because the
    whole pass was recorded). Listen to the joins: no click? **UNDO** goes back one step; right-click
    a correction to remove it; CLEAR TAKE (twice) removes the whole guitar track.
    Mouse wheel over the timeline = zoom, Shift + wheel = scroll, FIT = whole song, click = go there.
    SAVE AS: CD quality (WAV 16-bit 44.1 kHz), guitar alone and with backing; BOUNCE, then record a
    second layer on top.

18. Tone vs. another NAM player (same .nam, same sample rate and buffer): first switch **Level match
    off** (settings icon) and match the loudness with OUTPUT - a louder sound always seems fuller.
    If the other player calibrates its input, switch on **Calibrate input to each capture** in the
    settings menu and choose **My interface's input level** (the dBu level that gives full scale on
    your interface's guitar input - see its manual; +12 dBu is NAM's default). Hover over a capture
    name: "Recorded at input level ... dBu" shows the level the capture was made at.

19. Clicks: watch the footer. **DROPOUTS n** appears when the computer missed audio buffers - if the
    number rises exactly when you hear a click, use a larger buffer (256) or close other programs.
    If you hear clicks and the number does NOT rise, please tell me (with GATE on or off?).

19b. CPU: with 3-5 captures note the CPU value with **Use several CPU cores** (settings icon) on and
    off. MUTE an amp: after about a second the CPU value drops; unmute: it comes back after ~0.1 s
    without a click.

20. Frankenstein **NOTES** (header of the Frankenstein panel): low notes play through the amp of the
    left section, high notes through the right one - play a scale upwards and listen to the amps
    taking over. **TONE** = the previous mode (every note, split by its frequency content). The thin
    band under the graph shows where the note you play sits.

21. PITCH (GLOBAL -> PITCH): press D -2 and play: does it feel like a guitar tuned to D standard?
    Low power chords and single notes - any warble, and is the extra delay noticeable? Switch ON / OFF
    while playing: no click?

22. EQ bands: in an EQ choose 4 BANDS - only low cut, two bells, high cut remain. AUTO LEVEL: boost a band
    by +12 dB - the sound changes colour but not loudness (the readout shows how much the EQ turned
    itself down).

23. ECO (in a slot, only for A2 captures that contain a Lite model): switch it on - the CPU value drops,
    the tone stays close.

24. Standalone keys: Space = play / stop, R = record / punch in / out, W or Home = back to the start.
    The wide bar under the guitar track scrolls through the song.

25. Per-amp LEVEL (EDIT -> LEVEL): turn an amp's INPUT to +12 dB - it distorts more, but stays about as
    loud (the line below says how much AMPSURD compensates; "Measuring..." for a second or two after
    loading). OUTPUT makes the amp louder / quieter in the mix. Double-click = 0 dB.

26. MUTE an amp: its fader drops to 0 %, the others show the real shares (e.g. 50 / 50). Un-mute: the
    old shares come back. Also try: mute, SAVE the preset, load another, load it again, un-mute.

27. Gate: play a long note and let it ring out near the threshold (or with vibrato) - the gate should
    close once, softly, without stuttering; a note that swells back fades in instead of snapping open.

28. Footer: BUY AMPSURD AMP PACKS (bottom left) opens www.ampsurd.com in the browser.

29. Tone vs. TONE3000: settings menu "Load A2 captures as Lite (ECO) - like TONE3000" is on by default
    (TONE3000 also plays A2 captures as Lite). Load the same capture in both, gate off, same input,
    level matched - do they sound alike now? Switch ECO off on the slot to hear the full model.

30. UNDO / REDO (header, or Ctrl+Z / Ctrl+Y): change a few things, undo them one by one. A / B: press B,
    change the sound, switch A <-> B while playing. COPY TO B copies what you hear to the other side.

31. MIDI footswitch: standalone - Audio settings: enable your MIDI input; REAPER - route the MIDI to the
    AMPSURD track. Settings -> MIDI footswitch / controller -> e.g. "Amp 1 mute", then press the switch:
    the header shows "CC n -> Amp 1 mute"; each press toggles it. If every second press does nothing,
    tick "Latching footswitches". Program Change 1, 2, 3 ... selects the presets in alphabetical order.

32. Preset switching between presets that use the same captures should be instant (no reload).

33. Packs: preset menu -> Export pack... -> send the file to another computer -> Import pack... : the rig
    loads with its captures and IRs (unpacked into Documents\AMPSURD\Packs).

34. PAN by keyboard: click the PAN value of an amp (C / L40 / R35), type e.g. L30, R20 or C, Enter.
    OUTPUT meter: two bars, L above R - pan one amp hard left: the L bar is higher.

35. Metronome (standalone, PLAYER / REC): CLICK on - it clicks also while stopped. Set the tempo (type it or
    TAP at least twice), 4/4 / 3/4 / 6/8, VOL. COUNT-IN 1 BAR + REC from STOP: one bar of clicks, then the
    recording starts. Save the recording: the click must not be in it. EVEN = every click the same.

36. Move an amp: grab it by its name and drag it onto another slot - the two amps swap places (capture,
    IR, EQ, pan, levels, mute ...). Frankenstein keeps each amp's section. Ctrl+Z undoes the move.

37. EQ COPY / PASTE (amp EQ in EDIT, IR EQ, Global EQ): COPY on one EQ (it shows COPIED), every other EQ
    shows PASTE; PASTE -> SURE? -> click again within 3 s. The EQ glides to the copied curve; Ctrl+Z
    undoes it. Clicking COPIED again (or waiting / clicking elsewhere on SURE?) cancels.

38. Guitar input: with an interface whose guitar jack is input 2 (e.g. Scarlett Solo INST), plug the guitar
    there and play: after about a second "GUITAR ON INPUT 2? USE INPUT 2" replaces the INPUT slider - click
    it, the guitar is heard. Settings menu -> Guitar input shows input 2 (kept after restart). Input 1
    still works as before. (Same on the Mac.)
    Standalone: Options (top left) -> Audio / MIDI Settings... is AMPSURD's own window now: "Guitar input"
    on top lists the interface's inputs by name (any of them, also on 8-channel interfaces); the input
    channels below are listed one by one (JUCE showed "Input 1 + 2" only). The settings menu -> Guitar
    input lists the same names. The choice is kept after a restart.

39. ROOT (EXPERIMENTAL, master row next to FRANKENSTEIN; docs/ROOT_MODULE.md): play low power chords /
    palm mutes, open ROOT, ON. PARALLEL first: turn ROOT up and down, then GROWL (more difference tones),
    FREQ (deeper vs. punchier), COMP (palm mutes as big as open chords), BASS EQ. If the low end gets
    thinner, press POLARITY. Try POST and SUB (single low notes). Compare with ROOT off at similar
    loudness. Note what sounds good and what does not - it may be kept, changed or removed.

40. GLOBAL is the default: close EDIT / IR / PLAYER / ROOT -> GLOBAL (tuner, EQ, effects) comes back.
    Frankenstein on -> its map; the vertical GLOBAL button opens GLOBAL, its CLOSE returns.
41. Player: PAN under GUITAR (left of the guitar track) is where your guitar sits - set it BEFORE REC: the
    take is recorded there (hard left = only the left channel) and plays back as recorded. Stereo layering:
    PAN left, record, BOUNCE, PAN right, record the next take. The square X next to BACKING / GUITAR deletes
    that track after DELETE / CANCEL (guitar: UNDO brings it back); the X sits right next to the track.
    CLEAR TAKE is gone.
    MUTE (master row, right of BYPASS): the whole output to silence, red while on; not saved. UNDO takes back the last recording and, when
    there is none left, the last BOUNCE (the backing from before and the take come back). GUITAR VOL: the take's level and your guitar
    on the monitors while recording (not in the file). The scroll bar under the guitar track is 8 px high.

42. VIDEO takes (standalone, needs a camera): PLAYER / REC -> VIDEO: the camera window opens (choose the
    camera, you see yourself; the window stays in front of AMPSURD). In the camera window: REC - the song goes
    to the start, count-in / recording starts, the window shows REC VIDEO and the time; play, then STOP (same
    button). Right after STOP the MP4 WITH the sound is made automatically (status: the file name). PLAY -
    the take's video with AMPSURD's sound (STOP: same button). SAVE MP4 - another MP4 straight into
    Documents / AMPSURD / Recordings (no dialog), sound GUITAR + BACKING or GUITAR ONLY (next to it); the
    status line at the bottom says the file name or why it failed; SHOW FILE opens the folder. REC with a
    take already there shows REPLACE? - press REC again within 3 s (UNDO brings the old take back). Check:
    are picture and sound together (clap or a hard pick stroke)? If not, move SOUND OFFSET, PLAY / save again;
    note the value. Close the camera window = VIDEO off.

43. Blend tone: load two different amps, 100:0 vs. 50:50 vs. 0:100 at matched loudness: the 50:50 blend
    should sound like "half of each", not darker than both. The automatic EQ learns while you play: give
    it ~5-10 s of playing after changing amps or alignment (it changes slowly, never with single notes).

44. DOUBLER (EXPERIMENTAL, master row; docs/DOUBLER.md): play a riff, open DOUBLER, ON. DOUBLES 1 / 2 / 3,
    TIGHT (tight vs. loose player), LEVEL, WIDTH, LOW MONO, CHARACTER. Listen on headphones and on speakers,
    and once in mono (e.g. a mono button on the interface): does it sound like real second takes, no
    phasing / chorus, no clicks between phrases, as loud as without it? Note what works and what does not.

Expected latency shown by REAPER: 104 samples at 48 kHz, 123 at 44.1 kHz.

Optional measurements (in Developer PowerShell):
```
.\build\tools\Release\ampsurd_bench.exe "C:\path\to\capture.nam" --slots 5
.\build\tools\Release\mix_experiment.exe "C:\caps\a.nam" "C:\caps\b.nam" "C:\caps\c.nam"
```
The second one repeats the mix-law loudness experiment with your own captures.
