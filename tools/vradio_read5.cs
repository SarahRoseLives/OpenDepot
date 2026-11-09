using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct VARIANT {
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr p;
}

class P {
    const ushort VT_EMPTY = 0, VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int ReadAllDel(IntPtr self, IntPtr pOut, int partition, int ishType);

    static IntPtr Slot(IntPtr vtbl, int slot) { return Marshal.ReadIntPtr(vtbl, slot * 4); }

    static void Dump(string label, IntPtr a) {
        ushort vt = (ushort)Marshal.ReadInt16(a, 0);
        IntPtr p = Marshal.ReadIntPtr(a, 8);
        Console.WriteLine(label + ": vt=0x" + vt.ToString("X4") + " p=0x" + p.ToString("X"));
        if ((vt & VT_ARRAY) != 0 && (vt & 0xFF) == VT_UI1) {
            int lb, ub; SafeArrayGetLBound(p, 1, out lb); SafeArrayGetUBound(p, 1, out ub);
            int n = ub - lb + 1; Console.WriteLine("  n=" + n);
            if (n > 0) {
                IntPtr d; SafeArrayAccessData(p, out d);
                byte[] b = new byte[n]; Marshal.Copy(d, b, 0, n); SafeArrayUnaccessData(p);
                string s = ""; for (int i = 0; i < Math.Min(n, 64); i++) s += b[i].ToString("X2") + " ";
                Console.WriteLine("  " + s);
                File.WriteAllBytes(@"build\out\readall-" + label + ".bin", b);
            }
        }
    }

    [STAThread] static void Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        var v = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID u = new tagCAUUID(); string sysrel = "";
        v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
        IRcmpBundler b = null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
        string fc = ""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode=" + fc);

        IntPtr punk = Marshal.GetIUnknownForObject(b);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);

        int[][] specs = new int[][] {
            new int[] {0x0081, 0x1006},
            new int[] {0x0081, 0x1004},
            new int[] {0x0081, 0x1005},
            new int[] {0x0080, 0x000D},
            new int[] {0x0080, 0x0FFD},
            new int[] {0x0080, 0x0FFB},
        };
        foreach (var s in specs) {
            IntPtr outVar = Marshal.AllocHGlobal(16);
            Marshal.WriteInt16(outVar, 0, 0); Marshal.WriteIntPtr(outVar, 8, IntPtr.Zero);
            var f = (ReadAllDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 11), typeof(ReadAllDel));
            int hr = f(punk, outVar, s[0], s[1]);
            Console.WriteLine("ReadAllTypeInstances(0x" + s[0].ToString("X") + ",0x" + s[1].ToString("X") + ") hr=0x" + hr.ToString("X8"));
            Dump("0x" + s[0].ToString("X") + "_0x" + s[1].ToString("X"), outVar);
        }

        Marshal.Release(punk);
        try { v.Close(ref port); Console.WriteLine("Close OK"); } catch (Exception e) { Console.WriteLine("Close EX: " + e.Message); }
    }
}
