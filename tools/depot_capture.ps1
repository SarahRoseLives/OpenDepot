param(
    [Parameter(Mandatory=$true)][string]$File,
    [int]$WaitSeconds = 35,
    [string]$Out = "C:\Users\Rose\Projects\OpenDepot\build\out\depot.png"
)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class WinCap {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
if ($p.HasExited) { Write-Output "process exited"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { $h = (Get-Process -Id $p.Id).MainWindowHandle }
Write-Output ("hwnd=0x{0:X}" -f $h.ToInt64())
[WinCap]::ShowWindow($h, 9) | Out-Null
[WinCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Seconds 1
$r = New-Object WinCap+RECT
[WinCap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
Write-Output ("size={0}x{1}" -f $w, $ht)
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WinCap]::PrintWindow($h, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out"
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
