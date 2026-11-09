param(
    [string]$Out = 'C:\Users\Rose\Projects\OpenDepot\build\out\depot-conventional-ref.cpg',
    [string]$Band = 'UHF',
    [string]$Radio = 'XTS5000',
    [string]$Type = 'Model II',
    [string]$ModelText = 'AUTO',
    [string]$Serial = '0000000000',
    [string]$Package = 'P25 MDC Conv Data',
    [int]$WaitSeconds = 70
)
Add-Type -TypeDefinition @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class W8 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMsgStr(IntPtr h, uint msg, IntPtr wp, string lp);
    public class C { public long H; public int Id; public string Cls; public string Txt; public bool En; }
    public class W { public long H; public string Cls; public string Txt; public List<C> Kids = new List<C>(); }
    static string T(IntPtr h){ var s=new StringBuilder(4096); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    static string K(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<W> ForPid(uint pid){ var r=new List<W>(); EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true; var w=new W{H=h.ToInt64(),Cls=K(h),Txt=T(h)}; EnumChildWindows(h,(c,cl)=>{ w.Kids.Add(new C{H=c.ToInt64(),Id=GetDlgCtrlID(c),Cls=K(c),Txt=T(c),En=IsWindowEnabled(c)}); return true; },IntPtr.Zero); r.Add(w); return true; },IntPtr.Zero); return r; }
    public static List<string> Items(IntPtr h){ var o=new List<string>(); int n=(int)SendMessageW(h,0x0146,IntPtr.Zero,IntPtr.Zero); for(int i=0;i<n;i++){ IntPtr b=Marshal.AllocHGlobal(8192); SendMessageW(h,0x0148,(IntPtr)i,b); o.Add(Marshal.PtrToStringUni(b)); Marshal.FreeHGlobal(b);} return o; }
    public static int Sel(IntPtr h){ return (int)SendMessageW(h,0x0147,IntPtr.Zero,IntPtr.Zero); }
    public static int FindExact(IntPtr h,string s){ IntPtr b=Marshal.AllocHGlobal(4096); var by=Encoding.Unicode.GetBytes(s+"\0"); Marshal.Copy(by,0,b,by.Length); int r=(int)SendMessageW(h,0x0158,new IntPtr(-1),b); Marshal.FreeHGlobal(b); return r; }
    public static void SetCombo(IntPtr dlg, IntPtr combo, int idx){ SendMessageW(combo,0x014E,(IntPtr)idx,IntPtr.Zero); int id=GetDlgCtrlID(combo); SendMessageW(dlg,0x0111,(IntPtr)((1<<16)|id),combo); SendMessageW(dlg,0x0111,(IntPtr)((9<<16)|id),combo); }
    public static int SelectCombo(IntPtr dlg, IntPtr combo, string s){ int r=(int)SendMsgStr(combo,0x014D,new IntPtr(-1),s); int id=GetDlgCtrlID(combo); SendMessageW(dlg,0x0111,(IntPtr)((1<<16)|id),combo); SendMessageW(dlg,0x0111,(IntPtr)((9<<16)|id),combo); return r; }
    public static void Click(IntPtr h){ SendMessageW(h,0x00F5,IntPtr.Zero,IntPtr.Zero); }
    public static void SetText(IntPtr h,string s){ SendMsgStr(h,0x000C,IntPtr.Zero,s); }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
