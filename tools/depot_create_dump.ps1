param(
    [int]$Cmd = 50195,
    [int]$WaitSeconds = 12
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W6 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    public class C { public long H; public int Id; public string Cls; public string Txt; public bool Vis; public bool En; }
    public class W { public long H; public string Cls; public string Txt; public bool Vis; public List<C> Kids = new List<C>(); }
    static string T(IntPtr h){ var s=new StringBuilder(4096); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    static string K(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<W> ForPid(uint pid){
        var r=new List<W>();
        EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true;
            var w=new W{ H=h.ToInt64(), Cls=K(h), Txt=T(h), Vis=IsWindowVisible(h) };
            EnumChildWindows(h,(c,cl)=>{ w.Kids.Add(new C{ H=c.ToInt64(), Id=GetDlgCtrlID(c), Cls=K(c), Txt=T(c), Vis=IsWindowVisible(c), En=IsWindowEnabled(c) }); return true; },IntPtr.Zero);
            r.Add(w); return true; },IntPtr.Zero);
        return r;
    }
    public static List<string> ComboItems(IntPtr h){
        var outl=new List<string>();
        int n=(int)SendMessageW(h,0x0146,IntPtr.Zero,IntPtr.Zero);
        for(int i=0;i<n;i++){
            IntPtr buf=Marshal.AllocHGlobal(8192);
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
$dumped = $false
while (((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    $p.Refresh(); if ($p.HasExited) { break }
    $wins = [W6]::ForPid([uint32]$p.Id)
    if (-not $sent) {
        foreach ($w in $wins) { if ($w.Cls -eq '#32770' -and -not $w.Txt) { [W6]::SendMessageW([IntPtr]$w.H, 0x0111, [IntPtr]22784, [IntPtr]::Zero) | Out-Null } }
        $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
        if ($main) { [W6]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]$Cmd, [IntPtr]::Zero) | Out-Null; $sent = $true }
    }
    if ($sent -and -not $dumped) {
        $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
        if ($dlg) {
            Start-Sleep -Milliseconds 800
            $wins = [W6]::ForPid([uint32]$p.Id)
            $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            Write-Output ("DIALOG '{0}' hwnd=0x{1:X}" -f $dlg.Txt, $dlg.H)
            foreach ($k in ($dlg.Kids | Sort-Object Id)) {
                $extra = ''
                if ($k.Cls -eq 'ComboBox') {
                    $items = [W6]::ComboItems([IntPtr]$k.H)
                    $extra = " sel=$([W6]::ComboSel([IntPtr]$k.H)) items=[" + ($items -join ' | ') + "]"
                } elseif ($k.Cls -eq 'Button') {
                    $extra = " check=$([W6]::Checked([IntPtr]$k.H))"
                }
                Write-Output ("  {0,-20} id={1,-6} vis={2,-5} en={3,-5} text='{4}'{5}" -f $k.Cls, $k.Id, $k.Vis, $k.En, $k.Txt, $extra)
            }
            $dumped = $true
        }
    }
    Start-Sleep -Milliseconds 800
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
