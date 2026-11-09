using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    static long baseAddr;
    static void Slots(object o, Type t, string label) {
        IntPtr punk = Marshal.GetIUnknownForObject(o);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);
        Console.WriteLine("=== " + label + " ===");
        foreach (MethodInfo mi in t.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)) {
            try {
                int slot = Marshal.GetComSlotForMethodInfo(mi);
                IntPtr fn = Marshal.ReadIntPtr(vtbl, slot * 4);
                Console.WriteLine("  " + mi.Name + " slot=" + slot + " rva=0x" + (fn.ToInt64() - baseAddr).ToString("X"));
            } catch (Exception e) { Console.WriteLine("  " + mi.Name + " EX " + e.Message); }
        }
        Marshal.Release(punk);
    }
    [STAThread] static int Main() {
        foreach (ProcessModule m in Process.GetCurrentProcess().Modules)
            if (m.ModuleName.ToLower() == "nextgen10.dll") baseAddr = m.BaseAddress.ToInt64();
        Console.WriteLine("NextGen10 base=0x" + baseAddr.ToString("X"));
        object V = Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        Slots(V, typeof(IVirtualRadio), "IVirtualRadio");
        object B = Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
        Slots(B, typeof(IRcmpBundler), "IRcmpBundler");
        return 0;
    }
}
