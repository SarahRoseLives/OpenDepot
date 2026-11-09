param(
    [int]$Cmd = 32835,
    [int]$WaitSeconds = 15
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W5 {
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
            IntPtr buf=Marshal.AllocHGlobal(4096);
            SendMessageW(h,0x0148,(IntPtr)i,buf);
            outl.Add(Marshal.PtrToStringUni(buf));
            Marshal.FreeHGlobal(buf);
        }
        return outl;
    }
    public static int ComboSel(IntPtr h){ return (int)SendMessageW(h,0x0147,IntPtr.Zero,IntPtr.Zero); }
    public static int Checked(IntPtr h){ return (int)SendMessageW(h,0x00F0,IntPtr.Zero,IntPtr.Zero); }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 3
$start = Get-Date
$sent = $false
$seen = @{}
while (((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    $p.Refresh(); if ($p.HasExited) { Write-Output "process exited"; break }
    $wins = [W5]::ForPid([uint32]$p.Id)
    if (-not $sent) {
        foreach ($w in $wins) { if ($w.Cls -eq '#32770' -and -not $w.Txt) { [W5]::SendMessageW([IntPtr]$w.H, 0x0111, [IntPtr]22784, [IntPtr]::Zero) | Out-Null } }
        $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
        if ($main) {
            Write-Output ("sending cmd {0} to 0x{1:X}" -f $Cmd, $main.H)
            [W5]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]$Cmd, [IntPtr]::Zero) | Out-Null
            $sent = $true
        }
    }
    foreach ($w in $wins) {
        $sig = "$($w.Cls)|$($w.Txt)|" + (($w.Kids | ForEach-Object { "$($_.Cls):$($_.Id):$($_.Txt)" }) -join '|')
        if ($seen.ContainsKey($sig)) { continue }
        $seen[$sig] = $true
        if (-not $w.Vis -and -not $w.Txt -and $w.Kids.Count -eq 0) { continue }
        Write-Output ("{0:HH:mm:ss} WIN class='{1}' vis={2} title='{3}'" -f (Get-Date), $w.Cls, $w.Vis, $w.Txt)
        foreach ($k in $w.Kids) {
            if ($k.Cls -eq 'ComboBox') {
                $items = [W5]::ComboItems([IntPtr]$k.H)
                $sel = [W5]::ComboSel([IntPtr]$k.H)
                Write-Output ("      COMBO id={0} sel={1} items=[{2}]" -f $k.Id, $sel, ($items -join ' | '))
            } elseif ($k.Cls -eq 'Button') {
                Write-Output ("      BUTTON id={0} text='{1}' checked={2}" -f $k.Id, $k.Txt, [W5]::Checked([IntPtr]$k.H))
            } elseif ($k.Txt) {
                Write-Output ("      {0} id={1} text='{2}'" -f $k.Cls, $k.Id, $k.Txt)
            }
        }
    }
    Start-Sleep -Milliseconds 1000
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
