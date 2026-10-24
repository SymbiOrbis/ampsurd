# AMPSURD: sign a Windows release on the maintainer's PC (Certum certificate via SimplySign Desktop).
#
#   powershell -ExecutionPolicy Bypass -File sign_release.ps1 -Version 0.9.1
#
# What it does (stops at the first error, nothing is uploaded before every check passed):
#   1. downloads the unsigned Windows files of release v<Version> (AMPSURD-Windows.zip: VST3 + standalone;
#      releases before 2026-10-24 had two zips with the version in the name - also understood) and the
#      source code of that tag (for the installer script) from GitHub,
#   2. signs AMPSURD.exe and the plugin DLL inside AMPSURD.vst3,
#   3. builds AMPSURD-Windows-Setup.exe with the local Inno Setup 6, which signs Setup, Setup's
#      temporary copy and the uninstaller through its SignTool setting,
#   4. checks every signature (signtool verify /pa /all, Certum signer, timestamp),
#   5. zips the signed files (AMPSURD-Windows.zip; fixed names: the website links to
#      .../releases/latest/download/AMPSURD-Windows-Setup.exe and .../AMPSURD-Windows.zip),
#   6. replaces the Windows files of the GitHub release with the signed ones and changes the
#      "NOT code-signed" warning in the release notes to "Signed by ... (Certum)".
#      macOS files of the release are left alone.
#
# Needs: SimplySign Desktop logged in, Windows SDK signtool, Inno Setup 6, GitHub CLI (gh auth login).
# Steps for the maintainer: docs/RELEASE_SIGNING.md.
# Only ASCII in this file: Windows PowerShell 5.1 reads scripts without BOM as ANSI.

param(
    [Parameter(Mandatory = $true)][string]$Version,   # e.g. 0.9.1 (tag v0.9.1)
    [string]$Repo = "SymbiOrbis/ampsurd",
    [string]$WorkDir = (Join-Path $env:USERPROFILE "AMPSURD-signing"),
    [switch]$NoUpload                                  # build and check only, do not touch GitHub
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"               # Invoke-WebRequest / Expand-Archive much faster
[Console]::OutputEncoding = [Text.Encoding]::UTF8      # gh prints UTF-8 (release notes contain the author's name)

$Version = $Version.TrimStart("v", "V")
$Tag = "v$Version"
$TimestampUrl = "http://time.certum.pl"
$SignerPrefix = "Open Source Developer Radovan Glov"   # subject: "Open Source Developer Radovan Glov<n-caron>a"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Utf8Bom = New-Object System.Text.UTF8Encoding($true)

function Step([string]$text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Fail([string]$text) { Write-Host ""; Write-Host "STOPPED: $text" -ForegroundColor Red; exit 1 }
function Run([string]$exe, [string[]]$arguments, [string]$what) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE)" }
}

# ---------------------------------------------------------------- tools
Step "Checking the tools"
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) { Fail "signtool.exe not found (Windows SDK signing tools)" }
Write-Host "signtool: $signtool"

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { Fail "Inno Setup 6 not found. Install it: winget install --id JRSoftware.InnoSetup -e" }
Write-Host "Inno Setup: $iscc"

