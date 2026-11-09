using System;
using System.Runtime.InteropServices;

class P {
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr LoadLibrary(string p);
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetDllDirectory(string p);
    [DllImport("kernel32", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr h, string n);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int DllGetClassObject(ref Guid clsid, ref Guid iid, out IntPtr ppv);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int DllRegisterServer();
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int DllUnregisterServer();
    static int Main(string[] a) {
        SetDllDirectory(@"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13");
        Guid clsid = new Guid("DEC90FAF-8735-4DB9-A17A-D8BF745C2254");
        Guid iid = new Guid("00000001-0000-0000-C000-000000000046");
        string[] dlls = {
            @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\UdcDrIsh10c.dll",
            @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\cgIsh10c.dll",
            @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\udcIsh10c.dll",
            @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\udcXAdk10c.dll",
            @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\PBT2.dll",
            @"C:\Windows\SysWOW64\NextGen10.dll",
            @"C:\Windows\SysWOW64\PatComm10.dll",
            @"C:\Windows\SysWOW64\ByteData10.dll",
            @"C:\Windows\SysWOW64\RadioSerial10.dll",
            @"C:\Windows\SysWOW64\PortManager10.dll",
            @"C:\Windows\SysWOW64\RadioCustStack10.dll",
            @"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\DataStoreDecryptionHandler.dll",
            @"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\SSLManagerComp.dll",
            @"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\ManagedOpenSsl.dll",
        };
        foreach (string d in dlls) {
            IntPtr h = LoadLibrary(d);
            if (h == IntPtr.Zero) { Console.WriteLine(d + " LOADFAIL err=" + Marshal.GetLastWin32Error()); continue; }
            IntPtr p = GetProcAddress(h, "DllGetClassObject");
            if (p == IntPtr.Zero) { Console.WriteLine(d + " no DllGetClassObject"); continue; }
            var fn = (DllGetClassObject)Marshal.GetDelegateForFunctionPointer(p, typeof(DllGetClassObject));
            IntPtr cf = IntPtr.Zero;
            int hr = fn(ref clsid, ref iid, out cf);
            Console.WriteLine(d + "  DllGetClassObject hr=0x" + hr.ToString("X8") + " cf=0x" + cf.ToInt64().ToString("X"));
            if (hr == 0 && a.Length > 0 && a[0] == "register") {
                IntPtr rs = GetProcAddress(h, "DllRegisterServer");
                if (rs != IntPtr.Zero) {
                    var rfn = (DllRegisterServer)Marshal.GetDelegateForFunctionPointer(rs, typeof(DllRegisterServer));
                    int r = rfn();
                    Console.WriteLine("   DllRegisterServer hr=0x" + r.ToString("X8"));
                }
            }
        }
        return 0;
    }
}
