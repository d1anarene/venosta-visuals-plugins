; Film Effects for DaVinci Resolve (OpenFX) — Windows installer (Inno Setup 6).
; Built in CI (.github/workflows/film-effects-ofx.yml):
;   ISCC /DAppVersion=1.0.7 /DBundleDir=<FilmEffects.ofx.bundle> /DRepoRoot=<repo> /DOutputDir=<out> FilmEffectsResolve.iss
; Installs FilmEffects.ofx.bundle into the standard OpenFX folder
; C:\Program Files\Common Files\OFX\Plugins, where DaVinci Resolve looks for OFX plug-ins.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change AppId (separate product from the Premiere installer).
AppId={{5d0c1f4e-7a7b-4f43-9a55-2f3cbb8c6e21}
AppName=Film Effects for DaVinci Resolve
AppVersion={#AppVersion}
AppVerName=Film Effects for DaVinci Resolve {#AppVersion}
AppPublisher=Venosta Visuals
AppPublisherURL=https://venostavisuals.com
AppSupportURL=https://github.com/d1anarene/venosta-visuals-plugins
DefaultDirName={commoncf64}\OFX\Plugins
UsePreviousAppDir=no
UninstallFilesDir={commonpf64}\Venosta Visuals\Film Effects for Resolve
DisableDirPage=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallDisplayName=Film Effects for DaVinci Resolve (Venosta Visuals)
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=FilmEffects-Resolve-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}\FilmEffects.ofx.bundle"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RepoRoot}\LICENSE"; DestDir: "{commonpf64}\Venosta Visuals\Film Effects for Resolve"; DestName: "LICENSE.txt"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{app}\FilmEffects.ofx.bundle\Contents\Win64"
Type: dirifempty; Name: "{app}\FilmEffects.ofx.bundle\Contents"
Type: dirifempty; Name: "{app}\FilmEffects.ofx.bundle"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals\Film Effects for Resolve"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals"

[Messages]
FinishedLabel=Film Effects is installed. Start (or restart) DaVinci Resolve; the effects are in the Effects library under OpenFX > Venosta Visuals.
