<#
AMPSURD - train all NAM captures of one amp on rented GPUs (RunPod), with one command on your PC.

  powershell -ExecutionPolicy Bypass -File .\train_amp.ps1 -Amp "Mesa Dual" -Recordings "C:\...\Mesa Dual\recordings"

What it does:
  1. uploads the recordings (*.wav) to Google Drive: "AMPSURD training\<Amp>\recordings"
     (and the input file T3K-sweep-v3.wav to "AMPSURD training\_input", once)
  2. rents up to -Machines machines (at most 8; cheapest suitable GPU that is free). Each trains -PerMachine recordings
     side by side and then takes the next untrained one by itself (one shared queue on Google Drive) until all are done -
     so all recordings get trained in one run, however many there are, and this PC can be switched off
  3. each machine uploads its .nam files to "AMPSURD training\<Amp>\NAMs" and switches itself off
     (also on errors, and at the latest after -MaxHours hours - after uploading what it finished)
  4. this window shows the progress; when all machines are off it downloads the .nam files to
     "<Recordings>\..\NAMs". You can close the window at any time - the machines carry on; later:
        .\train_amp.ps1 -Amp "Mesa Dual" -Collect -Recordings "C:\...\Mesa Dual\recordings"
     downloads what is finished.

Running it again for the same amp trains only what is not finished yet (e.g. after the safety limit).

Once on this PC:
  - rclone.exe (rclone.org/downloads) - put it next to this script (or C:\rclone.exe, or say where with -Rclone)
  - the first run asks for your RunPod API key (Settings / Credentials - API Keys, permission "All") and keeps it
    encrypted for your Windows user in %USERPROFILE%\.ampsurd\runpod_key.txt
  - the first run connects rclone to your Google Drive (a browser window: allow access). rclone only sees the files
    it created itself.

Options: -Machines 8 -PerMachine 2 -Epochs 400 -MaxHours 8  -InputFile <path to T3K-sweep-v3.wav>  -NoWait
#>
param(
    [Parameter(Mandatory = $true)][string]$Amp,
    [string]$Recordings = "",
    [string]$InputFile = "",
    [int]$Machines = 8,
    [int]$PerMachine = 2,   # 3 at once on one GPU took 3.5-4 h (2026-11-18/20 runs) - 2 take ~1.5 h and cost less per capture
    [int]$Epochs = 400,
    [int]$MaxHours = 8,
    [string]$Rclone = "",
    [switch]$Collect,
    [switch]$NoWait
)

# rclone writes notices to stderr; with "Stop" PowerShell 5 would treat them as errors - errors are checked by hand
$ErrorActionPreference = "Continue"
$env:RCLONE_LOG_LEVEL = "ERROR"
$Raw = "https://raw.githubusercontent.com/SymbiOrbis/ampsurd/main/tools/nam_training"
# RunPod REST API v2 (rest.runpod.io/v1 stopped existing in Nov 2026 - its name no longer resolves)
$Api = "https://api.runpod.io/v2"
$Image = "runpod/pytorch:1.5.0-cu1281-torch280-ubuntu2404"
# cheapest suitable first (RunPod takes the first one that is free); all are fast enough for NAM
$Gpus = @(
    "NVIDIA RTX A4500", "NVIDIA RTX A5000", "NVIDIA RTX 4000 Ada Generation", "NVIDIA GeForce RTX 3090",
    "NVIDIA A40", "NVIDIA L4", "NVIDIA RTX A6000", "NVIDIA GeForce RTX 4090"
)
$Root = "gdrive:AMPSURD training"
$Slug = ($Amp.ToLower() -replace "[^a-z0-9]+", "-").Trim("-")

function Say($t) { Write-Host ("{0}  {1}" -f (Get-Date -Format "HH:mm"), $t) }
function Fail($t) { Write-Host $t -ForegroundColor Red; exit 1 }

# --- rclone + Google Drive ---
# rclone.exe: next to this script, then C:\rclone.exe, then anywhere on the PATH
if (-not $Rclone) {
    foreach ($c in @((Join-Path $PSScriptRoot "rclone.exe"), "C:\rclone.exe")) { if (Test-Path $c) { $Rclone = $c; break } }
    if (-not $Rclone) { $cmd = Get-Command rclone.exe -ErrorAction SilentlyContinue; if ($cmd) { $Rclone = $cmd.Source } }
}
if (-not $Rclone -or -not (Test-Path $Rclone)) { Fail "rclone.exe not found - put it next to train_amp.ps1 (rclone.org/downloads) or use -Rclone <path>" }
$remotes = & $Rclone listremotes
if ($remotes -notcontains "gdrive:") {
    Say "Connecting rclone to your Google Drive - a browser window opens, allow access ..."
    & $Rclone config create gdrive drive scope=drive.file | Out-Null
    if ((& $Rclone listremotes) -notcontains "gdrive:") { Fail "Google Drive is not connected." }
}

