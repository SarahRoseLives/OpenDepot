param(
    [string]$File = "C:\Users\Rose\Projects\OpenDepot\build\out\205CNX2873-clone.cpg",
    [string]$Port = "COM20",
    [int]$LoadWaitSeconds = 45,
    [int]$ObserveSeconds = 60,
    [switch]$AutoConfirm
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W4 {
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
    public class W { public long H; public string Cls; public string Txt; public bool Vis; public List<C> Kids = new List<C>(); }
    static string T(IntPtr h){ var s=new StringBuilder(2048); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    static string K(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<W> ForPid(uint pid){
        var r=new List<W>();
        EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true;
            var w=new W{ H=h.ToInt64(), Cls=K(h), Txt=T(h), Vis=IsWindowVisible(h) };
            EnumChildWindows(h,(c,cl)=>{ w.Kids.Add(new C{ H=c.ToInt64(), Id=GetDlgCtrlID(c), Cls=K(c), Txt=T(c) }); return true; },IntPtr.Zero);
            r.Add(w); return true; },IntPtr.Zero);
        return r;
    }
    public static List<string> ComboItems(IntPtr h){
        var outl=new List<string>();
        int n=(int)SendMessageW(h,0x0146,IntPtr.Zero,IntPtr.Zero);
        for(int i=0;i<n;i++){
            IntPtr buf=Marshal.AllocHGlobal(1024);
            SendMessageW(h,0x0148,(IntPtr)i,buf);
            outl.Add(Marshal.PtrToStringUni(buf));
            Marshal.FreeHGlobal(buf);
        }
        return outl;
    }
    public static int ComboFind(IntPtr h, string text){
        IntPtr buf=Marshal.AllocHGlobal(1024);
        var bytes=Encoding.Unicode.GetBytes(text+"\0");
        Marshal.Copy(bytes,0,buf,bytes.Length);
        int idx=(int)SendMessageW(h,0x0158,(IntPtr)(-1),buf);
        Marshal.FreeHGlobal(buf);
        return idx;
    }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
$start = Get-Date
$sentWrite = $false
$handledPort = $false
$seen = @{}
$confirmed = @{}
while (((Get-Date) - $start).TotalSeconds -lt ($LoadWaitSeconds + $ObserveSeconds)) {
    $p.Refresh(); if ($p.HasExited) { Write-Output "process exited"; break }
    $wins = [W4]::ForPid([uint32]$p.Id)
    $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
    $loaded = $main -and ($main.Txt -match ' - ')
    if (-not $sentWrite -and $loaded) {
        Write-Output ("{0:HH:mm:ss} doc loaded '{1}' -> Write Device 32790" -f (Get-Date), $main.Txt)
        [W4]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]32790, [IntPtr]::Zero) | Out-Null
        $sentWrite = $true
    }
    foreach ($w in $wins) {
        $kids = ($w.Kids | Where-Object { $_.Txt } | ForEach-Object { "[$($_.Cls)#$($_.Id)] $($_.Txt)" }) -join ' ; '
        $sig = "$($w.Cls)|$($w.Txt)|$kids"
        if (-not $seen.ContainsKey($sig)) {
            $seen[$sig] = $true
            Write-Output ("{0:HH:mm:ss} (+{1:N1}s) WIN class='{2}' vis={3} title='{4}'" -f (Get-Date), ((Get-Date)-$start).TotalSeconds, $w.Cls, $w.Vis, $w.Txt)
            foreach ($k in $w.Kids) { if ($k.Txt) { Write-Output ("      [{0}#{1}] {2}" -f $k.Cls, $k.Id, $k.Txt) } }
        }
        if ($w.Txt -eq 'Select Port' -and -not $handledPort) {
            $combo = $w.Kids | Where-Object { $_.Cls -eq 'ComboBox' } | Select-Object -First 1
            if ($combo) {
                $items = [W4]::ComboItems([IntPtr]$combo.H)
                Write-Output ("{0:HH:mm:ss} Select Port combo items: {1}" -f (Get-Date), ($items -join ', '))
                $idx = [W4]::ComboFind([IntPtr]$combo.H, $Port)
                if ($idx -lt 0) { $idx = 0 }
                Write-Output ("{0:HH:mm:ss} selecting index {1} ({2})" -f (Get-Date), $idx, $items[$idx])
                [W4]::SendMessageW([IntPtr]$combo.H, 0x014E, [IntPtr]$idx, [IntPtr]::Zero) | Out-Null
                $wp = [IntPtr](1 * 0x10000 + $combo.Id)
                [W4]::SendMessageW([IntPtr]$w.H, 0x0111, $wp, [IntPtr]$combo.H) | Out-Null
                Start-Sleep -Milliseconds 300
                $ok = $w.Kids | Where-Object { $_.Cls -eq 'Button' -and $_.Id -eq 1 } | Select-Object -First 1
                if ($ok) { Write-Output "clicking OK on Select Port"; [W4]::SendMessageW([IntPtr]$ok.H, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
                $handledPort = $true
            }
        }
        if ($AutoConfirm -and $handledPort -and -not $confirmed.ContainsKey($w.H)) {
            $alltext = $w.Txt + ' ' + (($w.Kids | ForEach-Object { $_.Txt }) -join ' ')
            if ($alltext -notmatch 'tuning|soft.?pot|calibration|Upgrade Radio|FLASHport') {
                foreach ($k in $w.Kids) {
                    if ($k.Cls -eq 'Button' -and $k.Txt -match '^(OK|&OK|Yes|&Yes|Continue|&Continue|Write|&Write)$') {
                        Write-Output ("{0:HH:mm:ss} AUTO-CLICK [{1}#{2}] {3}" -f (Get-Date), $k.Cls, $k.Id, $k.Txt)
                        [W4]::SendMessageW([IntPtr]$k.H, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
                        $confirmed[$w.H] = $true
                    }
                }
            }
        }
    }
    Start-Sleep -Milliseconds 1200
}
$p.Refresh()
Write-Output ("final exited={0}" -f $p.HasExited)
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
