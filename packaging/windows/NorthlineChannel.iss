; Inno Setup script for the Windows installer (Inno Setup 6.3+).
; Build:  iscc /DAppVersion=1.0.0 /DArch=x64 /DArtefacts=..\..\build\NorthlineChannel_artefacts\Release packaging\windows\NorthlineChannel.iss
; Arch is x64 or arm64. Sign the plugin binaries before packaging (sign.ps1), then sign the installer.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\NorthlineChannel_artefacts\Release"
#endif

[Setup]
AppId={{5B7C3A52-2E0B-4C55-9A0D-6F3E1D2B9C41}
AppName=Northline Channel
AppVersion={#AppVersion}
AppPublisher=Northline Audio
DefaultDirName={autopf}\Northline Audio\Northline Channel
DefaultGroupName=Northline Audio
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=NorthlineChannel-{#AppVersion}-win-{#Arch}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
UninstallDisplayName=Northline Channel {#AppVersion}

[Types]
Name: "full"; Description: "All formats"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
#if FileExists(Artefacts + "\CLAP\Northline Channel.clap")
Name: "clap"; Description: "CLAP plug-in"; Types: full custom
#endif
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\Northline Channel.vst3\*"; DestDir: "{commoncf64}\VST3\Northline Channel.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
#if FileExists(Artefacts + "\CLAP\Northline Channel.clap")
Source: "{#Artefacts}\CLAP\Northline Channel.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
#endif
Source: "{#Artefacts}\Standalone\Northline Channel.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Northline Channel"; Filename: "{app}\Northline Channel.exe"; Components: standalone
Name: "{group}\Uninstall Northline Channel"; Filename: "{uninstallexe}"

; User presets live in Documents\Northline Audio\Northline Channel\Presets and are never touched
; by the installer or uninstaller.
