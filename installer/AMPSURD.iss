; AMPSURD installer (Inno Setup 6). Built by GitHub Actions:
;   ISCC.exe /DAppVersion=0.9.5 /DSourceDir=<build artefacts> installer\AMPSURD.iss
; Installs the VST3 plugin (C:\Program Files\Common Files\VST3) and the standalone app (Program Files),
; Start-menu entry, optional desktop shortcut, uninstaller. Your presets and recordings in
; Documents\AMPSURD are never touched (also not by the uninstaller).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\plugin\Ampsurd_artefacts\Release"
#endif

[Setup]
AppId={{6E3B1C0A-5A8E-4F3B-9C71-2B7D4E0F9A15}
AppName=AMPSURD
AppVersion={#AppVersion}
AppVerName=AMPSURD {#AppVersion}
AppPublisher=Radovan Glovňa
AppPublisherURL=https://github.com/SymbiOrbis/ampsurd
AppSupportURL=https://github.com/SymbiOrbis/ampsurd/issues
AppUpdatesURL=https://github.com/SymbiOrbis/ampsurd/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=AMPSURD
DefaultDirName={autopf}\AMPSURD
DefaultGroupName=AMPSURD
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\LICENSE.md
SetupIconFile=..\plugin\assets\icon\ampsurd.ico
UninstallDisplayIcon={app}\AMPSURD.exe
UninstallDisplayName=AMPSURD {#AppVersion}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=output
; the same name for every version: the website links to .../releases/latest/download/AMPSURD-Windows-Setup.exe
OutputBaseFilename=AMPSURD-Windows-Setup
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "VST3 plugin + standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (for REAPER and other DAWs)"; Types: full custom
Name: "app"; Description: "Standalone app (with player / recorder)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: app; Flags: unchecked

[Files]
Source: "{#SourceDir}\Standalone\AMPSURD.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\VST3\AMPSURD.vst3\*"; DestDir: "{commoncf64}\VST3\AMPSURD.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\AMPSURD"; Filename: "{app}\AMPSURD.exe"; Components: app
Name: "{group}\Uninstall AMPSURD"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AMPSURD"; Filename: "{app}\AMPSURD.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AMPSURD.exe"; Description: "Start AMPSURD"; Components: app; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\AMPSURD.vst3"
