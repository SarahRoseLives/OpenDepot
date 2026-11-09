param(
    [Parameter(Mandatory=$true)][string]$File,
    [Parameter(Mandatory=$true)][int]$Cmd,
    [int]$WaitSeconds = 30,
    [int]$LoadWaitSeconds = 45,
    [switch]$ClickOk
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W3 {
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
    static string T(IntPtr h){ var s=new StringBuilder(2048); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
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
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
$start = Get-Date
$sent = $false
$seen = @{}
$clicked = @{}
while (((Get-Date) - $start).TotalSeconds -lt ($LoadWaitSeconds + $WaitSeconds)) {
    $p.Refresh(); if ($p.HasExited) { Write-Output "process exited"; break }
    $wins = [W3]::ForPid([uint32]$p.Id)
    $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
    $loaded = $main -and ($main.Txt -match ' - ')
    if (-not $sent -and $loaded) {
        Write-Output ("{0:HH:mm:ss} doc loaded: '{1}' -> sending cmd {2}" -f (Get-Date), $main.Txt, $Cmd)
        [W3]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]$Cmd, [IntPtr]::Zero) | Out-Null
        $sent = $true
    }
    foreach ($w in $wins) {
        $kids = ($w.Kids | Where-Object { $_.Txt } | ForEach-Object { "[$($_.Cls)#$($_.Id)] $($_.Txt)" }) -join ' ; '
        $sig = "$($w.Cls)|$($w.Txt)|$kids"
        if (-not $seen.ContainsKey($sig)) {
            $seen[$sig] = $true
            Write-Output ("{0:HH:mm:ss} (+{1:N1}s) WIN class='{2}' title='{3}'" -f (Get-Date), ((Get-Date)-$start).TotalSeconds, $w.Cls, $w.Txt)
            foreach ($k in $w.Kids) { if ($k.Txt) { Write-Output ("      [{0}#{1}] {2}" -f $k.Cls, $k.Id, $k.Txt) } }
        }
        if ($ClickOk -and -not $clicked.ContainsKey($w.H)) {
            foreach ($k in $w.Kids) {
                if ($k.Cls -eq 'Button' -and $k.Txt -match '^(OK|&OK|Yes|&Yes|Continue|&Continue|Write)$') {
                    Write-Output ("{0:HH:mm:ss} CLICK [{1}#{2}] {3}" -f (Get-Date), $k.Cls, $k.Id, $k.Txt)
                    [W3]::SendMessageW([IntPtr]$k.H, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
                    $clicked[$w.H] = $true
                }
            }
        }
    }
    Start-Sleep -Milliseconds 1200
}
$p.Refresh()
Write-Output ("final exited={0}" -f $p.HasExited)
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