function G($d,$id){ $d.Kids | Where-Object { $_.Id -eq $id } | Select-Object -First 1 }
function Refetch($procId){ $w=[W8]::ForPid([uint32]$procId); $w | Where-Object { $_.Txt -match 'Create Codeplug' } | Select-Object -First 1 }
Remove-Item -LiteralPath $Out -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 3
$start = Get-Date; $sent = $false; $ok = $false; $saved = $false
while (((Get-Date)-$start).TotalSeconds -lt $WaitSeconds) {
    $p.Refresh(); if($p.HasExited){ break }
    $wins=[W8]::ForPid([uint32]$p.Id)
    if(-not $sent){
        $main=$wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
        if($main){ [W8]::SendMessageW([IntPtr]$main.H,0x0111,[IntPtr]50195,[IntPtr]::Zero) | Out-Null; $sent=$true; Start-Sleep -Seconds 2 }
    }
    if($sent -and -not $ok){
        $dlg=Refetch $p.Id
        if($dlg){
            $type=G $dlg 21420
            if($type){ $ti=[W8]::FindExact([IntPtr]$type.H,$Type); if($ti -lt 0){$ti=1}; [W8]::SetCombo([IntPtr]$dlg.H,[IntPtr]$type.H,$ti); Start-Sleep -Milliseconds 800 }
            $dlg=Refetch $p.Id; $radio=G $dlg 21457; $rr=-99
            if($radio){ $rr=[W8]::SelectCombo([IntPtr]$dlg.H,[IntPtr]$radio.H,$Radio); Start-Sleep -Milliseconds 1200 }
            $dlg=Refetch $p.Id; $band=G $dlg 21419
            if($band){ [W8]::SelectCombo([IntPtr]$dlg.H,[IntPtr]$band.H,$Band) | Out-Null; Start-Sleep -Milliseconds 700 }
            $dlg=Refetch $p.Id; $model=G $dlg 21423
            if($model){ $mi=-1; if($ModelText -and $ModelText -ne 'AUTO'){ $mi=[W8]::FindExact([IntPtr]$model.H,$ModelText) }; if($mi -lt 0){$mi=1}; [W8]::SetCombo([IntPtr]$dlg.H,[IntPtr]$model.H,$mi); Start-Sleep -Milliseconds 900 }
            $dlg=Refetch $p.Id; $pkg=G $dlg 21461
            if($pkg){ $pi=[W8]::FindExact([IntPtr]$pkg.H,$Package); if($pi -lt 0){$pi=3}; [W8]::SetCombo([IntPtr]$dlg.H,[IntPtr]$pkg.H,$pi); Start-Sleep -Milliseconds 1200 }
            $dlg=Refetch $p.Id; $serial=G $dlg 21410
            if($serial){ [W8]::SetText([IntPtr]$serial.H,$Serial); Start-Sleep -Milliseconds 500 }
            $dlg=Refetch $p.Id; $r2=G $dlg 21457; $m2=G $dlg 21423
            if($r2){ Write-Output ("RADIO ret=" + $rr + " sel=" + [W8]::Sel([IntPtr]$r2.H) + " want=" + $Radio) }
            if($m2){ $items=[W8]::Items([IntPtr]$m2.H); $first = if($items.Count -gt 0){$items[0]}else{''}; Write-Output ("MODEL sel=" + [W8]::Sel([IntPtr]$m2.H) + " count=" + $items.Count + " first=" + $first) }
            $btn=G $dlg 21406
            if($btn -and $btn.En){ [W8]::Click([IntPtr]$btn.H); $ok=$true; Write-Output "clicked OK" }
        }
    }
    if($ok -and -not $saved){
        Start-Sleep -Seconds 4
        $wins=[W8]::ForPid([uint32]$p.Id); $main=$wins | Where-Object { $_.Cls -eq 'AfxMDIFrame42' } | Select-Object -First 1
        if($main){
            [W8]::SendMessageW([IntPtr]$main.H,0x0111,[IntPtr]57604,[IntPtr]::Zero) | Out-Null; Start-Sleep -Seconds 2
            $wins=[W8]::ForPid([uint32]$p.Id); $saveDlg=$null
            foreach($w in $wins){ if($w.Cls -ne '#32770'){continue}; $hs=$w.Kids | Where-Object { $_.Cls -eq 'Button' -and ($_.Txt -match '^&?Save$') }; $he=$w.Kids | Where-Object { $_.Cls -eq 'Edit' }; if($hs -and $he){$saveDlg=$w;break} }
            if($saveDlg){ $edit=$saveDlg.Kids | Where-Object { $_.Cls -eq 'Edit' } | Select-Object -First 1; [W8]::SetText([IntPtr]$edit.H,$Out); Start-Sleep -Milliseconds 400; $sb=$saveDlg.Kids | Where-Object { $_.Cls -eq 'Button' -and ($_.Txt -match '^&?Save$') } | Select-Object -First 1; [W8]::Click([IntPtr]$sb.H); Write-Output ("save -> "+$Out); $saved=$true; Start-Sleep -Seconds 3 }
            else { $saved=$true; Write-Output 'Save As dialog not found' }
        }
    }
    if($saved){ break }
    Start-Sleep -Milliseconds 500
}
if(-not $p.HasExited){ $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if(-not $p.HasExited){ Stop-Process -Id $p.Id -Force } }
if(Test-Path -LiteralPath $Out){ Write-Output ("wrote " + (Get-Item -LiteralPath $Out).Length + " bytes") } else { Write-Output 'no output file' }
