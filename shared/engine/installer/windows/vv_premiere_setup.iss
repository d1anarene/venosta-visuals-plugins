[Setup]
; (Starts with a section header so it can be #included after a plugin's own [Code] section.)
; Venosta Visuals engine — Windows installer template for a Premiere Pro effect (Inno Setup 6).
; A plugin's own .iss defines the values below, may add its own sections (e.g. [InstallDelete]
; for files of older versions, a [Code] procedure named by VV_EXTRA_INIT), then #includes this file
; LAST. Built in CI by .github/workflows/vv-premiere.yml.
;
; Required defines: VV_APP_ID ("{{GUID}" — the doubled brace escapes Inno's constant syntax), VV_NAME (e.g. "Film Effects"),
;   VV_BINARY (e.g. "FilmEffects"), VV_X64_DIR (folder with <VV_BINARY>.aex for x64),
;   RepoRoot. Optional: VV_ARM64_DIR (Windows on Arm build), AppVersion, OutputDir, VV_EXTRA_INIT.
;
; Installs <VV_BINARY>.aex into Premiere's shared plug-in folder: the Plug-InsDir registry value
; (HKLM\SOFTWARE\Adobe\Premiere Pro\CurrentVersion), else C:\Program Files\Adobe\Common\Plug-ins\
; 7.0\MediaCore (WITH a hyphen). On Windows on Arm the ARM64 build is installed when provided.
; The uninstaller lives in C:\Program Files\Venosta Visuals\<VV_NAME>; Settings > Apps lists it.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change a plugin's AppId: it lets newer versions upgrade and uninstall older ones.
AppId={#VV_APP_ID}
AppName={#VV_NAME} for Premiere Pro
AppVersion={#AppVersion}
AppVerName={#VV_NAME} {#AppVersion}
AppPublisher=Venosta Visuals
AppPublisherURL=https://venostavisuals.com
AppSupportURL=https://github.com/d1anarene/venosta-visuals-plugins
DefaultDirName={code:GetPluginDir}
UsePreviousAppDir=no
UninstallFilesDir={commonpf64}\Venosta Visuals\{#VV_NAME}
DisableDirPage=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallDisplayName={#VV_NAME} for Premiere Pro (Venosta Visuals)
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#VV_BINARY}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
#ifdef VV_ARM64_DIR
Source: "{#VV_X64_DIR}\{#VV_BINARY}.aex"; DestDir: "{app}"; Check: not IsArm64; Flags: ignoreversion
Source: "{#VV_ARM64_DIR}\{#VV_BINARY}.aex"; DestDir: "{app}"; Check: IsArm64; Flags: ignoreversion
#else
Source: "{#VV_X64_DIR}\{#VV_BINARY}.aex"; DestDir: "{app}"; Flags: ignoreversion
#endif
; Licence notices go to a normal program folder, not into Adobe's plug-in folder.
Source: "{#RepoRoot}\LICENSE"; DestDir: "{commonpf64}\Venosta Visuals\{#VV_NAME}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; Builds with the statically linked CUDA runtime ship NVIDIA's EULA (x64 only).
Source: "{#VV_X64_DIR}\NVIDIA-CUDA-EULA.html"; DestDir: "{commonpf64}\Venosta Visuals\{#VV_NAME}"; Check: not IsArm64; Flags: ignoreversion skipifsourcedoesntexist

[UninstallDelete]
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals\{#VV_NAME}"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals"

[Messages]
FinishedLabel={#VV_NAME} is installed. Start (or restart) Premiere Pro and look in Effects > Video Effects > Venosta Visuals.

[Code]
function GetPluginDir(Param: String): String;
var
  Dir: String;
begin
  { Premiere records its shared plug-in folder here; use it when it exists. }
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Adobe\Premiere Pro\CurrentVersion', 'Plug-InsDir', Dir)
     and (Trim(Dir) <> '') and DirExists(RemoveBackslashUnlessRoot(Trim(Dir))) then
    Result := RemoveBackslashUnlessRoot(Trim(Dir))
  else
    Result := ExpandConstant('{commonpf64}\Adobe\Common\Plug-ins\7.0\MediaCore');
end;

function InitializeSetup(): Boolean;
begin
#ifdef VV_EXTRA_INIT
  {#VV_EXTRA_INIT}();
#endif
  Result := True;
end;
