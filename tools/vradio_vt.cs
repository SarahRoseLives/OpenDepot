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
    const ushort VT_EMPTY = 0, VT_BYREF = 0x4000, VT_VARIANT = 0x0C, VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int ReadAllDel(IntPtr self, IntPtr pOut, int partition, int ishType);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int ReadIshDel(IntPtr self, IntPtr pIsh, IntPtr pData);

    static void WriteVar(IntPtr a, ushort vt, IntPtr p) {
        Marshal.WriteInt16(a, 0, (short)vt);
        Marshal.WriteIntPtr(a, 8, p);
    }
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
                File.WriteAllBytes(@"build\out\radio-vt-" + label.Replace(" ", "_") + ".bin", b);
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
        Console.WriteLine("punk=" + punk.ToString("X") + " vtbl=" + vtbl.ToString("X"));

        // slot 7: ReadAllTypeInstances(VARIANT* out, int partition, int ishType)
        IntPtr outVar = Marshal.AllocHGlobal(16); WriteVar(outVar, VT_EMPTY, IntPtr.Zero);
        IntPtr fn7 = Marshal.ReadIntPtr(vtbl, 7 * 4);
        var readAll = (ReadAllDel)Marshal.GetDelegateForFunctionPointer(fn7, typeof(ReadAllDel));
        Console.WriteLine("calling slot7 fn=0x" + fn7.ToString("X"));
        int hr = readAll(punk, outVar, 0x0081, 0x1006);
        Console.WriteLine("ReadAllTypeInstances hr=0x" + hr.ToString("X8"));
        Dump("ReadAll", outVar);

        // slot 10: ReadIshList(VARIANT* in, VARIANT* out)
        IntPtr inVar = Marshal.AllocHGlobal(16);
        IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, 0);
        WriteVar(inVar, (ushort)(VT_ARRAY | VT_UI1), sa);
        IntPtr outVar2 = Marshal.AllocHGlobal(16); WriteVar(outVar2, VT_EMPTY, IntPtr.Zero);
        IntPtr fn10 = Marshal.ReadIntPtr(vtbl, 10 * 4);
        var readIsh = (ReadIshDel)Marshal.GetDelegateForFunctionPointer(fn10, typeof(ReadIshDel));
        Console.WriteLine("calling slot10 fn=0x" + fn10.ToString("X"));
        hr = readIsh(punk, inVar, outVar2);
        Console.WriteLine("ReadIshList hr=0x" + hr.ToString("X8"));
        Dump("ReadIsh", outVar2);

        Marshal.Release(punk);
        try { v.Close(ref port); Console.WriteLine("Close OK"); } catch (Exception e) { Console.WriteLine("Close EX: " + e.Message); }
    }
}
