param(
    [Parameter(Mandatory=$true)][string]$File,
    [int]$WaitSeconds = 45
)

Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WinEnum {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);

    public class WinInfo {
        public long Handle; public uint Pid; public bool Visible;
        public string Class; public string Title;
        public List<WinInfo> Children = new List<WinInfo>();
    }
    static string Text(IntPtr h) { var sb = new StringBuilder(1024); GetWindowTextW(h, sb, sb.Capacity); return sb.ToString(); }
    static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity); return sb.ToString(); }

    public static List<WinInfo> ForProcess(uint pid) {
        var result = new List<WinInfo>();
        EnumWindows((h, lp) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid) return true;
            var wi = new WinInfo { Handle = h.ToInt64(), Pid = p, Visible = IsWindowVisible(h), Class = Cls(h), Title = Text(h) };
            EnumChildWindows(h, (ch, cl) => {
                wi.Children.Add(new WinInfo { Handle = ch.ToInt64(), Pid = p, Visible = IsWindowVisible(ch), Class = Cls(ch), Title = Text(ch) });
                return true;
            }, IntPtr.Zero);
            result.Add(wi);
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@

$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
Write-Output ("{0:HH:mm:ss} launch: {1} `"{2}`" (wait {3}s)" -f (Get-Date), $exe, $File, $WaitSeconds)
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
$start = Get-Date
$deadline = $start.AddSeconds($WaitSeconds)
$seen = @{}
while ((Get-Date) -lt $deadline) {
    $p.Refresh()
    if ($p.HasExited) { Write-Output ("{0:HH:mm:ss} process exited code={1}" -f (Get-Date), $p.ExitCode); break }
    foreach ($w in [WinEnum]::ForProcess([uint32]$p.Id)) {
        $childText = ($w.Children | Where-Object { $_.Title } | ForEach-Object { "[$($_.Class)] $($_.Title)" }) -join " ; "
        $key = "$($w.Class)|$($w.Title)|$childText"
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $t = ((Get-Date) - $start).TotalSeconds
            Write-Output ("{0:HH:mm:ss} (+{1:N1}s) WINDOW class='{2}' visible={3} title='{4}'" -f (Get-Date), $t, $w.Class, $w.Visible, $w.Title)
            foreach ($c in $w.Children) { if ($c.Title) { Write-Output ("           CHILD class='{0}' title='{1}'" -f $c.Class, $c.Title) } }
        }
    }
    Start-Sleep -Milliseconds 1500
}
$p.Refresh()
Write-Output ("{0:HH:mm:ss} final: exited={1}" -f (Get-Date), $p.HasExited)
$wins = [WinEnum]::ForProcess([uint32]$p.Id)
$dialogs = @($wins | Where-Object { $_.Class -eq '#32770' })
Write-Output ("dialogs_seen={0}" -f $dialogs.Count)
if (-not $p.HasExited) {
    $p.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 2
    $p.Refresh()
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Write-Output "closed"
}
