<#
.SYNOPSIS  One-time: give the current user Modify rights on the two install folders so that
           deploy-local.ps1 / build-native.ps1 -Install can copy without a UAC prompt every time.
           Asks for admin ONCE (self-elevating). Safe to re-run.
.NOTES     Only these two folders are affected:
             C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\com.bang.toolbox   (CEP panel)
             C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\BANG   (native .aex)
           To revoke:  icacls "<folder>" /remove:g "<user>"
#>
[CmdletBinding()] param([string]$User = "$env:USERDOMAIN\$env:USERNAME")
$folders = @(
  'C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\com.bang.toolbox',
  'C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\BANG'
)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-WindowStyle','Hidden','-File',"`"$PSCommandPath`"",'-User',"`"$User`"")
  if ($p.ExitCode -ne 0) { throw "grant failed (exit $($p.ExitCode))" }
  foreach ($f in $folders) { "{0}  ->  {1}" -f $f, ((icacls $f | Select-String ([regex]::Escape($User))) -join ' ') }
  Write-Host "Done. deploy-local.ps1 and build-native.ps1 -Install no longer need UAC." -ForegroundColor Green
  return
}
foreach ($f in $folders) {
  if (-not (Test-Path $f)) { New-Item -ItemType Directory -Force $f | Out-Null }
  icacls $f /grant "${User}:(OI)(CI)M" /T | Out-Null
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
exit 0
