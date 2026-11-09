param(
    [string]$File = "C:\Users\Rose\Projects\OpenDepot\build\out\depot-conventional-ref.cpg",
    [int]$WaitSeconds = 14
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TW {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct TVITEMW {
        public uint mask; public IntPtr hItem; public uint state; public uint stateMask;
        public IntPtr pszText; public int cchTextMax; public int iImage; public int iSelectedImage;
        public int cChildren; public IntPtr lParam;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")]
    public static extern IntPtr SendTV(IntPtr h, uint msg, IntPtr wp, ref TVITEMW item);
    public static IntPtr Main = IntPtr.Zero;
    public static string WinText(IntPtr h){ var s=new StringBuilder(1024); GetWindowTextW(h,s,s.Capacity); return s.ToString(); }
    public static string WinClass(IntPtr h){ var s=new StringBuilder(256); GetClassNameW(h,s,s.Capacity); return s.ToString(); }
    public static List<IntPtr> Kids(uint pid){
        Main=IntPtr.Zero; var r=new List<IntPtr>();
        EnumWindows((h,lp)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p!=pid) return true;
            if(WinClass(h)=="AfxMDIFrame42"){ Main=h; EnumChildWindows(h,(c,cl)=>{ r.Add(c); return true; },IntPtr.Zero); }
            return true; },IntPtr.Zero);
        return r;
    }
    public static string Text(IntPtr tree, IntPtr item){
        var it=new TVITEMW(); it.mask=0x1; it.hItem=item; it.cchTextMax=512;
        IntPtr buf=Marshal.AllocHGlobal(2048);
        it.pszText=buf;
        SendTV(tree,0x113E,IntPtr.Zero,ref it);
        string s=Marshal.PtrToStringUni(buf);
        Marshal.FreeHGlobal(buf);
        return s;
    }
    public static void Walk(IntPtr tree, IntPtr item, int depth){
        for(int guard=0; guard<2000 && item!=IntPtr.Zero; ++guard){
            Console.WriteLine(new string(' ',depth*2)+"["+Text(tree,item)+"]");
            IntPtr child=SendMessageW(tree,0x110A,(IntPtr)4,item);
            if(child!=IntPtr.Zero) Walk(tree,child,depth+1);
            item=SendMessageW(tree,0x110A,(IntPtr)1,item);
        }
    }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -ArgumentList "`"$File`"" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $WaitSeconds
$kids=[TW]::Kids([uint32]$p.Id)
$main=[TW]::Main
foreach($k in $kids){
    Write-Output ("child class='{0}' text='{1}'" -f [TW]::WinClass($k), [TW]::WinText($k))
}
foreach($k in $kids){
    if([TW]::WinClass($k) -eq 'SysTreeView32'){
        Write-Output "=== tree ==="
        $root=[TW]::SendMessageW($k,0x110A,[IntPtr]0,[IntPtr]::Zero)
        if($root -ne [IntPtr]::Zero){ [TW]::Walk($k,$root,0) }
    }
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
