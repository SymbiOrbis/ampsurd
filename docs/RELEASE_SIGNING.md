# Signing a Windows release (on Radovan's PC)

GitHub publishes every release **unsigned**. Then you sign it on your PC with one command; the script
replaces the Windows files of the release with signed ones and changes the warning at the top of the
release notes to "Signed by Open Source Developer Radovan Glovňa (Certum)". macOS files stay as they are.

## Once: install two tools

Windows SDK signtool is already installed. In PowerShell:

1. `winget install --id JRSoftware.InnoSetup -e` (Inno Setup 6, builds the installer)
2. `winget install --id GitHub.cli -e` (GitHub CLI, downloads and uploads the release files)
3. Close PowerShell, open it again, then `gh auth login` -> GitHub.com -> HTTPS -> log in with a web
   browser. (`gh auth status` shows that it worked.)

## Every release (e.g. 0.9.1)

1. Wait until GitHub has published the release (Releases page shows `vX.Y.Z` with the unsigned files).
2. Open **SimplySign Desktop** and log in (code from the SimplySign app on your phone). Leave it running.
3. In PowerShell, get the newest script and run it:

       cd $HOME
       irm https://raw.githubusercontent.com/SymbiOrbis/ampsurd/main/installer/sign_release.ps1 -OutFile sign_release.ps1
       powershell -ExecutionPolicy Bypass -File .\sign_release.ps1 -Version 0.9.1

   If SimplySign asks to confirm a signature, confirm it (it signs about five files).
4. Result:
   - green **DONE** -> finished.
   - red **STOPPED: ...** -> nothing was changed on GitHub (unless the message says "the signed files ARE
     uploaded"). Copy the red text and the lines above it and send them to Claude.
     Most common cause: SimplySign is not logged in (log in, run step 3 again).
5. Check:
   - the release page starts with "Windows: Signed by Open Source Developer Radovan Glovňa (Certum) ...";
   - download `AMPSURD-Setup-<version>.exe` from the release page -> right-click -> Properties ->
     **Digital Signatures**: "Open Source Developer Radovan Glovňa", with a timestamp -> Details ->
     "This digital signature is OK";
   - optional: install it on a PC with Smart App Control turned on.

Trial run without touching GitHub: add `-NoUpload` (the signed files end up in
`%USERPROFILE%\AMPSURD-signing\<version>\signed`). Running the script twice for the same version is
fine: it signs again and replaces the files again.

The certificate is valid until **2027-10-19**. Releases signed before that stay valid afterwards,
because every signature carries a Certum timestamp. Renew the certificate before that date.
