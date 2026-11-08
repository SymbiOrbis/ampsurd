<#
AMPSURD - train all NAM captures of one amp on rented GPUs (RunPod), with one command on your PC.

  powershell -ExecutionPolicy Bypass -File .\train_amp.ps1 -Amp "Mesa Dual" -Recordings "C:\...\Mesa Dual\recordings"

What it does:
  1. uploads the recordings (*.wav) to Google Drive: "AMPSURD training\<Amp>\recordings"
     (and the input file T3K-sweep-v3.wav to "AMPSURD training\_input", once)
  2. rents -Machines machines (cheapest suitable GPU that is free); each trains -PerMachine recordings side by side
  3. each machine uploads its .nam files to "AMPSURD training\<Amp>\NAMs" and switches itself off
     (also on errors, and at the latest after -MaxHours hours - after uploading what it finished)
  4. this window shows the progress; when all machines are off it downloads the .nam files to
     "<Recordings>\..\NAMs". You can close the window at any time - the machines carry on; later:
        .\train_amp.ps1 -Amp "Mesa Dual" -Collect -Recordings "C:\...\Mesa Dual\recordings"
     downloads what is finished.

Running it again for the same amp trains only what is not finished yet (e.g. after the safety limit).

Once on this PC:
  - rclone.exe (rclone.org/downloads) - default C:\rclone.exe, or say where with -Rclone
  - the first run asks for your RunPod API key (Settings / Credentials - API Keys, permission "All") and keeps it
    encrypted for your Windows user in %USERPROFILE%\.ampsurd\runpod_key.txt
  - the first run connects rclone to your Google Drive (a browser window: allow access). rclone only sees the files
    it created itself.

Options: -Machines 3 -PerMachine 3 -Epochs 400 -MaxHours 8  -InputFile <path to T3K-sweep-v3.wav>  -NoWait
#>
param(
    [Parameter(Mandatory = $true)][string]$Amp,
    [string]$Recordings = "",
    [string]$InputFile = "",
    [int]$Machines = 3,
    [int]$PerMachine = 3,
    [int]$Epochs = 400,
    [int]$MaxHours = 8,
    [string]$Rclone = "C:\rclone.exe",
    [switch]$Collect,
    [switch]$NoWait
)

$ErrorActionPreference = "Stop"
$Raw = "https://raw.githubusercontent.com/SymbiOrbis/ampsurd/main/tools/nam_training"
$Api = "https://rest.runpod.io/v1"
$Image = "__IMAGE__"
# cheapest suitable first (RunPod takes the first one that is free); all are fast enough for NAM
$Gpus = @(
    "NVIDIA RTX A4500", "NVIDIA RTX A5000", "NVIDIA RTX 4000 Ada Generation", "NVIDIA GeForce RTX 3090",
    "__PRO4000__", "NVIDIA A40", "NVIDIA L4", "NVIDIA RTX A6000", "NVIDIA GeForce RTX 4090"
) | Where-Object { $_ -and -not $_.StartsWith("__") }
$Root = "gdrive:AMPSURD training"
$Slug = ($Amp.ToLower() -replace "[^a-z0-9]+", "-").Trim("-")

function Say($t) { Write-Host ("{0}  {1}" -f (Get-Date -Format "HH:mm"), $t) }
function Fail($t) { Write-Host $t -ForegroundColor Red; exit 1 }

# --- rclone + Google Drive ---
if (-not (Test-Path $Rclone)) { Fail "rclone.exe not found at $Rclone - download it from rclone.org/downloads or use -Rclone <path>" }
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
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR((Get-Content $KeyFile | ConvertTo-SecureString)))
$Headers = @{ Authorization = "Bearer $Key" }

function Get-AmpPods {
    $all = Invoke-RestMethod -Uri "$Api/pods" -Headers $Headers
    @($all | Where-Object { $_.name -like "ampsurd-$Slug-*" })
}

