using System;
using System.IO;
using System.Runtime.InteropServices;

class P {
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr LoadLibrary(string p);
    [DllImport("kernel32", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr h, string n);
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetDllDirectory(string p);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int DllGetClassObject(ref Guid clsid, ref Guid iid, out IntPtr ppv);
    static Guid CLS = new Guid("DEC90FAF-8735-4DB9-A17A-D8BF745C2254");
    static Guid IID = new Guid("00000001-0000-0000-C000-000000000046");
    static bool Test(string dll) {
        try {
            IntPtr h = LoadLibrary(dll);
            if (h == IntPtr.Zero) return false;
            IntPtr p = GetProcAddress(h, "DllGetClassObject");
            if (p == IntPtr.Zero) return false;
            var fn = (DllGetClassObject)Marshal.GetDelegateForFunctionPointer(p, typeof(DllGetClassObject));
            IntPtr cf = IntPtr.Zero;
            int hr = fn(ref CLS, ref IID, out cf);
            if (hr == 0) { Console.WriteLine("FOUND IMPLEMENTER: " + dll); return true; }
            return false;
        } catch { return false; }
    }
    static int Main() {
        string[] roots = {
            @"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot",
            @"C:\Program Files (x86)\Common Files\Motorola",
            @"C:\Program Files\Common Files\Motorola",
        };
        foreach (string root in roots) {
            if (!Directory.Exists(root)) continue;
            foreach (string dll in Directory.GetFiles(root, "*.dll", SearchOption.AllDirectories)) {
                SetDllDirectory(Path.GetDirectoryName(dll));
                if (Test(dll)) return 0;
            }
        }
        Console.WriteLine("not found");
        return 1;
    }
}
