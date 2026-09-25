<#
.SYNOPSIS  Quit AE (no save) → build → install .aex (admin) → relaunch AE → wait for the BANG_Toolbox panel.
           Native plug-ins are locked while AE runs, so a full restart cycle is required after every build.
#>
[CmdletBinding()] param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot; $repo = Split-Path $here -Parent
$ae = 'C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\AfterFX.exe'
# AE 를 강제 종료하면 다음 실행에서 'Crash Repair Options' 모달이 떠 패널이 뜨지 않는다 → 정상 종료를 두 단계로 시도
if (Get-Process AfterFX -ErrorAction SilentlyContinue) {
  # 1) 패널 경유 app.quit() — 저장 프롬프트 없이 가장 깔끔
  try { & node (Join-Path $repo 'tools\cep-eval.js') jsx 'app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES); app.quit(); "quit"' | Out-Null } catch {}
  $t = 0; while ((Get-Process AfterFX -ErrorAction SilentlyContinue) -and $t -lt 60) { Start-Sleep 2; $t += 2 }
  # 2) 패널이 없을 때(닫혀 있거나 AE 가 UI 를 못 띄운 상태) 창 닫기 요청 — 저장 프롬프트가 뜨면 여기서 멈춘다
  $p = Get-Process AfterFX -ErrorAction SilentlyContinue
  if ($p) {
    Write-Host 'panel unreachable - asking AE to close its window' -ForegroundColor Yellow
    foreach ($q in $p) { try { $q.CloseMainWindow() | Out-Null } catch {} }
    $t = 0; while ((Get-Process AfterFX -ErrorAction SilentlyContinue) -and $t -lt 30) { Start-Sleep 2; $t += 2 }
  }
  # 3) 최후의 수단
  if (Get-Process AfterFX -ErrorAction SilentlyContinue) {
    Write-Warning 'AE did not quit - killing it. The next launch will show "Crash Repair Options"; click Continue.'
    Stop-Process -Name AfterFX -Force; Start-Sleep 3
  }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'build-native.ps1') -Configuration $Configuration -Install
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Start-Process $ae
$t = 0
while ($t -lt 240) {
  try { $r = Invoke-RestMethod -Uri 'http://localhost:8089/json' -TimeoutSec 3; if ($r) { break } } catch {}
  if ($t -eq 60) { Write-Warning 'panel still down after 60s - a modal (Crash Repair Options / save prompt) is probably blocking AE startup, or the screen is locked (AE cannot build its UI then).' }
  Start-Sleep 5; $t += 5
}
Start-Sleep 3
# 같은 버전 번호의 .aex 를 바꿔 끼우면 AE 디스크 캐시가 예전 렌더를 그대로 돌려준다 → 캐시 비움
try { & node (Join-Path $repo 'tools\cep-eval.js') jsx 'app.purge(PurgeTarget.ALL_CACHES); "purged"' | Out-Null } catch {}
Write-Host "AE relaunched (caches purged), panel up after ${t}s" -ForegroundColor Green