# --- RunPod key (encrypted for this Windows user) ---
$KeyDir = Join-Path $env:USERPROFILE ".ampsurd"
$KeyFile = Join-Path $KeyDir "runpod_key.txt"
if (-not (Test-Path $KeyFile)) {
    New-Item -ItemType Directory -Force -Path $KeyDir | Out-Null
    $sec = Read-Host "RunPod API key (Credentials - API Keys, permission All)" -AsSecureString
    $sec | ConvertFrom-SecureString | Set-Content $KeyFile
}
$Key = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR((Get-Content $KeyFile -ErrorAction Stop | ConvertTo-SecureString -ErrorAction Stop)))
$Headers = @{ Authorization = "Bearer $Key" }

function Get-AmpPods {
    $r = Invoke-RestMethod -Uri "$Api/pods" -Headers $Headers -ErrorAction Stop
    $all = if ($null -ne $r.pods) { $r.pods } else { $r }
    # v2 may still list switched-off machines: only those that are (or are becoming) active count
    @($all | Where-Object { $_.name -like "ampsurd-$Slug-*" -and @("TERMINATED", "EXITED", "ERROR") -notcontains $_.status })
}

$NamDir = if ($Recordings) { Join-Path (Split-Path $Recordings -Parent) "NAMs" } else { Join-Path ([Environment]::GetFolderPath("MyDocuments")) "AMPSURD training\$Amp\NAMs" }
function Collect-Nams {
    New-Item -ItemType Directory -Force -Path $NamDir | Out-Null
    & $Rclone copy "$Root/$Amp/NAMs" $NamDir --include "/*.nam" --include "/summary*.txt" --include "/summaries/**" --include "/logs/**"
    $n = @(Get-ChildItem $NamDir -Filter *.nam).Count
    Say "$n .nam files in $NamDir"
}

if ($Collect) { Collect-Nams; exit 0 }

# --- upload ---
if ($Recordings) {
    if (-not (Test-Path $Recordings)) { Fail "Folder not found: $Recordings" }
    Say "Uploading the recordings to Google Drive (AMPSURD training\$Amp\recordings) ..."
    & $Rclone copy $Recordings "$Root/$Amp/recordings" --include "*.wav" -P
    if ($LASTEXITCODE -ne 0) { Fail "Upload failed." }
}
$haveInput = @(& $Rclone lsf "$Root/_input" --include "T3K-sweep-v3.wav" 2>$null).Count -gt 0
if (-not $haveInput) {
    if (-not $InputFile) { Fail "The input file is not on Google Drive yet: run again with -InputFile <path to T3K-sweep-v3.wav>" }
    & $Rclone copyto $InputFile "$Root/_input/T3K-sweep-v3.wav" -P
}
# --files-only: folders (status, the machines' work folders) are not captures
$files = @(& $Rclone lsf "$Root/$Amp/recordings" --files-only --include "/*.wav")
$done = @(& $Rclone lsf "$Root/$Amp/NAMs" --files-only --include "/*.nam" 2>$null)
if ($files.Count -eq 0) { Fail "No recordings on Google Drive for '$Amp'." }
Say "$($files.Count) recordings, $($done.Count) already trained"
if ($done.Count -ge $files.Count) { Say "Nothing left to train."; Collect-Nams; exit 0 }

try { $already = @(Get-AmpPods).Count } catch {
    $code = 0; try { $code = [int]$_.Exception.Response.StatusCode } catch {}
    if ($code -eq 401 -or $code -eq 403) { Fail "RunPod refused the API key ($($_.Exception.Message)) - delete $KeyFile to enter it again." }
    Fail "RunPod could not be reached ($($_.Exception.Message)) - check the internet connection and try again later."
}
if ($already -gt 0) { Fail "Machines for '$Amp' are still running - wait for them, or check RunPod." }

