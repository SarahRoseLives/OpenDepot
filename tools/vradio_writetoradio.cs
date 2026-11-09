using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [STAThread] static int Main(string[] a) {
        string port = a[0]; string recPath = a[1];
        byte[] data = File.ReadAllBytes(recPath);
        var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        V.Open(4, ref port, null, ref uuid, 0, 0, 0, ref sysrel);
        IRcmpBundler B = null; V.GetBundlerPointer(ref B); B.EnterProgramMode();
        RcmpBundler RB = B as RcmpBundler;
        Console.WriteLine("bundler=" + (RB != null));
        if (RB != null) { try { V.SetBundlerPointer(RB); Console.WriteLine("SetBundlerPointer ok"); } catch (Exception e) { Console.WriteLine("SetBundlerPointer EX " + e.Message); } }
        Console.WriteLine("opened; WriteToRadio " + data.Length + " bytes");
        try {
            V.WriteToRadio(data);
            Console.WriteLine("WriteToRadio OK");
        } catch (Exception e) { Console.WriteLine("WriteToRadio EX " + e.GetType().Name + ": " + e.Message); }
        try { V.Close(ref port); } catch { }
        return 0;
    }
}
