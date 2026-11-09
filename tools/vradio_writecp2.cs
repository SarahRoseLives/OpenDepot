using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [STAThread] static int Main(string[] a) {
        string port = a[0]; string recPath = a[1];
        uint ver = a.Length > 2 ? uint.Parse(a[2]) : 0;
        byte[] data = File.ReadAllBytes(recPath);
        var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        V.Open(4, ref port, null, ref uuid, 0, 0, 0, ref sysrel);
        IRcmpBundler B = null; V.GetBundlerPointer(ref B); B.EnterProgramMode();
        Console.WriteLine("opened; rec=" + data.Length + " bytes ver=" + ver);
        try {
            V.NetWriteCodeplug(ver, data);
            Console.WriteLine("NetWriteCodeplug OK");
        } catch (Exception e) {
            Console.WriteLine("NetWriteCodeplug EX " + e.GetType().Name + ": " + e.Message);
        }
        try { V.Close(ref port); } catch { }
        return 0;
    }
}
