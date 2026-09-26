<#
.SYNOPSIS
  Package BANG_Toolbox into a distributable zip (unsigned CEP extension).

.DESCRIPTION
  Produces  <OutDir>\BANG_Toolbox_v<version>.zip  with the same layout as v1.0.0:

    BANG_Toolbox_v<version>\
      INSTALL.txt
      EnablePlayerDebugMode.reg
      com.bang.toolbox\
        CSXS\manifest.xml
        index.html
        css\  js\  jsx\  icons\

  <version> is read from CSXS\manifest.xml (ExtensionBundleVersion).
  Repo-only files (.git, .gitignore, README.md, dist\, tools\) are excluded.

.PARAMETER OutDir
  Where to write the zip. Default: ..\dist relative to the repo root
  (i.e. E:\_BANG_WORK\_AI\AE_BANG_Toolbox\dist).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build.ps1
#>
[CmdletBinding()]
param(
  [string]$OutDir
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BundleId = 'com.bang.toolbox'
if (-not $OutDir) { $OutDir = Join-Path (Split-Path $RepoRoot -Parent) 'dist' }

# -- version from manifest -------------------------------------------------
$manifestPath = Join-Path $RepoRoot 'CSXS\manifest.xml'
[xml]$manifest = Get-Content $manifestPath -Raw
$version = $manifest.ExtensionManifest.ExtensionBundleVersion
if (-not $version) { throw "ExtensionBundleVersion not found in $manifestPath" }
$extVersion = ($manifest.ExtensionManifest.ExtensionList.Extension | Select-Object -First 1).Version
if ($extVersion -ne $version) {
  Write-Warning "ExtensionBundleVersion ($version) != Extension Version ($extVersion) in manifest.xml"
}

$pkgName = "BANG_Toolbox_v$version"
$zipPath = Join-Path $OutDir "$pkgName.zip"

# -- staging ----------------------------------------------------------------
$stage    = Join-Path $env:TEMP ("bang_build_" + [guid]::NewGuid().ToString('N'))
$pkgRoot  = Join-Path $stage $pkgName
$extRoot  = Join-Path $pkgRoot $BundleId
New-Item -ItemType Directory -Force -Path $extRoot | Out-Null

# extension payload (only what the panel needs at runtime)
Copy-Item (Join-Path $RepoRoot 'index.html') $extRoot
foreach ($dir in 'CSXS', 'css', 'js', 'jsx', 'bin') {
  $src = Join-Path $RepoRoot $dir
  if (Test-Path $src) { Copy-Item $src (Join-Path $extRoot $dir) -Recurse }
  elseif ($dir -eq 'bin') { Write-Warning "bin\ not found - 스포이드 도우미(BANG_Picker.exe) 가 빠집니다 (nativeuild-native.ps1 먼저 실행)" }
  else { throw "missing payload folder: $src" }
}
# icons: manifest references ./icons/icon_32.png; ship the folder even if empty
$iconsSrc = Join-Path $RepoRoot 'icons'
$iconsDst = Join-Path $extRoot 'icons'
if (Test-Path $iconsSrc) { Copy-Item $iconsSrc $iconsDst -Recurse }
else {
  New-Item -ItemType Directory -Force -Path $iconsDst | Out-Null
  Write-Warning "icons\ not found in repo - shipping empty icons\ (panel shows no tab icon)"
}

# native plug-ins (.aex) — built by nativeuild-native.ps1 (Release). Optional: skipped with a warning if absent.
$aexDir = Join-Path $RepoRoot 'native\out\Release'
$aex = if (Test-Path $aexDir) { Get-ChildItem $aexDir -Filter '*.aex' } else { @() }
if ($aex.Count -gt 0) {
  $plugDst = Join-Path $pkgRoot 'plugins'
  New-Item -ItemType Directory -Force -Path $plugDst | Out-Null
  $aex | ForEach-Object { Copy-Item $_.FullName $plugDst }
} else { Write-Warning "no .aex in native\out\Release - package will not include native plug-ins" }

# installer helpers next to the extension folder
Copy-Item (Join-Path $RepoRoot 'dist\INSTALL.txt')              $pkgRoot
Copy-Item (Join-Path $RepoRoot 'dist\EnablePlayerDebugMode.reg') $pkgRoot

# safety: nothing repo-only leaked into the package
$leak = Get-ChildItem $pkgRoot -Recurse -Force |
        Where-Object { $_.Name -in '.git', '.gitignore', '.debug', 'README.md', 'AGENTS.md', 'CHANGELOG.md', 'tools', 'dist' }
if ($leak) { throw "Repo-only item leaked into package: $($leak.FullName -join ', ')" }

# -- zip --------------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $pkgRoot -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

# -- report -----------------------------------------------------------------
Write-Host ""
Write-Host "Built: $zipPath" -ForegroundColor Green
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
  $z.Entries | Where-Object { $_.Length -gt 0 } |
    Sort-Object FullName |
    ForEach-Object { "{0,8}  {1}" -f $_.Length, $_.FullName }
} finally { $z.Dispose() }