$NamDir = if ($Recordings) { Join-Path (Split-Path $Recordings -Parent) "NAMs" } else { Join-Path ([Environment]::GetFolderPath("MyDocuments")) "AMPSURD training\$Amp\NAMs" }
function Collect-Nams {
    New-Item -ItemType Directory -Force -Path $NamDir | Out-Null
    & $Rclone copy "$Root/$Amp/NAMs" $NamDir --include "*.nam" --include "summary*.txt" --include "logs/**"
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
$files = @(& $Rclone lsf "$Root/$Amp/recordings" --include "*.wav")
$done = @(& $Rclone lsf "$Root/$Amp/NAMs" --include "*.nam" 2>$null)
if ($files.Count -eq 0) { Fail "No recordings on Google Drive for '$Amp'." }
Say "$($files.Count) recordings, $($done.Count) already trained"
if ($done.Count -ge $files.Count) { Say "Nothing left to train."; Collect-Nams; exit 0 }

if ((Get-AmpPods).Count -gt 0) { Fail "Machines for '$Amp' are still running - wait for them, or check RunPod." }

# --- machines ---
$parts = [Math]::Min($Machines * $PerMachine, $files.Count - $done.Count)
$Machines = [Math]::Ceiling($parts / $PerMachine)
$tok = ((& $Rclone config dump) | ConvertFrom-Json).gdrive.token
Say "Renting $Machines machine(s), $PerMachine trainings each, $Epochs epochs, safety limit $MaxHours h ..."
for ($m = 1; $m -le $Machines; $m++) {
    $from = ($m - 1) * $PerMachine + 1
    $to = [Math]::Min($m * $PerMachine, $parts)
    $body = @{
        name = "ampsurd-$Slug-$m"
        imageName = $Image
        gpuTypeIds = @($Gpus)
        gpuTypePriority = "custom"
        gpuCount = 1
        containerDiskInGb = 30
        volumeInGb = 0
        minVCPUPerGPU = 8
        ports = @()
        env = @{
            AMP = $Amp; PART_FROM = "$from"; PART_TO = "$to"; PARTS = "$parts"; EPOCHS = "$Epochs"; MAX_HOURS = "$MaxHours"
            RCLONE_CONFIG_GDRIVE_TYPE = "drive"; RCLONE_CONFIG_GDRIVE_SCOPE = "drive.file"; RCLONE_CONFIG_GDRIVE_TOKEN = $tok
            AMPSURD_RUNPOD_KEY = $Key
        }
        dockerStartCmd = @("bash", "-c", "curl -fsSL $Raw/pod_job.sh -o /pod_job.sh && bash /pod_job.sh")
    }
    $pod = $null
    foreach ($cloud in @("SECURE", "COMMUNITY")) {
        $body.cloudType = $cloud
        try {
            $pod = Invoke-RestMethod -Method Post -Uri "$Api/pods" -Headers $Headers -ContentType "application/json" `
                -Body ($body | ConvertTo-Json -Depth 5)
            break
        } catch { Say "  no machine free in $cloud cloud ($($_.Exception.Message))" }
    }
    if (-not $pod) { Say "Machine $m could not be rented - run the command again later (finished work is kept)."; continue }
    $gpu = if ($pod.machine.gpuTypeId) { $pod.machine.gpuTypeId } else { "GPU" }
    $price = if ($pod.costPerHr) { " - `${0}/h" -f $pod.costPerHr } else { "" }
    Say "  machine $m ($gpu$price): parts $from-$to of $parts"
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
    $n = @(& $Rclone lsf "$Root/$Amp/NAMs" --include "*.nam" 2>$null).Count
    $pods = Get-AmpPods
    $running = $pods.Count
    $lines = @(); $worst = 0
    foreach ($p in ($pods | Sort-Object name)) {
        $st = & $Rclone cat "$Root/$Amp/NAMs/status/status_$($p.id).txt" 2>$null
        if (-not $st) { $st = "starting ..." }
        $eta = 0; [int]::TryParse(((& $Rclone cat "$Root/$Amp/NAMs/status/eta_$($p.id).txt" 2>$null) -join ""), [ref]$eta) | Out-Null
        if ($eta -gt $worst) { $worst = $eta }
        $lines += ("   machine {0}: {1}" -f ($p.name -replace "^ampsurd-$Slug-", ""), $st)
    }
    $left = ""
    if ($worst -gt 0) {
        $left = " - about {0} h {1} min left (around {2})" -f [Math]::Floor($worst / 3600), [Math]::Floor(($worst % 3600) / 60), (Get-Date).AddSeconds($worst).ToString("HH:mm")
    }
    Say "$n of $total trained, $running machine(s) running$left"
    $lines | ForEach-Object { Write-Host $_ }
} while ($running -gt 0)
Collect-Nams
Get-ChildItem $NamDir -Filter "summary*.txt" | ForEach-Object { Get-Content $_.FullName | Select-String "OK|FAILED|SKIPPED" }
