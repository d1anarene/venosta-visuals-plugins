# Build the native plugins on Windows (CPU path). STATUS: not yet run on Windows.
# Usage (Developer PowerShell for Visual Studio 2022):
#   .\scripts\build-windows.ps1 "C:\AfterEffectsSDK\Examples" "C:\Premiere Pro SDK"
param(
  [Parameter(Mandatory = $true)][string]$AeSdk,
  [Parameter(Mandatory = $true)][string]$PrSdk
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release | Out-Null
cmake --build build --config Release --target fe_core_tests | Out-Null
& .\build\Release\fe_core_tests.exe | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw "core tests failed" }

cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DFE_BUILD_PLUGIN=ON "-DAE_SDK_DIR=$AeSdk" "-DPR_SDK_DIR=$PrSdk"
foreach ($t in "FilmEffects", "FilmBloom", "FilmHalation", "FilmGrain") {
  cmake --build build-win --config Release --target $t
  if ($LASTEXITCODE -ne 0) { throw "build of $t failed" }
}
Get-ChildItem -Recurse build-win -Filter "Film*.aex" | ForEach-Object { "Built: $($_.FullName)" }
