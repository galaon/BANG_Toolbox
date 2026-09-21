<#
.SYNOPSIS  Quit AE (no save) → build → install .aex (admin) → relaunch AE → wait for the BANG_Toolbox panel.
           Native plug-ins are locked while AE runs, so a full restart cycle is required after every build.
#>
[CmdletBinding()] param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot; $repo = Split-Path $here -Parent
$ae = 'C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\AfterFX.exe'
if (Get-Process AfterFX -ErrorAction SilentlyContinue) {
  try { & node (Join-Path $repo 'tools\cep-eval.js') jsx 'app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES); app.quit(); "quit"' | Out-Null } catch {}
  $t = 0; while ((Get-Process AfterFX -ErrorAction SilentlyContinue) -and $t -lt 60) { Start-Sleep 2; $t += 2 }
  if (Get-Process AfterFX -ErrorAction SilentlyContinue) { Stop-Process -Name AfterFX -Force; Start-Sleep 3 }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'build-native.ps1') -Configuration $Configuration -Install
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Start-Process $ae
$t = 0
while ($t -lt 240) {
  try { $r = Invoke-RestMethod -Uri 'http://localhost:8089/json' -TimeoutSec 3; if ($r) { break } } catch {}
  Start-Sleep 5; $t += 5
}
Start-Sleep 3
Write-Host "AE relaunched, panel up after ${t}s" -ForegroundColor Green
