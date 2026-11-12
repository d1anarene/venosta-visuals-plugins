# Silent-install test for a Premiere effect Setup built from vv_premiere_setup.iss (used in CI).
#  1) no Premiere registry value: installs <Binary>.aex into Common\Plug-ins\7.0\MediaCore and it
#     is byte-identical to -Expected (x64 or ARM64 build for this machine), then uninstalls;
#  2) unless -SkipRegistryTest: installs into the folder named by Premiere's Plug-InsDir value.
param(
  [Parameter(Mandatory = $true)][string]$Setup,
  [Parameter(Mandatory = $true)][string]$Binary,
  [Parameter(Mandatory = $true)][string]$Name,
  [Parameter(Mandatory = $true)][string]$Expected,
  [switch]$SkipRegistryTest
)
$ErrorActionPreference = 'Stop'
$file = "$Binary.aex"
$fallback = "$env:ProgramFiles\Adobe\Common\Plug-ins\7.0\MediaCore"
$uninst = "$env:ProgramFiles\Venosta Visuals\$Name\unins000.exe"
$key = 'HKLM:\SOFTWARE\Adobe\Premiere Pro\CurrentVersion'

function Install($log) {
  $p = Start-Process $Setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log" -Wait -PassThru
  if ($p.ExitCode -ne 0) { Get-Content $log; throw "Setup exit code $($p.ExitCode)" }
}
function Uninstall($dir) {
  if (-not (Test-Path $uninst)) { throw "Uninstaller missing: $uninst" }
  Start-Process $uninst -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
  for ($i = 0; $i -lt 60 -and (Test-Path "$dir\$file"); $i++) { Start-Sleep 1 }
  if (Test-Path "$dir\$file") { throw "Uninstall left $file" }
}

Install "$env:RUNNER_TEMP\setup1.log"
if (-not (Test-Path "$fallback\$file")) { Get-Content "$env:RUNNER_TEMP\setup1.log"; throw "Missing $fallback\$file" }
if ((Get-FileHash "$fallback\$file").Hash -ne (Get-FileHash $Expected).Hash) { throw "$file is not the build for this machine" }
Uninstall $fallback

if (-not $SkipRegistryTest) {
  $custom = 'C:\VV-Test\MediaCore'
  New-Item -ItemType Directory -Force $custom | Out-Null
  New-Item -Force $key | Out-Null
  New-ItemProperty -Force $key -Name 'Plug-InsDir' -Value "$custom\" | Out-Null
  try {
    Install "$env:RUNNER_TEMP\setup2.log"
    if (-not (Test-Path "$custom\$file")) { Get-Content "$env:RUNNER_TEMP\setup2.log"; throw "Missing $custom\$file" }
    Uninstall $custom
  } finally {
    Remove-Item -Recurse -Force 'HKLM:\SOFTWARE\Adobe\Premiere Pro'
  }
}
Write-Host "$Name installer test passed"
