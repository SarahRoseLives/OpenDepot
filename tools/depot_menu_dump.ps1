param(
    [int]$WaitSeconds = 4
)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class MenuDump {
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr hMenu);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr hMenu, int nPos);
    [DllImport("user32.dll")] public static extern int GetMenuItemID(IntPtr hMenu, int nPos);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuStringW(IntPtr hMenu, uint uIDItem, StringBuilder lpString, int nMaxCount, uint uFlag);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    const uint MF_BYPOSITION = 0x400;

    public static List<string> Lines = new List<string>();
    static void Dump(IntPtr h, int depth) {
        int n = GetMenuItemCount(h);
        for (int i = 0; i < n; i++) {
            var sb = new StringBuilder(512);
            GetMenuStringW(h, (uint)i, sb, sb.Capacity, MF_BYPOSITION);
            int id = GetMenuItemID(h, i);
            string text = sb.ToString();
            if (text.Length == 0 && GetSubMenu(h, i) == IntPtr.Zero) continue;
            Lines.Add(new string(' ', depth * 2) + "[" + id + "] " + text);
            IntPtr sub = GetSubMenu(h, i);
            if (sub != IntPtr.Zero) Dump(sub, depth + 1);
        }
    }
    public static IntPtr FindMain(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, lp) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid) {
                var sb = new StringBuilder(256);
                // class check done by caller via GetClassName omitted; take first with a menu
                if (GetMenu(h) != IntPtr.Zero) { found = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static void DoMenu(IntPtr hwnd) {
        IntPtr m = GetMenu(hwnd);
        if (m != IntPtr.Zero) Dump(m, 0);
    }
}
"@
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $WaitSeconds
$h = [MenuDump]::FindMain([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { Write-Output "main menu window not found"; if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}; exit 1 }
Write-Output ("main=0x{0:X}" -f $h.ToInt64())
[MenuDump]::DoMenu($h)
[MenuDump]::Lines | ForEach-Object { Write-Output $_ }
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
