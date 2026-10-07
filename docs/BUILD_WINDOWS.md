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

Expected latency shown by REAPER: 104 samples at 48 kHz, 123 at 44.1 kHz.

Optional measurements (in Developer PowerShell):
```
.\build\tools\Release\ampsurd_bench.exe "C:\path\to\capture.nam" --slots 5
.\build\tools\Release\mix_experiment.exe "C:\caps\a.nam" "C:\caps\b.nam" "C:\caps\c.nam"
```
The second one repeats the mix-law loudness experiment with your own captures.
