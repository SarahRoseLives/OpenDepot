using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using NEXTGENLib;

[StructLayout(LayoutKind.Explicit, Size = 16)]
struct VARIANT { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; }

class P {
    const ushort VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr sa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr sa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayDestroy(IntPtr sa);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int WriteCpDel(IntPtr self, IntPtr pVar);
    [STAThread] static int Main(string[] a) {
        string port = a[0]; string recPath = a[1];
        byte[] data = File.ReadAllBytes(recPath);
        var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        V.Open(4, ref port, null, ref uuid, 0, 0, 0, ref sysrel);
        IRcmpBundler B = null; V.GetBundlerPointer(ref B); B.EnterProgramMode();
        IntPtr punk = Marshal.GetIUnknownForObject(V);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);
        foreach (string m in new string[] { "ReadCodeplug", "WriteCodeplug", "NetWriteCodeplug", "ReadRadioConfiguration", "GetVersion" }) {
            MethodInfo mi = typeof(IVirtualRadio).GetMethod(m);
            if (mi != null) Console.WriteLine(m + " slot=" + Marshal.GetComSlotForMethodInfo(mi));
        }
        MethodInfo mw = typeof(IVirtualRadio).GetMethod("WriteCodeplug");
        int slot = Marshal.GetComSlotForMethodInfo(mw);
        IntPtr pv = Marshal.AllocHGlobal(16);
        for (int i = 0; i < 16; i++) Marshal.WriteByte(pv, i, 0);
        Marshal.WriteInt16(pv, 0, (short)(VT_ARRAY | VT_UI1));
        IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, (uint)data.Length);
        IntPtr d; SafeArrayAccessData(sa, out d); Marshal.Copy(data, 0, d, data.Length); SafeArrayUnaccessData(sa);
        Marshal.WriteIntPtr(pv, 8, sa);
        IntPtr fn = Marshal.ReadIntPtr(vtbl, slot * 4);
        var f = (WriteCpDel)Marshal.GetDelegateForFunctionPointer(fn, typeof(WriteCpDel));
        int hr = f(punk, pv);
        Console.WriteLine("WriteCodeplug raw hr=0x" + hr.ToString("X8"));
        SafeArrayDestroy(sa); Marshal.FreeHGlobal(pv); Marshal.Release(punk);
        try { V.Close(ref port); } catch { }
        return 0;
    }
}
