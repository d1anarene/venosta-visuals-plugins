; Film Effects — Windows installer for Premiere Pro, on the shared template
; (shared/engine/installer/windows/vv_premiere_setup.iss). Built in CI:
;   ISCC /DAppVersion=1.0.x /DX64Dir=<x64 CUDA-build folder> /DArm64Dir=<ARM64 folder>
;        /DRepoRoot=<repository root> /DOutputDir=<out> FilmEffects.iss
; One x64 build for every PC (CUDA with automatic CPU fallback); native ARM64 (CPU) on Windows on Arm.
; Film-specific additions: clean-up of older versions (below).

#define VV_APP_ID "{{98bb9bda-fede-4855-bd24-e70984a4a237}"
#define VV_NAME "Film Effects"
#define VV_BINARY "FilmEffects"
#define VV_X64_DIR X64Dir
#ifdef Arm64Dir
  #define VV_ARM64_DIR Arm64Dir
#endif
#define VV_EXTRA_INIT "FilmEffectsInit"

[InstallDelete]
; Standalone Film Bloom / Film Halation / Film Grain (until v1.0.11) are now part of Film Effects.
Type: files; Name: "{app}\FilmBloom.aex"
Type: files; Name: "{app}\FilmHalation.aex"
Type: files; Name: "{app}\FilmGrain.aex"
; Clean up the wrong folder used by v1.0.6 (no hyphen; only when our files are found there).
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmEffects.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmBloom.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmHalation.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\FilmGrain.aex"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\unins000.exe"; Check: HasStrayInstall
Type: files; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore\unins000.dat"; Check: HasStrayInstall
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore"
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins\7.0"
Type: dirifempty; Name: "{commonpf64}\Adobe\Common\Plugins"

[Code]
const
  StrayDir = '{commonpf64}\Adobe\Common\Plugins\7.0\MediaCore';
var
  StrayFound: Boolean;

procedure FilmEffectsInit();
begin
  { Decide once: [InstallDelete] deletes FilmEffects.aex first, so re-checking per entry
    would skip the remaining stray files. }
  StrayFound := FileExists(ExpandConstant(StrayDir + '\FilmEffects.aex'));
end;

function HasStrayInstall(): Boolean;
begin
  Result := StrayFound;
end;

#include "..\..\..\..\shared\engine\installer\windows\vv_premiere_setup.iss"
