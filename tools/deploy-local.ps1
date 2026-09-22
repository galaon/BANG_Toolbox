<#
.SYNOPSIS
  Mirror this repo into the local CEP extensions folder for testing in After Effects.

.DESCRIPTION
  Copies the runtime payload (CSXS, css, js, jsx, icons, index.html) to

    C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\com.bang.toolbox   (default, -Scope Machine)
    %APPDATA%\Adobe\CEP\extensions\com.bang.toolbox                             (-Scope User)

  using robocopy /MIR. Repo-only files (.git, tools, dist, README.md, ...) are
  never copied. Note: robocopy /XD applies to both sides, so pre-existing
  .git\ or tools\ folders inside the target are left untouched unless you pass
  -PurgeStale, which deletes those known leftovers from older manual installs.

  The Machine scope needs administrator rights; the script re-launches itself
  elevated when required.

  Restart After Effects (or close/reopen the panel) after deploying.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\deploy-local.ps1
  powershell -ExecutionPolicy Bypass -File tools\deploy-local.ps1 -Scope User
#>
[CmdletBinding()]
param(
  [ValidateSet('Machine', 'User')]
  [string]$Scope = 'Machine',
  [switch]$PurgeStale,
  [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BundleId = 'com.bang.toolbox'

switch ($Scope) {
  'Machine' { $extDir = 'C:\Program Files (x86)\Common Files\Adobe\CEP\extensions' }
  'User'    { $extDir = Join-Path $env:APPDATA 'Adobe\CEP\extensions' }
}
$target = Join-Path $extDir $BundleId

# -- elevation (Machine scope only) ----------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($Scope -eq 'Machine' -and -not $isAdmin -and -not $WhatIf) {
  Write-Host "Elevation required for $extDir - relaunching as administrator..." -ForegroundColor Yellow
  # 승격 창은 끝나면 닫히도록(-NoExit 금지: 남아 있는 관리자 창이 자동화 스크린샷을 가리고 클릭을 막는다) 숨겨서 실행하고 끝날 때까지 기다린다
  $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden',
            '-File', "`"$PSCommandPath`"", '-Scope', $Scope)
  if ($PurgeStale) { $args += '-PurgeStale' }
  $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $args -Wait -PassThru
  if ($p.ExitCode -ne 0) { Write-Warning "elevated deploy exited with $($p.ExitCode)" } else { Write-Host "Deployed (elevated) to $target" -ForegroundColor Green }
  return
}

# -- warn if both scopes hold the same bundle id (CEP loads only one) --------
$other = if ($Scope -eq 'Machine') { Join-Path $env:APPDATA "Adobe\CEP\extensions\$BundleId" }
         else { "C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\$BundleId" }
if (Test-Path $other) {
  Write-Warning "Another copy of $BundleId exists at: $other  (CEP may load that one instead)"
}

# -- robocopy mirror --------------------------------------------------------
if ($WhatIf) { Write-Host "[WhatIf] would mirror $RepoRoot -> $target"; }
New-Item -ItemType Directory -Force -Path $target | Out-Null

$rcArgs = @(
  "`"$RepoRoot`"", "`"$target`"",
  '/MIR', '/NJH', '/NP', '/NDL',
  '/XD', '.git', 'tools', 'dist', 'backups',
  '/XF', '.gitignore', 'README.md', 'AGENTS.md', 'CHANGELOG.md', '*.patch'
)
if ($WhatIf) { $rcArgs += '/L' }

$p = Start-Process robocopy.exe -ArgumentList $rcArgs -NoNewWindow -Wait -PassThru
# robocopy: 0-7 = success (bits: 1 copied, 2 extras, 4 mismatched), >=8 = failure
if ($p.ExitCode -ge 8) { throw "robocopy failed with exit code $($p.ExitCode)" }

# -- optional: remove leftovers from older manual installs -------------------
if ($PurgeStale) {
  foreach ($rel in '.git', 'tools') {
    $stale = Join-Path $target $rel
    if (Test-Path $stale) {
      if ($WhatIf) { Write-Host "[WhatIf] would remove stale $stale" }
      else { Remove-Item $stale -Recurse -Force; Write-Host "Removed stale $stale" }
    }
  }
}

Write-Host ""
if ($WhatIf) { Write-Host "[WhatIf] no changes made. Target: $target"; return }
Write-Host "Deployed to: $target" -ForegroundColor Green
[xml]$m = Get-Content (Join-Path $RepoRoot 'CSXS\manifest.xml') -Raw
Write-Host ("Version   : " + $m.ExtensionManifest.ExtensionBundleVersion)
Write-Host "Now restart After Effects  ->  Window > Extensions > BANG_Toolbox"