$gh = (Get-Command gh.exe -ErrorAction SilentlyContinue).Source
if (-not $gh) { $gh = @("$env:ProgramFiles\GitHub CLI\gh.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1 }
if (-not $gh) { Fail "GitHub CLI not found. Install it: winget install --id GitHub.cli -e   then: gh auth login" }
& $gh auth status *> $null
if ($LASTEXITCODE -ne 0) { Fail "GitHub CLI is not logged in. Run: gh auth login" }
Write-Host "GitHub CLI: $gh (logged in)"

# ---------------------------------------------------------------- certificate (SimplySign)
Step "Looking for the Certum certificate (SimplySign Desktop must be logged in)"
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -like "*$SignerPrefix*" -and $_.NotAfter -gt (Get-Date) } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if ($cert) {
    Write-Host "Certificate: $($cert.Subject)  valid until $($cert.NotAfter.ToString('yyyy-MM-dd'))  ($($cert.Thumbprint))"
    $certArgs = @("/sha1", $cert.Thumbprint)
} else {
    Write-Host "Certificate not visible in the Windows certificate store - using signtool /a (automatic choice)." -ForegroundColor Yellow
    Write-Host "If signing fails: open SimplySign Desktop and log in, then run this script again." -ForegroundColor Yellow
    $certArgs = @("/a")
}
$signArgs = @("sign") + $certArgs + @("/fd", "sha256", "/tr", $TimestampUrl, "/td", "sha256", "/d", "AMPSURD")

function Sign-File([string]$file) {
    Write-Host "signing $file"
    Run $signtool ($signArgs + @($file)) "signing $file"
}

function Verify-File([string]$file) {
    Write-Host "verifying $file"
    Run $signtool @("verify", "/pa", "/all", $file) "signtool verify of $file"
    $sig = Get-AuthenticodeSignature -FilePath $file
    if ($sig.Status -ne "Valid") { Fail "signature of $file is $($sig.Status)" }
    if ($sig.SignerCertificate.Subject -notlike "*$SignerPrefix*") { Fail "$file is signed by $($sig.SignerCertificate.Subject), not by the AMPSURD certificate" }
    if (-not $sig.TimeStamperCertificate) { Fail "$file has no timestamp (it would stop being valid when the certificate expires)" }
}

# ---------------------------------------------------------------- download
Step "Downloading release $Tag from GitHub ($Repo)"
$work = Join-Path $WorkDir $Version
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
$dl = Join-Path $work "download"
New-Item -ItemType Directory -Force -Path $dl | Out-Null

$winZipName = "AMPSURD-Windows.zip"                  # since 2026-10-24: VST3 + standalone in one zip
$oldVstZip = "AMPSURD-$Version-VST3-windows-x64.zip"   # older releases
$oldAppZip = "AMPSURD-$Version-Standalone-windows-x64.zip"
$oldSetup = "AMPSURD-Setup-$Version.exe"
$setupName = "AMPSURD-Windows-Setup.exe"
$ErrorActionPreference = "Continue"                   # "not found" here is not an error (an older release)
& $gh release download $Tag --repo $Repo --dir $dl --pattern $winZipName 2>&1 | Out-Null
$ErrorActionPreference = "Stop"
$oldNames = -not (Test-Path (Join-Path $dl $winZipName))
if ($oldNames) {
    Run $gh @("release", "download", $Tag, "--repo", $Repo, "--dir", $dl, "--pattern", $oldVstZip, "--pattern", $oldAppZip) "downloading the Windows zips of $Tag"
}
Run $gh @("release", "download", $Tag, "--repo", $Repo, "--dir", $dl, "--archive", "zip") "downloading the source code of $Tag"

$bin = Join-Path $work "bin"                           # same layout as the CI build output
New-Item -ItemType Directory -Force -Path (Join-Path $bin "VST3"), (Join-Path $bin "Standalone") | Out-Null
if ($oldNames) {
    Expand-Archive (Join-Path $dl $oldVstZip) -DestinationPath (Join-Path $bin "VST3")
    Expand-Archive (Join-Path $dl $oldAppZip) -DestinationPath (Join-Path $bin "Standalone")
} else {
    $unz = Join-Path $work "unzipped"
    Expand-Archive (Join-Path $dl $winZipName) -DestinationPath $unz
    Move-Item (Join-Path $unz "AMPSURD.vst3") (Join-Path $bin "VST3")
    Move-Item (Join-Path $unz "AMPSURD.exe") (Join-Path $bin "Standalone")
}
$srcZip = Get-ChildItem $dl -Filter "*.zip" | Where-Object { $_.Name -ne $winZipName -and $_.Name -ne $oldVstZip -and $_.Name -ne $oldAppZip } | Select-Object -First 1
Expand-Archive $srcZip.FullName -DestinationPath (Join-Path $work "src")
$src = Get-ChildItem (Join-Path $work "src") -Directory | Select-Object -First 1 -ExpandProperty FullName

$appExe = Join-Path $bin "Standalone\AMPSURD.exe"
$vstDir = Join-Path $bin "VST3\AMPSURD.vst3"
if (-not (Test-Path $appExe)) { Fail "AMPSURD.exe missing in the downloaded zip" }
$vstDlls = @(Get-ChildItem (Join-Path $vstDir "Contents") -Recurse -File -Include "*.vst3", "*.dll", "*.exe")
if ($vstDlls.Count -eq 0) { Fail "no plugin DLL found inside AMPSURD.vst3" }

# ---------------------------------------------------------------- sign the program files
Step "Signing AMPSURD.exe and the VST3 plugin"
Sign-File $appExe
foreach ($f in $vstDlls) { Sign-File $f.FullName }

# ---------------------------------------------------------------- installer
Step "Building the installer with Inno Setup (signs Setup, its temporary copy and the uninstaller)"
$iss = Join-Path $src "installer\AMPSURD.iss"
$issText = [IO.File]::ReadAllText($iss)
$setupHead = "(?m)^\[Setup\][ \t]*(?=\r?$)"
if ($issText -notmatch $setupHead) { Fail "[Setup] section not found in $iss" }
$issText = [regex]::Replace($issText, $setupHead, "[Setup]`r`nSignTool=ampsurdsign`r`nSignedUninstaller=yes", 1)
[IO.File]::WriteAllText($iss, $issText, $Utf8Bom)

# Inno replaces $q with a quote and $f with the quoted file name (single quotes: no PowerShell expansion)
$innoSign = 'ampsurdsign=$q' + $signtool + '$q ' + ($signArgs -join " ") + ' $f'
Run $iscc @("/Qp", "/DAppVersion=$Version", "/DSourceDir=$bin", "/S$innoSign", $iss) "Inno Setup"
$setupExe = Join-Path $src "installer\output\$setupName"
if (-not (Test-Path $setupExe)) {
    # an older tag's installer script still puts the version into the name: rename it
    $built = Join-Path $src "installer\output\$oldSetup"
    if (Test-Path $built) { Move-Item $built $setupExe } else { Fail "$setupName was not built" }
}

# ---------------------------------------------------------------- verify everything
Step "Verifying every signature"
Verify-File $appExe
foreach ($f in $vstDlls) { Verify-File $f.FullName }
Verify-File $setupExe

# ---------------------------------------------------------------- zips
Step "Making the zips from the signed files"
$out = Join-Path $work "signed"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $setupExe $out
Compress-Archive -Path $vstDir, $appExe -DestinationPath (Join-Path $out $winZipName)
Get-ChildItem $out | Format-Table Name, Length -AutoSize | Out-String | Write-Host

if ($NoUpload) {
    Write-Host "Signed and checked. -NoUpload: GitHub not changed. Files: $out" -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- upload
Step "Replacing the Windows files of release $Tag with the signed ones"
$files = @(Get-ChildItem $out | ForEach-Object { $_.FullName })
Run $gh (@("release", "upload", $Tag, "--repo", $Repo, "--clobber") + $files) "uploading to release $Tag"
if ($oldNames) {
    # the release had the old, versioned names: the unsigned old files go, the signed ones have the fixed names
    $ErrorActionPreference = "Continue"
    foreach ($n in @($oldVstZip, $oldAppZip, $oldSetup)) { & $gh release delete-asset $Tag $n --repo $Repo --yes 2>&1 | Out-Null }
    $ErrorActionPreference = "Stop"
}

Step "Release notes: 'NOT code-signed' warning -> 'Signed by ... (Certum)'"
$body = (& $gh api "repos/$Repo/releases/tags/$Tag" --jq ".body") -join "`n"
if ($LASTEXITCODE -ne 0) { Fail "could not read the release notes (the signed files ARE uploaded)" }
$author = "Radovan Glov" + [char]0x0148 + "a"
$signedText = "Windows: Signed by Open Source Developer $author (Certum) - the installer, the app and the VST3 plugin.`n" +
              "Windows SmartScreen may still show `"Windows protected your PC`" for a while (a new certificate first has to build" +
              " a reputation): More info, Run anyway."
$pattern = "(?s)Windows warning: this beta is NOT code-signed.*?More info, Run anyway\."
if ($body -match [regex]::Escape("Signed by Open Source Developer")) {
    Write-Host "The release notes already say it is signed - unchanged."
} elseif ($body -match $pattern) {
    $newBody = [regex]::Replace($body, $pattern, $signedText.Replace('$', '$$'), 1)
    $notes = Join-Path $work "release-notes.md"
    [IO.File]::WriteAllText($notes, $newBody, $Utf8NoBom)
    Run $gh @("release", "edit", $Tag, "--repo", $Repo, "--notes-file", $notes) "updating the release notes (the signed files ARE uploaded)"
} else {
    Write-Host "The 'NOT code-signed' warning was not found in the release notes - edit them by hand on GitHub." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "DONE: release $Tag now has signed Windows files: https://github.com/$Repo/releases/tag/$Tag" -ForegroundColor Green
