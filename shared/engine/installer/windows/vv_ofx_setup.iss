[Setup]
; (Starts with a section header so it can be #included after a plugin's own [Code] section.)
; Venosta Visuals engine — Windows installer template for an OpenFX plug-in (DaVinci Resolve),
; Inno Setup 6. A plugin's own .iss defines the values below and #includes this file last.
; Built in CI by .github/workflows/vv-ofx.yml.
;
; Required defines: VV_APP_ID ("{{GUID}" — the doubled brace escapes Inno's constant syntax), VV_NAME (e.g. "Film Effects"),
;   VV_BUNDLE (e.g. "FilmEffects"), BundleDir (the <VV_BUNDLE>.ofx.bundle folder), RepoRoot.
; Optional: AppVersion, OutputDir. A file NVIDIA-CUDA-EULA.html next to the bundle is shipped too.
;
; Installs <VV_BUNDLE>.ofx.bundle into the standard OpenFX folder C:\Program Files\Common Files\OFX\
; Plugins. The bundle may hold Contents\Win64 (x64) and Contents\Win-arm64ec (Windows on Arm);
; Resolve loads the folder for its own architecture.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change a plugin's AppId (separate product from its Premiere installer).
AppId={#VV_APP_ID}
AppName={#VV_NAME} for DaVinci Resolve
AppVersion={#AppVersion}
AppVerName={#VV_NAME} for DaVinci Resolve {#AppVersion}
AppPublisher=Venosta Visuals
AppPublisherURL=https://venostavisuals.com
AppSupportURL=https://github.com/d1anarene/venosta-visuals-plugins
DefaultDirName={commoncf64}\OFX\Plugins
UsePreviousAppDir=no
UninstallFilesDir={commonpf64}\Venosta Visuals\{#VV_NAME} for Resolve
DisableDirPage=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallDisplayName={#VV_NAME} for DaVinci Resolve (Venosta Visuals)
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#VV_BUNDLE}-Resolve-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}\{#VV_BUNDLE}.ofx.bundle"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RepoRoot}\LICENSE"; DestDir: "{commonpf64}\Venosta Visuals\{#VV_NAME} for Resolve"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#BundleDir}\..\NVIDIA-CUDA-EULA.html"; DestDir: "{commonpf64}\Venosta Visuals\{#VV_NAME} for Resolve"; Flags: ignoreversion skipifsourcedoesntexist

[UninstallDelete]
Type: dirifempty; Name: "{app}\{#VV_BUNDLE}.ofx.bundle\Contents\Win64"
Type: dirifempty; Name: "{app}\{#VV_BUNDLE}.ofx.bundle\Contents\Win-arm64ec"
Type: dirifempty; Name: "{app}\{#VV_BUNDLE}.ofx.bundle\Contents"
Type: dirifempty; Name: "{app}\{#VV_BUNDLE}.ofx.bundle"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals\{#VV_NAME} for Resolve"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals"

[Messages]
FinishedLabel={#VV_NAME} is installed. Start (or restart) DaVinci Resolve; it is in the Effects library under OpenFX > Venosta Visuals.
