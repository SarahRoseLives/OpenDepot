using System;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [STAThread] static int Main(string[] a) {
        string port0 = a.Length > 0 ? a[0] : "COM20";
        string[] cpgs = { "1001", "24", "1005", "0", "25", "1000" };
        string[] sysrs = { "1005", "24", "0", "1001", "1000" };
        foreach (string ch in cpgs) {
            foreach (string sh in sysrs) {
                uint cpg = Convert.ToUInt32(ch, 16);
                uint sys = Convert.ToUInt32(sh, 16);
                string port = port0; string sysrel = "";
                object V = null;
                try {
                    V = Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
                    tagCAUUID uuid = new tagCAUUID();
                    ((IVirtualRadio)V).Open(4, ref port, null, ref uuid, cpg, 0, sys, ref sysrel);
                    Console.WriteLine("cpg=0x" + cpg.ToString("X") + " sys=0x" + sys.ToString("X") + " OPEN OK sysrel='" + sysrel + "'");
                    try { ((IVirtualRadio)V).Close(ref port); } catch { }
                    return 0;
                } catch (Exception e) {
                    Console.WriteLine("cpg=0x" + cpg.ToString("X") + " sys=0x" + sys.ToString("X") + " EX " + e.Message);
                }
            }
        }
        return 1;
    }
}
