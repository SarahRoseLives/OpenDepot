param(
    [string]$File = 'C:\Users\Rose\Projects\OpenDepot\build\radio-backups\205CNX2873-test-radio-backup.cpg',
    [int]$LoadWaitSeconds = 45,
    [int]$WaitSeconds = 25
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W10 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
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
    public static List<string> ComboItems(IntPtr h){
        var o=new List<string>(); int n=(int)SendMessageW(h,0x0146,IntPtr.Zero,IntPtr.Zero);
        for(int i=0;i<n;i++){ IntPtr b=Marshal.AllocHGlobal(8192); SendMessageW(h,0x0148,(IntPtr)i,b); o.Add(Marshal.PtrToStringUni(b)); Marshal.FreeHGlobal(b); }
        return o;
    }
    public static void Click(IntPtr h){ SendMessageW(h,0x00F5,IntPtr.Zero,IntPtr.Zero); }
}
"@
$exe = 'C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe'
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
$start = Get-Date; $sent = $false; $dots = $false
while (((Get-Date) - $start).TotalSeconds -lt ($LoadWaitSeconds + $WaitSeconds)) {
    $p.Refresh(); if ($p.HasExited) { break }
    $wins = [W10]::ForPid([uint32]$p.Id)
    $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' -and $_.Txt -match ' - ' } | Select-Object -First 1
    if (-not $sent -and $main) { [W10]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]50207, [IntPtr]::Zero) | Out-Null; $sent = $true; Write-Output 'sent Flash Radio 50207' }
    if ($sent -and -not $dots) {
        $fr = $wins | Where-Object { $_.Txt -match 'Depot Upgrade Radio' } | Select-Object -First 1
        if ($fr) {
            Write-Output ("Flash Radio dialog: " + $fr.Txt)
            foreach ($k in $fr.Kids) { if ($k.Txt) { Write-Output ("  [{0}#{1}] {2}" -f $k.Cls, $k.Id, $k.Txt) } }
            $b = $fr.Kids | Where-Object { $_.Cls -eq 'Button' -and $_.Id -eq 21465 } | Select-Object -First 1
            if ($b) { [W10]::Click([IntPtr]$b.H); $dots = $true; Write-Output 'clicked ...' }
        }
    }
    if ($dots) {
        Start-Sleep -Milliseconds 1500
        $wins2 = [W10]::ForPid([uint32]$p.Id)
        $dlg = $wins2 | Where-Object { $_.Cls -eq '#32770' -and $_.Txt -match 'FLASHport|Upgrade|Open|Select' } | Select-Object -First 1
        if ($dlg) {
            Write-Output ("File dialog: '" + $dlg.Txt + "'")
            foreach ($k in $dlg.Kids) {
                if ($k.Cls -eq 'ComboBox') { $items = [W10]::ComboItems([IntPtr]$k.H); Write-Output ("  ComboBox#{0} items=[{1}]" -f $k.Id, ($items -join ' | ')) }
                elseif ($k.Cls -eq 'Edit') { Write-Output ("  Edit#{0} text='{1}'" -f $k.Id, $k.Txt) }
                elseif ($k.Txt) { Write-Output ("  [{0}#{1}] {2}" -f $k.Cls, $k.Id, $k.Txt) }
            }
            $cancel = $dlg.Kids | Where-Object { $_.Cls -eq 'Button' -and $_.Id -eq 2 } | Select-Object -First 1
            if ($cancel) { [W10]::Click([IntPtr]$cancel.H); Write-Output 'cancelled file dialog' }
            Start-Sleep -Milliseconds 500
            $fr2 = $wins2 | Where-Object { $_.Txt -match 'Depot Upgrade Radio' } | Select-Object -First 1
            if ($fr2) { $c2 = $fr2.Kids | Where-Object { $_.Cls -eq 'Button' -and $_.Id -eq 21462 } | Select-Object -First 1; if ($c2) { [W10]::Click([IntPtr]$c2.H) } }
            break
        }
    }
    Start-Sleep -Milliseconds 800
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
