# AEgreatAgain - Screen Color Picker
# GetAsyncKeyState: 포커스 없이 전역 입력 감지
# 결과 파일 경로를 첫 번째 인수로 받음 ($args[0])

param([string]$OutFile)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AEColorGrab {
    [DllImport("gdi32.dll")]
    public static extern int GetPixel(IntPtr hdc, int x, int y);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [DllImport("user32.dll")]
    public static extern short GetAsyncKeyState(int vKey);
}
"@

# 결과 파일 경로 (인수 없으면 TEMP 폴백)
$outFile = if ($OutFile) { $OutFile } else { "$env:TEMP\ae_picked_color.txt" }
if (Test-Path $outFile) { Remove-Item $outFile -Force }

# 전체 가상 스크린 (다중 모니터 지원)
$allScreens = [System.Windows.Forms.Screen]::AllScreens
$left   = ($allScreens | Measure-Object -Property Bounds.Left   -Minimum).Minimum
$top    = ($allScreens | Measure-Object -Property Bounds.Top    -Minimum).Minimum
$right  = ($allScreens | Measure-Object -Property Bounds.Right  -Maximum).Maximum
$bottom = ($allScreens | Measure-Object -Property Bounds.Bottom -Maximum).Maximum

# 커서 십자선 표시용 투명 오버레이
$form = New-Object System.Windows.Forms.Form
$form.StartPosition   = [System.Windows.Forms.FormStartPosition]::Manual
$form.Bounds          = [System.Drawing.Rectangle]::FromLTRB($left, $top, $right, $bottom)
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.Opacity         = 0.01
$form.TopMost         = $true
$form.Cursor          = [System.Windows.Forms.Cursors]::Cross
$form.BackColor       = [System.Drawing.Color]::Black
$form.ShowInTaskbar   = $false

$script:lmbSeenUp = $false
$script:done      = $false

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 30

$timer.Add_Tick({
    if ($script:done) { return }

    $lmb = ([AEColorGrab]::GetAsyncKeyState(0x01) -band 0x8000) -ne 0
    $rmb = ([AEColorGrab]::GetAsyncKeyState(0x02) -band 0x8000) -ne 0
    $esc = ([AEColorGrab]::GetAsyncKeyState(0x1B) -band 0x8000) -ne 0

    # LMB이 한 번이라도 올라와야 실제 pick 허용 (AE 버튼 클릭 무시)
    if (-not $lmb) { $script:lmbSeenUp = $true }

    # ESC 또는 우클릭 → 취소
    if ($esc -or $rmb) {
        $script:done = $true
        $timer.Stop()
        "CANCELLED" | Set-Content -Path $outFile -NoNewline -Encoding ASCII
        $form.Close()
        return
    }

    # 좌클릭 (최초 release 확인 후)
    if ($lmb -and $script:lmbSeenUp) {
        $script:done = $true
        $timer.Stop()
        $pos = [System.Windows.Forms.Cursor]::Position
        $hdc = [AEColorGrab]::GetDC([IntPtr]::Zero)
        $col = [AEColorGrab]::GetPixel($hdc, $pos.X, $pos.Y)
        [void][AEColorGrab]::ReleaseDC([IntPtr]::Zero, $hdc)
        $r = $col -band 0xFF
        $g = ($col -shr 8) -band 0xFF
        $b = ($col -shr 16) -band 0xFF
        ("{0:X2}{1:X2}{2:X2}" -f $r, $g, $b) | Set-Content -Path $outFile -NoNewline -Encoding ASCII
        $form.Close()
        return
    }
})

$form.Add_Shown({ $timer.Start() })
[void]$form.ShowDialog()
