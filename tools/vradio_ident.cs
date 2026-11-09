using System;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int UuidDel(IntPtr self, out IntPtr bstr);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int StrIntDel(IntPtr self, out IntPtr bstr, int flag);

    static IntPtr Slot(IntPtr vtbl, int slot) { return Marshal.ReadIntPtr(vtbl, slot * 4); }

    [STAThread] static void Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        var v = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID u = new tagCAUUID(); string sysrel = "";
        v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
        IRcmpBundler b = null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
        IntPtr punk = Marshal.GetIUnknownForObject(b);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);

        try {
            var f = (UuidDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 10), typeof(UuidDel));
            IntPtr s; int hr = f(punk, out s);
            Console.WriteLine("GetRadioUUID hr=0x" + hr.ToString("X8") + " value='" + (s == IntPtr.Zero ? "" : Marshal.PtrToStringBSTR(s)) + "'");
            if (s != IntPtr.Zero) Marshal.FreeBSTR(s);
        } catch (Exception e) { Console.WriteLine("GetRadioUUID EX: " + e.Message); }

        for (int flag = 0; flag <= 1; flag++) {
            try {
                var f = (StrIntDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 17), typeof(StrIntDel));
                IntPtr s; int hr = f(punk, out s, flag);
                Console.WriteLine("Serial flag=" + flag + " hr=0x" + hr.ToString("X8") + " value='" + (s == IntPtr.Zero ? "" : Marshal.PtrToStringBSTR(s)) + "'");
                if (s != IntPtr.Zero) Marshal.FreeBSTR(s);
            } catch (Exception e) { Console.WriteLine("Serial flag=" + flag + " EX: " + e.Message); }
        }
        for (int flag = 0; flag <= 1; flag++) {
            try {
                var f = (StrIntDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 17), typeof(StrIntDel));
                IntPtr s; int hr = f(punk, out s, flag);
                Console.WriteLine("Model flag=" + flag + " hr=0x" + hr.ToString("X8") + " value='" + (s == IntPtr.Zero ? "" : Marshal.PtrToStringBSTR(s)) + "'");
                if (s != IntPtr.Zero) Marshal.FreeBSTR(s);
            } catch (Exception e) { Console.WriteLine("Model flag=" + flag + " EX: " + e.Message); }
        }

        Marshal.Release(punk);
        try { v.Close(ref port); Console.WriteLine("Close OK"); } catch (Exception e) { Console.WriteLine("Close EX: " + e.Message); }
    }
}
