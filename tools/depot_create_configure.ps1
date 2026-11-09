param(
    [string]$TypeText = "Model III",
    [string]$ModelText = "H46UCF9PW6BN",
    [int]$WaitSeconds = 12
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class W7 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    public class C { public long H; public int Id; public string Cls; public string Txt; public bool En; }
    public class W { public long H; public string Cls; public string Txt; public List<C> Kids = new List<C>(); }
    static string T(IntPtr h){ var s=new StringBuilder(4096); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    static string K(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<W> ForPid(uint pid){
        var r=new List<W>();
        EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true;
            var w=new W{ H=h.ToInt64(), Cls=K(h), Txt=T(h) };
            EnumChildWindows(h,(c,cl)=>{ w.Kids.Add(new C{ H=c.ToInt64(), Id=GetDlgCtrlID(c), Cls=K(c), Txt=T(c), En=IsWindowEnabled(c) }); return true; },IntPtr.Zero);
            r.Add(w); return true; },IntPtr.Zero);
        return r;
    }
    public static List<string> Items(IntPtr h){ var o=new List<string>(); int n=(int)SendMessageW(h,0x0146,IntPtr.Zero,IntPtr.Zero); for(int i=0;i<n;i++){ IntPtr b=Marshal.AllocHGlobal(8192); SendMessageW(h,0x0148,(IntPtr)i,b); o.Add(Marshal.PtrToStringUni(b)); Marshal.FreeHGlobal(b);} return o; }
    public static int Sel(IntPtr h){ return (int)SendMessageW(h,0x0147,IntPtr.Zero,IntPtr.Zero); }
    public static int FindExact(IntPtr h,string s){ IntPtr b=Marshal.AllocHGlobal(4096); var by=Encoding.Unicode.GetBytes(s+"\0"); Marshal.Copy(by,0,b,by.Length); int r=(int)SendMessageW(h,0x0158,new IntPtr(-1),b); Marshal.FreeHGlobal(b); return r; }
    public static IntPtr ItemPtr(IntPtr h,int i){ IntPtr b=Marshal.AllocHGlobal(8192); int r=(int)SendMessageW(h,0x0148,(IntPtr)i,b); string s=Marshal.PtrToStringUni(b); Marshal.FreeHGlobal(b); return (IntPtr)r; }
    public static void SetCombo(IntPtr dlg, IntPtr combo, int idx){ SendMessageW(combo,0x014E,(IntPtr)idx,IntPtr.Zero); int id=GetDlgCtrlID(combo); SendMessageW(dlg,0x0111,(IntPtr)((1<<16)|id),combo); SendMessageW(dlg,0x0111,(IntPtr)((9<<16)|id),combo); }
    public static int ListCount(IntPtr h){ return (int)SendMessageW(h,0x018B,IntPtr.Zero,IntPtr.Zero); }
    public static string ListItem(IntPtr h,int i){ IntPtr b=Marshal.AllocHGlobal(4096); int n=(int)SendMessageW(h,0x0189,(IntPtr)i,b); string s=Marshal.PtrToStringUni(b,n); Marshal.FreeHGlobal(b); return s; }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 3
$start = Get-Date
$sent = $false
$done = $false
while (((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    $p.Refresh(); if ($p.HasExited) { break }
    $wins = [W7]::ForPid([uint32]$p.Id)
    if (-not $sent) {
        foreach ($w in $wins) { if ($w.Cls -eq '#32770' -and -not $w.Txt) { [W7]::SendMessageW([IntPtr]$w.H, 0x0111, [IntPtr]22784, [IntPtr]::Zero) | Out-Null } }
        $main = $wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
        if ($main) { [W7]::PostMessageW([IntPtr]$main.H, 0x0111, [IntPtr]50195, [IntPtr]::Zero) | Out-Null; $sent = $true }
    }
    if ($sent -and -not $done) {
        $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
        if ($dlg) {
            Start-Sleep -Milliseconds 600
            $wins = [W7]::ForPid([uint32]$p.Id); $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            function G($id){ $dlg.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
            $type = G 21420; $model = G 21423; $band = G 21419; $radio = G 21457; $pkg = G 21461; $list = G 21478
            $ti = [W7]::FindExact([IntPtr]$type.H, "ALL")
            if ($ti -lt 0) { $ti = 3 }
            [W7]::SetCombo([IntPtr]$dlg.H, [IntPtr]$type.H, $ti); Start-Sleep -Milliseconds 600
            $wins = [W7]::ForPid([uint32]$p.Id); $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            function G2($id){ $dlg.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
            $band = G2 21419; $radio = G2 21457
            if ($band -and $band.En) { $bi=[W7]::FindExact([IntPtr]$band.H,"UHF"); if($bi -ge 0){ [W7]::SetCombo([IntPtr]$dlg.H,[IntPtr]$band.H,$bi); Start-Sleep -Milliseconds 400 } }
            $wins = [W7]::ForPid([uint32]$p.Id); $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            function G2b($id){ $dlg.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
            $radio = G2b 21457
            if ($radio -and $radio.En) { $ri=[W7]::FindExact([IntPtr]$radio.H,"XTS5000"); if($ri -ge 0){ [W7]::SetCombo([IntPtr]$dlg.H,[IntPtr]$radio.H,$ri); Start-Sleep -Milliseconds 400 } }
            $wins = [W7]::ForPid([uint32]$p.Id); $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            function G2c($id){ $dlg.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
            $model = G2c 21423
            $mi = [W7]::FindExact([IntPtr]$model.H, $ModelText)
            Write-Output ("type index={0} model index={1}" -f $ti, $mi)
            if ($mi -ge 0) { [W7]::SetCombo([IntPtr]$dlg.H, [IntPtr]$model.H, $mi); Start-Sleep -Milliseconds 800 }
            $wins = [W7]::ForPid([uint32]$p.Id); $dlg = $wins | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1
            function G3($id){ $dlg.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
            foreach($cid in @(21419,21457,21420,21423,21461)){ $c=G3 $cid; if($c){ $items=[W7]::Items([IntPtr]$c.H); Write-Output ("combo {0} sel={1} en={2} items=[{3}]" -f $cid, [W7]::Sel([IntPtr]$c.H), $c.En, ($items -join ' | ')) } }
            foreach($eid in @(21410,21408,21404,21402)){ $e=G3 $eid; if($e){ Write-Output ("edit {0} text='{1}'" -f $eid, $e.Txt) } }
            $lb=G3 21478; if($lb){ $n=[W7]::ListCount([IntPtr]$lb.H); Write-Output ("listbox 21478 count={0}" -f $n); for($i=0;$i -lt $n;$i++){ Write-Output ("   [{0}] {1}" -f $i, [W7]::ListItem([IntPtr]$lb.H,$i)) } }
            # cancel
            $cancel = G3 21400; if($cancel){ [W7]::SendMessageW([IntPtr]$cancel.H,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
            $done = $true
        }
    }
    Start-Sleep -Milliseconds 700
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
