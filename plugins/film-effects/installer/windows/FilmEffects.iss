; Film Effects — Windows installer (Inno Setup 6).
; Built in CI (.github/workflows/film-effects-plugins.yml) from the CPU and CUDA plugin builds:
;   ISCC /DAppVersion=1.0.10 /DCudaDir=<CUDA-build .aex folder> /DRepoRoot=<repository root>
;        /DOutputDir=<out> FilmEffects.iss
; One build for every PC: the CUDA build contains the full CPU path and loads without an NVIDIA
; driver (the CUDA runtime is linked statically and looks for the driver only at run time), so
; Premiere renders on the CPU whenever CUDA is not available.
; Installs Film Effects, Film Bloom, Film Halation and Film Grain into Adobe's shared MediaCore
; plug-in folder: the Plug-InsDir registry value Premiere writes
; (HKLM\SOFTWARE\Adobe\Premiere Pro\CurrentVersion), falling back to
; C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore (WITH a hyphen). The uninstaller lives in
; C:\Program Files\Venosta Visuals\Film Effects, not in Adobe's folder; Settings > Apps lists it.
; v1.0.6 wrongly used ...\Common\Plugins\7.0\MediaCore (no hyphen); upgrades clean that up.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change AppId: it lets newer versions upgrade and uninstall older ones.
AppId={{98bb9bda-fede-4855-bd24-e70984a4a237}
AppName=Film Effects for Premiere Pro
AppVersion={#AppVersion}
AppVerName=Film Effects {#AppVersion}
AppPublisher=Venosta Visuals
AppPublisherURL=https://venostavisuals.com
AppSupportURL=https://github.com/d1anarene/venosta-visuals-plugins
DefaultDirName={code:GetPluginDir}
; Never reuse the folder of a previous install: v1.0.6 recorded the wrong one.
UsePreviousAppDir=no
UninstallFilesDir={commonpf64}\Venosta Visuals\Film Effects
DisableDirPage=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallDisplayName=Film Effects for Premiere Pro (Venosta Visuals)
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=FilmEffects-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#CudaDir}\*.aex"; DestDir: "{app}"; Flags: ignoreversion
; Licence notices go to a normal program folder, not into Adobe's plug-in folder.
Source: "{#RepoRoot}\LICENSE"; DestDir: "{commonpf64}\Venosta Visuals\Film Effects"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#CudaDir}\NVIDIA-CUDA-EULA.html"; DestDir: "{commonpf64}\Venosta Visuals\Film Effects"; Flags: ignoreversion skipifsourcedoesntexist

[InstallDelete]
; Clean up the wrong folder used by v1.0.6 (only when our files are found there).
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmEffects.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmBloom.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmHalation.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmGrain.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\unins000.exe"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\unins000.dat"; Check: HasStrayInstall
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore"
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins\7.0"
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins"

[UninstallDelete]
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals\Film Effects"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals"

[Messages]
FinishedLabel=Film Effects is installed. Start (or restart) Premiere Pro and look in Effects > Video Effects > Venosta Visuals.

[Code]
const
  StrayDir = '{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore';

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

var
  StrayFound: Boolean;

function InitializeSetup(): Boolean;
begin
  { Decide once: [InstallDelete] deletes FilmEffects.aex first, so re-checking per entry
    would skip the remaining stray files. }
  StrayFound := FileExists(ExpandConstant(StrayDir + '\FilmEffects.aex'));
  Result := True;
end;

function HasStrayInstall(): Boolean;
begin
  Result := StrayFound;
end;
