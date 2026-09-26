<#
.SYNOPSIS  Build BANG native effect plug-ins (.aex) with MSBuild (VS 2022 Build Tools).
.PARAMETER Configuration  Release (default) | Debug
.PARAMETER Install        Also copy the .aex into AE's Plug-ins\BANG folder (asks for admin).
.EXAMPLE   powershell -ExecutionPolicy Bypass -File native\build-native.ps1 -Install
#>
[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$Install, [string]$Defines = '')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vc)) { throw "VS 2022 Build Tools not found: $vc" }
$env:CL = '/utf-8'   # SDK headers contain non-CP949 chars (warning C4819)
$projects = @(Get-ChildItem (Join-Path $here 'BANG_FX') -Filter '*.vcxproj') +
            @(Get-ChildItem (Join-Path $here 'BANG_Picker') -Filter '*.vcxproj' -ErrorAction SilentlyContinue)
foreach ($p in $projects) {
  Write-Host "== $($p.Name) [$Configuration]" -ForegroundColor Cyan
  cmd /c "`"$vc`" >nul && msbuild `"$($p.FullName)`" /p:Configuration=$Configuration /p:Platform=x64 /p:BANG_FX_DEFINES=$Defines /m /v:m /nologo"
  if ($LASTEXITCODE -ne 0) { throw "build failed: $($p.Name)" }
}
$out = Join-Path $here "out\$Configuration"
Get-ChildItem $out -Filter '*.aex' | ForEach-Object { "{0,10} bytes  {1}" -f $_.Length, $_.FullName }
# 스포이드 도우미는 AE 플러그인이 아니라 패널이 실행하는 exe — 확장 폴더 안 bin\ 으로 옮긴다
$picker = Join-Path $out 'BANG_Picker.exe'
if (Test-Path $picker) {
  $bin = Join-Path $here '..\bin'
  New-Item -ItemType Directory -Force $bin | Out-Null
  Copy-Item $picker $bin -Force
  "{0,10} bytes  {1}" -f (Get-Item $picker).Length, (Join-Path (Resolve-Path $bin) 'BANG_Picker.exe')
}
if ($Install) {
  $dst = 'C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\BANG'
  # tools\grant-write-access.ps1 을 한 번 실행해 두면 UAC 없이 바로 복사된다. 권한이 없을 때만 승격.
  try {
    New-Item -ItemType Directory -Force $dst -ErrorAction Stop | Out-Null
    Copy-Item "$out\*.aex" $dst -Force -ErrorAction Stop
  } catch {
    Write-Host "no write access to $dst - elevating once (run tools\grant-write-access.ps1 to avoid this)" -ForegroundColor Yellow
    $cmd = "New-Item -ItemType Directory -Force '$dst' | Out-Null; Copy-Item '$out\*.aex' '$dst' -Force"
    Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList @('-NoProfile','-WindowStyle','Hidden','-Command', $cmd)
  }
  foreach ($f in Get-ChildItem $out -Filter '*.aex') {
    $a = (Get-FileHash $f.FullName).Hash; $b = (Get-FileHash (Join-Path $dst $f.Name)).Hash
    if ($a -ne $b) { throw "install verify failed: $($f.Name) differs (is After Effects running?)" }
  }
  Write-Host "Installed to $dst (verified; restart After Effects)" -ForegroundColor Green
}
