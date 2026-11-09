using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

[StructLayout(LayoutKind.Sequential)]
public struct DISPPARAMS {
    public IntPtr rgvarg;
    public IntPtr rgdispidNamedArgs;
    public uint cArgs;
    public uint cNamedArgs;
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct VARIANT {
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr p;
}

[ComImport, Guid("00020400-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IDispatchRaw {
    [PreserveSig] int GetTypeInfoCount(out uint pctinfo);
    [PreserveSig] int GetTypeInfo(uint iTInfo, uint lcid, out IntPtr ppTInfo);
    [PreserveSig] int GetIDsOfNames(ref Guid riid,
        [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] rgszNames,
        uint cNames, uint lcid, [Out] int[] rgDispId);
    [PreserveSig] int Invoke(int dispIdMember, ref Guid riid, uint lcid, ushort wFlags,
        ref DISPPARAMS pDispParams, IntPtr pVarResult, IntPtr pExcepInfo, IntPtr puArgErr);
}

class P {
    const ushort VT_EMPTY = 0, VT_I4 = 3, VT_BYREF = 0x4000, VT_VARIANT = 0x0C,
        VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    const ushort DISPATCH_METHOD = 1;

    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayDestroy(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);
    [DllImport("oleaut32.dll")] static extern int VariantClear(IntPtr pv);

    static int DispId(IDispatchRaw d, string name) {
        int[] ids = new int[1];
        Guid nil = Guid.Empty;
        int hr = d.GetIDsOfNames(ref nil, new string[] { name }, 1, 0, ids);
        Console.WriteLine("GetIDsOfNames(" + name + ") hr=0x" + hr.ToString("X8") + " id=" + ids[0]);
        return ids[0];
    }

    static void WriteVar(IntPtr addr, ushort vt, IntPtr p) {
        Marshal.WriteInt16(addr, 0, (short)vt);
        Marshal.WriteIntPtr(addr, 8, p);
    }
    static ushort ReadVt(IntPtr addr) { return (ushort)Marshal.ReadInt16(addr, 0); }
    static IntPtr ReadP(IntPtr addr) { return Marshal.ReadIntPtr(addr, 8); }

    static void Dump(string label, IntPtr pvar) {
        ushort vt = ReadVt(pvar);
        IntPtr p = ReadP(pvar);
        Console.WriteLine(label + ": vt=0x" + vt.ToString("X4") + " p=0x" + p.ToString("X"));
        ushort baseVt = (ushort)(vt & ~VT_BYREF);
        if ((vt & VT_BYREF) != 0) {
            Dump(label + " ->", p);
            return;
        }
        if ((vt & VT_ARRAY) != 0 && (vt & 0xFF) == VT_UI1) {
            int lb, ub;
            SafeArrayGetLBound(p, 1, out lb);
            SafeArrayGetUBound(p, 1, out ub);
            int n = ub - lb + 1;
            IntPtr data;
            SafeArrayAccessData(p, out data);
            byte[] bytes = new byte[n];
            Marshal.Copy(data, bytes, 0, n);
            SafeArrayUnaccessData(p);
            Console.WriteLine("  array ui1 n=" + n);
            string s = "";
            for (int i = 0; i < Math.Min(n, 64); i++) s += bytes[i].ToString("X2") + " ";
            Console.WriteLine("  " + s);
            File.WriteAllBytes(@"build\out\radio-idisp.bin", bytes);
        }
    }

    static void Invoke(IDispatchRaw d, string name, VARIANT[] args) {
        int id = DispId(d, name);
        IntPtr argBuf = Marshal.AllocHGlobal(args.Length * 16);
        for (int i = 0; i < args.Length; i++) Marshal.StructureToPtr(args[i], argBuf + i * 16, false);
        DISPPARAMS dp = new DISPPARAMS();
        dp.rgvarg = argBuf;
        dp.cArgs = (uint)args.Length;
        Guid nil = Guid.Empty;
        int hr = d.Invoke(id, ref nil, 0, DISPATCH_METHOD, ref dp, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        Console.WriteLine("Invoke(" + name + ") hr=0x" + hr.ToString("X8"));
        Marshal.FreeHGlobal(argBuf);
    }

    static VARIANT ByRef(IntPtr pvar) { VARIANT v = new VARIANT(); v.vt = (ushort)(VT_BYREF | VT_VARIANT); v.p = pvar; return v; }
    static VARIANT I4(int x) { VARIANT v = new VARIANT(); v.vt = VT_I4; v.p = (IntPtr)x; return v; }

    [STAThread] static void Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        var v = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID u = new tagCAUUID(); string sysrel = "";
        v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
        IRcmpBundler b = null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
        string fc = ""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode=" + fc);

        IntPtr pd = Marshal.GetIDispatchForObject(b);
        Console.WriteLine("IDispatch=" + pd.ToString("X"));
        IDispatchRaw d = (IDispatchRaw)Marshal.GetObjectForIUnknown(pd);

        // ReadAllTypeInstances(VARIANT* out, int partition, int ishType)
        IntPtr outVar = Marshal.AllocHGlobal(16);
        WriteVar(outVar, VT_EMPTY, IntPtr.Zero);
        VARIANT[] args = new VARIANT[] { I4(0x1006), I4(0x0081), ByRef(outVar) };
        Invoke(d, "ReadAllTypeInstances", args);
        Dump("ReadAllTypeInstances out", outVar);

        // ReadIshList(VARIANT* in, VARIANT* out)
        IntPtr inVar = Marshal.AllocHGlobal(16);
        IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, 0);
        WriteVar(inVar, (ushort)(VT_ARRAY | VT_UI1), sa);
        IntPtr outVar2 = Marshal.AllocHGlobal(16);
        WriteVar(outVar2, VT_EMPTY, IntPtr.Zero);
        VARIANT[] args2 = new VARIANT[] { ByRef(outVar2), ByRef(inVar) };
        Invoke(d, "ReadIshList", args2);
        Dump("ReadIshList out", outVar2);

        Marshal.Release(pd);
        try { v.Close(ref port); Console.WriteLine("Close OK"); } catch (Exception e) { Console.WriteLine("Close EX: " + e.Message); }
    }
}
