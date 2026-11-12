# Film Effects only: upgrading over older installs. Simulates (a) v1.0.6, which used the wrong
# folder Common\Plugins (no hyphen), and (b) v1.0.11 and earlier, which also installed the
# standalone Film Bloom / Film Halation / Film Grain effects. After the upgrade only FilmEffects.aex
# (the x64 build) may remain, and the wrong folder must be gone.
param(
  [Parameter(Mandatory = $true)][string]$Setup,
  [Parameter(Mandatory = $true)][string]$X64Dir
)
$ErrorActionPreference = 'Stop'
$fallback = "$env:ProgramFiles\Adobe\Common\Plug-ins\7.0\MediaCore"
$stray = "$env:ProgramFiles\Adobe\Common\Plugins\7.0\MediaCore"
$retired = 'FilmBloom.aex', 'FilmHalation.aex', 'FilmGrain.aex'
New-Item -ItemType Directory -Force $stray, $fallback | Out-Null
foreach ($n in @('FilmEffects.aex') + $retired + 'unins000.exe', 'unins000.dat') { Set-Content "$stray\$n" 'old' }
foreach ($n in $retired) { Set-Content "$fallback\$n" 'old' }
$p = Start-Process $Setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$env:RUNNER_TEMP\legacy.log" -Wait -PassThru
if ($p.ExitCode -ne 0) { Get-Content "$env:RUNNER_TEMP\legacy.log"; throw "Setup exit code $($p.ExitCode)" }
if ((Get-FileHash "$fallback\FilmEffects.aex").Hash -ne (Get-FileHash "$X64Dir\FilmEffects.aex").Hash) { throw 'FilmEffects.aex is not the x64 build' }
foreach ($n in $retired) { if (Test-Path "$fallback\$n") { throw "Upgrade left the retired $n" } }
if (Test-Path "$env:ProgramFiles\Adobe\Common\Plugins") { Get-ChildItem -Recurse "$env:ProgramFiles\Adobe\Common\Plugins"; throw 'Stray Common\Plugins folder not removed' }
Start-Process "$env:ProgramFiles\Venosta Visuals\Film Effects\unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
for ($i = 0; $i -lt 60 -and (Test-Path "$fallback\FilmEffects.aex"); $i++) { Start-Sleep 1 }
Write-Host 'Legacy upgrade test passed'
