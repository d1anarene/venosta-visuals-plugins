# Copy built Film*.aex plugins into Adobe's shared MediaCore folder. Run PowerShell as administrator.
# Only touches these four files; an existing copy is renamed to a timestamped backup first.
#   .\scripts\install-dev-windows.ps1                 # all four
#   .\scripts\install-dev-windows.ps1 FilmGrain       # only some
param([string[]]$Names = @("FilmEffects", "FilmBloom", "FilmHalation", "FilmGrain"))
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
# Documented common plug-in location (Premiere Pro SDK guide, "Plugin Installation").
$Dest = Join-Path $env:ProgramFiles "Adobe\Common\Plugins\7.0\MediaCore"
$Alt = Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
foreach ($name in $Names) {
  $src = Get-ChildItem -Recurse build-win -Filter "$name.aex" | Select-Object -First 1
  if (-not $src) { throw "$name.aex not found; build first" }
  if (Test-Path (Join-Path $Alt "$name.aex")) {
    Write-Host "Note: an older $name.aex exists in $Alt - remove it to avoid loading two copies."
  }
  $target = Join-Path $Dest "$name.aex"
  if (Test-Path $target) {
    Rename-Item $target "$name.aex.backup-$(Get-Date -Format yyyyMMddHHmmss)"
  }
  Copy-Item $src.FullName $target
  Write-Host "Installed $name.aex"
}
Write-Host "Restart Premiere Pro, then look in Effects > Video Effects > Venosta Visuals."
