# Building MONSTROSITY on Windows 11 (step by step)

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
3. **Put the project somewhere simple**, e.g. unzip `monstrosity.zip` to `C:\dev\monstrosity`
   (avoid OneDrive folders and paths with spaces).

## Part B — build the plugin

1. Press the **Windows key**, type `Developer PowerShell`, open **Developer PowerShell for VS**.
2. Type these lines one at a time, pressing **Enter** after each:
   ```
   cd C:\dev\monstrosity
   cmake -S . -B build -A x64
   cmake --build build --config Release --target Monstrosity_VST3 monstrosity_bench
   ```
   The first `cmake -S` downloads JUCE, NAM Core and Eigen (5–10 minutes the first time).
   The build takes another few minutes. It is finished when the prompt returns without the
   word `error`.
3. The plugin is now here:
   `C:\dev\monstrosity\build\plugin\Monstrosity_artefacts\Release\VST3\MONSTROSITY.vst3`

If anything prints `error`, copy the last ~30 lines and send them to me.

## Part C — use it in REAPER (one-time setup, then it updates automatically)

1. In REAPER: **Options → Preferences → Plug-ins → VST**.
2. Next to "VST plug-in paths" click **Edit path list… → Add path…** and choose
   `C:\dev\monstrosity\build\plugin\Monstrosity_artefacts\Release\VST3`
3. Click **Re-scan → Re-scan all plug-ins**, then **OK**.
4. On your guitar track, click **FX → Add**, search `MONSTROSITY`, double-click it.
5. Click **LOAD .nam**, choose a capture. The info area shows its name, type (e.g.
   `Slimmable [A2 Lite + A2 Full]`), sample rates and latency.

Every time you rebuild (Part B step 2, last line), close and reopen the REAPER project so
REAPER picks up the new version.

## Part D — Milestone 1 listening checklist (please report back)

1. Set REAPER to **48 kHz** (Options → Preferences → Audio → Device). Load an A2 capture you
   know well. Does it sound like the same capture in the official NAM plugin? (Same input
   level, MONSTROSITY "Normalise loudness" OFF, NAM plugin "Normalize" OFF.)
2. Switch captures while playing: any click, dropout or glitch?
3. Note the **CPU** value in the top right at 64 and 128 samples buffer size.
4. Switch the project to **44.1 kHz** and **96 kHz**: still sounds right? (A latency of 27 /
   42 samples is expected and is reported to REAPER.)
5. Save the project, close REAPER, reopen: is the capture restored?
6. Rename/move the capture file and reopen the project: you should see a "Capture file not
   found" message, not a crash.

Optional CPU measurement (in Developer PowerShell):
```
.\build\tools\Release\monstrosity_bench.exe "C:\path\to\capture.nam" --slots 5
```
