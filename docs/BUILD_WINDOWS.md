# Getting AMPSURD onto Windows 11

## Easiest: download the ready-made plugin (no tools needed)

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

11. Gate + tuner (centre area when no amp is in EDIT): play an open string — the tuner shows
    note and cents. Stop playing on a high-gain rig — the hiss between notes should disappear,
    while notes keep their attack and sustain. Drag the white marker on the GUITAR LEVEL meter
    to set the threshold just above the level shown when you are not playing.

12. Create Frankenstein (button in the master row, bottom): load 2–5 different captures, click
    **CREATE FRANKENSTEIN**. Choose 2–5 sections at the top. Click inside a section to choose
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

16. Effects: open **EQ / FX** (vertical button). Choose DELAY 1, switch ON, click the TIME value and
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

Expected latency shown by REAPER: 104 samples at 48 kHz, 123 at 44.1 kHz.

Optional measurements (in Developer PowerShell):
```
.\build\tools\Release\ampsurd_bench.exe "C:\path\to\capture.nam" --slots 5
.\build\tools\Release\mix_experiment.exe "C:\caps\a.nam" "C:\caps\b.nam" "C:\caps\c.nam"
```
The second one repeats the mix-law loudness experiment with your own captures.
