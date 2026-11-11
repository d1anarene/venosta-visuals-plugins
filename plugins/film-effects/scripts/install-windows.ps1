# Film Effects installer for Windows. Right-click > "Run with PowerShell" (as administrator), from
# the folder that contains the Film*.aex files. Older copies are renamed to a timestamped backup.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`""
  exit
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$plugins = Get-ChildItem -Path $here -Filter "Film*.aex"
if (-not $plugins) { Write-Host "No Film*.aex found next to this script."; Read-Host "Press Enter to close"; exit 1 }
foreach ($p in $plugins) {
  $target = Join-Path $dest $p.Name
  if (Test-Path $target) { Rename-Item $target "$($p.Name).backup-$(Get-Date -Format yyyyMMddHHmmss)" }
  Copy-Item $p.FullName $target
  Unblock-File $target
  Write-Host "Installed $($p.Name)"
}
Write-Host "`nDone. Restart Premiere Pro, then look in Effects > Video Effects > Venosta Visuals."
Read-Host "Press Enter to close"
