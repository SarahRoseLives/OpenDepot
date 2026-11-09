using System;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    const ushort VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr sa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr sa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr sa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr sa, uint dim, out int ub);
    [DllImport("oleaut32.dll")] static extern int SafeArrayDestroy(IntPtr sa);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ReadAllDel(IntPtr self, IntPtr pOut, int part, int tag);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int WriteIshDel(IntPtr self, IntPtr pList);
    static IntPtr Slot(IntPtr vtbl, int slot) { return Marshal.ReadIntPtr(vtbl, slot * 4); }
    static byte[] GetSa(IntPtr sa) {
        if (sa == IntPtr.Zero) return null;
        int lb, ub;
        if (SafeArrayGetLBound(sa, 1, out lb) != 0 || SafeArrayGetUBound(sa, 1, out ub) != 0) return null;
        int n = ub - lb + 1; if (n <= 0) return new byte[0];
        IntPtr d; if (SafeArrayAccessData(sa, out d) != 0) return null;
        byte[] b = new byte[n]; Marshal.Copy(d, b, 0, n); SafeArrayUnaccessData(sa); return b;
    }
    [STAThread] static int Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        var B = (IRcmpBundler)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
        try { B.OpenPort(4, ref port, null); Console.WriteLine("OpenPort ok"); }
        catch (Exception e) { Console.WriteLine("OpenPort EX " + e.GetType().Name + ": " + e.Message); }
        try { B.EnterProgramMode(); Console.WriteLine("EnterProgramMode ok"); }
        catch (Exception e) { Console.WriteLine("EnterProgramMode EX " + e.Message); }
        IntPtr punk = Marshal.GetIUnknownForObject(B);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);
        foreach (int t in new int[] { 0x1005, 0x1004, 0x1006 }) {
            IntPtr ov = Marshal.AllocHGlobal(16);
            for (int i = 0; i < 16; i++) Marshal.WriteByte(ov, i, 0);
            var f = (ReadAllDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 11), typeof(ReadAllDel));
            int hr = f(punk, ov, 0x0081, t);
            byte[] data = hr == 0 ? GetSa(Marshal.ReadIntPtr(ov, 8)) : null;
            Console.WriteLine("Read 0x" + t.ToString("X4") + " hr=0x" + hr.ToString("X8") + " len=" + (data == null ? -1 : data.Length) +
                (data != null && data.Length > 0 ? " head=" + BitConverter.ToString(data, 0, Math.Min(16, data.Length)) : ""));
            Marshal.FreeHGlobal(ov);
        }
        return 0;
    }
}
