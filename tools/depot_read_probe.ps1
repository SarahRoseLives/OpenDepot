param(
    [int]$WaitSeconds = 75
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W2 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    public class C { public long H; public int Id; public string Cls; public string Txt; }
    public class W { public long H; public string Cls; public string Txt; public List<C> Kids = new List<C>(); }
    static string T(IntPtr h){ var s=new StringBuilder(1024); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    static string K(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<W> ForPid(uint pid){
        var r=new List<W>();
        EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true;
            var w=new W{ H=h.ToInt64(), Cls=K(h), Txt=T(h) };
            EnumChildWindows(h,(c,cl)=>{ w.Kids.Add(new C{ H=c.ToInt64(), Id=GetDlgCtrlID(c), Cls=K(c), Txt=T(c) }); return true; },IntPtr.Zero);
            r.Add(w); return true; },IntPtr.Zero);
        return r;
    }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 3
$WM_COMMAND = 0x0111
$ID_READ_DEVICE = 32789
$seen = @{}
$sentRead = $false
$start = Get-Date
while (((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    $p.Refresh(); if ($p.HasExited) { Write-Output "process exited"; break }
    $wins = [W2]::ForPid([uint32]$p.Id)
    $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
    if (-not $sentRead -and $main) {
        # dismiss intro if present
        foreach ($w in $wins) { if ($w.Cls -eq '#32770' -and -not $w.Txt) { [W2]::SendMessageW([IntPtr]$w.H, $WM_COMMAND, [IntPtr]22784, [IntPtr]::Zero) | Out-Null } }
        Start-Sleep -Milliseconds 800
        Write-Output ("{0:HH:mm:ss} sending ID_READ_DEVICE to main 0x{1:X}" -f (Get-Date), $main.H)
        [W2]::PostMessageW([IntPtr]$main.H, $WM_COMMAND, [IntPtr]$ID_READ_DEVICE, [IntPtr]::Zero) | Out-Null
        $sentRead = $true
    }
    foreach ($w in $wins) {
        $kids = ($w.Kids | Where-Object { $_.Txt } | ForEach-Object { "[$($_.Cls)#$($_.Id)] $($_.Txt)" }) -join ' ; '
        $sig = "$($w.Cls)|$($w.Txt)|$kids"
        if (-not $seen.ContainsKey($sig)) {
            $seen[$sig] = $true
            Write-Output ("{0:HH:mm:ss} (+{1:N1}s) WIN class='{2}' title='{3}'" -f (Get-Date), ((Get-Date)-$start).TotalSeconds, $w.Cls, $w.Txt)
            foreach ($k in $w.Kids) { if ($k.Txt) { Write-Output ("      [{0}#{1}] {2}" -f $k.Cls, $k.Id, $k.Txt) } }
        }
    }
    Start-Sleep -Milliseconds 1500
}
$p.Refresh()
Write-Output ("final exited={0}" -f $p.HasExited)
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
