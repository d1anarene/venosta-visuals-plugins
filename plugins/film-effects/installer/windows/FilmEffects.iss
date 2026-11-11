; Film Effects — Windows installer (Inno Setup 6).
; Built in CI (.github/workflows/film-effects-plugins.yml) from the CPU and CUDA plugin builds:
;   ISCC /DAppVersion=1.0.6 /DCpuDir=<cpu .aex folder> /DCudaDir=<cuda .aex folder>
;        /DRepoRoot=<repository root> /DOutputDir=<out> FilmEffects.iss
; Installs Film Effects, Film Bloom, Film Halation and Film Grain into Adobe's shared MediaCore
; plug-in folder (Premiere Pro SDK guide, "Plugin Installation"), with an entry in
; Settings > Apps for uninstalling. The CUDA build is pre-selected when an NVIDIA driver is
; present (nvcuda.dll), the CPU-only build otherwise; the user can change the choice.

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
DefaultDirName={commonpf64}\Adobe\Common\Plugins\7.0\MediaCore
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

[Types]
Name: "cuda"; Description: "NVIDIA graphics card (CUDA GPU acceleration)"
Name: "cpu"; Description: "Any PC (CPU only)"

[Components]
Name: "cuda"; Description: "Film Effects with NVIDIA CUDA acceleration (falls back to CPU automatically)"; Types: cuda; Flags: exclusive
Name: "cpu"; Description: "Film Effects, CPU only"; Types: cpu; Flags: exclusive

[Files]
Source: "{#CudaDir}\*.aex"; DestDir: "{app}"; Components: cuda; Flags: ignoreversion
Source: "{#CpuDir}\*.aex"; DestDir: "{app}"; Components: cpu; Flags: ignoreversion
; Licence notices go to a normal program folder, not into Adobe's plug-in folder.
Source: "{#RepoRoot}\LICENSE"; DestDir: "{commonpf64}\Venosta Visuals\Film Effects"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#CudaDir}\NVIDIA-CUDA-EULA.html"; DestDir: "{commonpf64}\Venosta Visuals\Film Effects"; Components: cuda; Flags: ignoreversion skipifsourcedoesntexist

[UninstallDelete]
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals\Film Effects"
Type: dirifempty; Name: "{commonpf64}\Venosta Visuals"

[Messages]
FinishedLabel=Film Effects is installed. Start (or restart) Premiere Pro and look in Effects > Video Effects > Venosta Visuals.

[Code]
function HasNvidiaDriver(): Boolean;
begin
  { The NVIDIA display driver installs the CUDA driver library into System32. }
  Result := FileExists(ExpandConstant('{sys}\nvcuda.dll'));
end;

procedure InitializeWizard();
begin
  if not HasNvidiaDriver() then
  begin
    { Pre-select "Any PC (CPU only)": second entry of the type list. }
    WizardForm.TypesCombo.ItemIndex := 1;
    WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
    WizardSelectComponents('!cuda,cpu');
  end;
end;