# --- machines ---
# at most 8 machines at once (owner 2026-11-22: RunPod refused machine 9+ twice; 8 are enough)
$MaxMachines = 8
if ($Machines -gt $MaxMachines) { Say "At most $MaxMachines machines at once - using $MaxMachines."; $Machines = $MaxMachines }
$todo = $files.Count - $done.Count
$Machines = [Math]::Max(1, [Math]::Min($Machines, [Math]::Ceiling($todo / $PerMachine)))
# the "taken" marks of an earlier run are stale now (no machine of this amp is running): start with an empty queue
& $Rclone purge "$Root/$Amp/claims" 2>$null | Out-Null
$tok = ((& $Rclone config dump) | ConvertFrom-Json).gdrive.token
Say "Renting $Machines machine(s), $PerMachine trainings at a time each - they work through all $todo recordings, $Epochs epochs, safety limit $MaxHours h ..."
for ($m = 1; $m -le $Machines; $m++) {
    $body = @{
        name = "ampsurd-$Slug-$m"
        image = $Image
        disk = 30
        env = @{
            AMP = $Amp; SLOTS = "$PerMachine"; EPOCHS = "$Epochs"; MAX_HOURS = "$MaxHours"
            RCLONE_CONFIG_GDRIVE_TYPE = "drive"; RCLONE_CONFIG_GDRIVE_SCOPE = "drive.file"; RCLONE_CONFIG_GDRIVE_TOKEN = $tok
            AMPSURD_RUNPOD_KEY = $Key
        }
        cmd = @("bash", "-c", "curl -fsSL $Raw/pod_job.sh -o /pod_job.sh && bash /pod_job.sh")
    }
    # v2 takes one GPU type per request: try them cheapest first, secure cloud first
    $pod = $null; $last = ""
    foreach ($cloud in @("SECURE", "COMMUNITY")) {
        foreach ($g in $Gpus) {
            $body.cloud = $cloud
            $body.gpu = @{ id = $g; count = 1; minVcpuCountPerGpu = 8 }
            try {
                $pod = Invoke-RestMethod -Method Post -Uri "$Api/pods" -Headers $Headers -ContentType "application/json" `
                    -Body ($body | ConvertTo-Json -Depth 5) -ErrorAction Stop
                break
            } catch {
                $last = $_.Exception.Message
                $code = 0; try { $code = [int]$_.Exception.Response.StatusCode } catch {}
                if ($code -eq 401 -or $code -eq 402) { Fail "RunPod refused the request ($last) - nothing was rented." }
            }
        }
        if ($pod) { break }
    }
    if (-not $pod) { Say "Machine $m could not be rented ($last) - the others take over its recordings."; continue }
    $gpu = if ($pod.gpu.id) { $pod.gpu.id } else { "GPU" }
    $price = if ($pod.cost) { " - `${0}/h" -f $pod.cost } else { "" }
    Say "  machine $m ($gpu$price)"
}

if ($NoWait) {
    Say "The machines work on their own. Results: Google Drive - AMPSURD training\$Amp\NAMs (or run with -Collect)."
    exit 0
}

# --- progress ---
Say "Waiting - a report every 5 minutes (you can close this window - the machines carry on) ..."
$total = $files.Count
do {
    Start-Sleep -Seconds 300
    $n = @(& $Rclone lsf "$Root/$Amp/NAMs" --files-only --include "/*.nam" 2>$null).Count
    try { $pods = @(Get-AmpPods) } catch { Say "RunPod did not answer - trying again in 5 minutes"; continue }
    $running = $pods.Count
    $lines = @(); $worst = 0; $per = 0
    foreach ($p in ($pods | Sort-Object name)) {
        $st = & $Rclone cat "$Root/$Amp/NAMs/status/status_$($p.id).txt" 2>$null
        if (-not $st) { $st = "starting ..." }
        # "<seconds left on the current recordings> <seconds per recording>"
        $et = ((& $Rclone cat "$Root/$Amp/NAMs/status/eta_$($p.id).txt" 2>$null) -join " ").Trim() -split "\s+"
        $eta = 0; [int]::TryParse($et[0], [ref]$eta) | Out-Null
        $one = 0; if ($et.Count -gt 1) { [int]::TryParse($et[1], [ref]$one) | Out-Null }
        if ($eta -gt $worst) { $worst = $eta }
        if ($one -gt $per) { $per = $one }
        $lines += ("   machine {0}: {1}" -f ($p.name -replace "^ampsurd-$Slug-", ""), $st)
    }
    $left = ""
    # recordings still waiting in the queue (not finished, not being trained): further rounds on all machines
    $waiting = [Math]::Max(0, $total - $n - $running * $PerMachine)
    if ($running -gt 0 -and $per -gt 0) { $worst += [Math]::Ceiling($waiting / ($running * $PerMachine)) * $per }
    if ($worst -gt 0) {
        $left = " - about {0} h {1} min left (around {2})" -f [Math]::Floor($worst / 3600), [Math]::Floor(($worst % 3600) / 60), (Get-Date).AddSeconds($worst).ToString("HH:mm")
    }
    Say "$n of $total trained, $running machine(s) running$left"
    $lines | ForEach-Object { Write-Host $_ }
} while ($running -gt 0)
Collect-Nams
Get-ChildItem $NamDir -Filter "summary*.txt" -Recurse | ForEach-Object { Get-Content $_.FullName | Select-String "OK|FAILED|SKIPPED" }
