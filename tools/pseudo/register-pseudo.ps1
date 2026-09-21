<#
.SYNOPSIS
  Register (or remove) the BANG_Toolbox pseudo effect definition in After Effects' PresetEffects.xml.

.DESCRIPTION
  One-time developer step. Inserts tools\pseudo\BANG_Cloner.pseudo.xml right before </Effects>
  in  <AE>\Support Files\PresetEffects.xml  (backup written next to it and into ..\..\..\backups).
  After Effects must be restarted afterwards. Requires administrator rights (self-elevates).

  -Remove   strips a previously inserted block (between the BANG markers).
  -AEPath   override the AE "Support Files" folder (default: newest "Adobe After Effects 20xx").

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\pseudo\register-pseudo.ps1
  powershell -ExecutionPolicy Bypass -File tools\pseudo\register-pseudo.ps1 -Remove
#>
[CmdletBinding()]
param(
  [string]$AEPath,
  [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  $args = @('-NoProfile','-ExecutionPolicy','Bypass','-NoExit','-File',"`"$PSCommandPath`"")
  if ($AEPath) { $args += @('-AEPath', "`"$AEPath`"") }
  if ($Remove) { $args += '-Remove' }
  Start-Process powershell.exe -Verb RunAs -ArgumentList $args
  return
}

if (-not $AEPath) {
  $ae = Get-ChildItem 'C:\Program Files\Adobe' -Directory -Filter 'Adobe After Effects 20*' |
        Sort-Object Name -Descending | Where-Object { Test-Path (Join-Path $_.FullName 'Support Files\AfterFX.exe') } |
        Select-Object -First 1
  if (-not $ae) { throw 'After Effects install not found under C:\Program Files\Adobe' }
  $AEPath = Join-Path $ae.FullName 'Support Files'
}
$xmlPath = Join-Path $AEPath 'PresetEffects.xml'
if (-not (Test-Path $xmlPath)) { throw "Not found: $xmlPath" }

$startMark = '<!-- BANG_Toolbox pseudo effects : begin -->'
$endMark   = '<!-- BANG_Toolbox pseudo effects : end -->'

$bytes = [IO.File]::ReadAllBytes($xmlPath)
$text  = [Text.Encoding]::UTF8.GetString($bytes)
$nl    = if ($text -match "`r`n") { "`r`n" } else { "`n" }

# backup (next to the file + project backups folder)
$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
Copy-Item $xmlPath "$xmlPath.bak_BANG_$ts"
$projBak = Join-Path $here '..\..\..\backups'
if (Test-Path $projBak) { Copy-Item $xmlPath (Join-Path $projBak "PresetEffects.xml_$ts") }

# strip existing block
$pattern = [regex]::Escape($startMark) + '[\s\S]*?' + [regex]::Escape($endMark) + '\s*'
$text = [regex]::Replace($text, $pattern, '')

if (-not $Remove) {
  $block = [IO.File]::ReadAllText((Join-Path $here 'BANG_Cloner.pseudo.xml'), [Text.Encoding]::UTF8)
  $block = $block -replace "`r?`n", $nl
  $insert = "$startMark$nl$block$nl$endMark$nl"
  $idx = $text.LastIndexOf('</Effects>')
  if ($idx -lt 0) { throw '</Effects> not found' }
  $text = $text.Substring(0, $idx) + $insert + $text.Substring($idx)
}

# write UTF-8 without BOM (file declares encoding="utf-8")
[IO.File]::WriteAllBytes($xmlPath, (New-Object Text.UTF8Encoding($false)).GetBytes($text))
Write-Host ("{0}: {1}" -f ($(if ($Remove) {'Removed from'} else {'Registered in'}), $xmlPath)) -ForegroundColor Green
Write-Host "Backup: $xmlPath.bak_BANG_$ts"
Write-Host 'Restart After Effects to load the pseudo effect.'
