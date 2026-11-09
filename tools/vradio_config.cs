using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    static void Dump(string n, object o) {
        if (o == null) { Console.WriteLine(n + " = null"); return; }
        Console.WriteLine(n + " type=" + o.GetType().FullName);
        byte[] b = o as byte[];
        if (b != null) {
            Console.WriteLine(n + " byte[] len=" + b.Length);
            for (int off = 0; off < b.Length; off += 16) {
                int lim = Math.Min(16, b.Length - off);
                string hex = "", asc = "";
                for (int i = 0; i < lim; i++) { hex += b[off + i].ToString("X2") + " "; asc += (b[off + i] >= 32 && b[off + i] < 127) ? (char)b[off + i] : '.'; }
                Console.WriteLine("  {0:X4}  {1,-48} {2}", off, hex, asc);
            }
            return;
        }
        Array a = o as Array;
        if (a != null) {
            Console.WriteLine(n + " Array rank=" + a.Rank + " len=" + a.Length + " elem=" + a.GetType().GetElementType());
            int lim = Math.Min(a.Length, 32);
            for (int i = 0; i < lim; i++) { object e = a.GetValue(i); Console.WriteLine("  [" + i + "] " + (e == null ? "null" : e.GetType().FullName + " = " + e)); }
            return;
        }
        Console.WriteLine(n + " value=" + o);
    }
    [STAThread] static int Main(string[] args) {
        string port = args.Length > 0 ? args[0] : "COM20";
        var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        V.Open(4, ref port, null, ref uuid, 0, 0, 0, ref sysrel);
        Console.WriteLine("opened sysrel='" + sysrel + "'");
        IRcmpBundler B = null; V.GetBundlerPointer(ref B); B.EnterProgramMode();
        try { object rc = null; V.ReadRadioConfiguration(ref rc, 0); Dump("V.ReadRadioConfiguration", rc); }
        catch (Exception e) { Console.WriteLine("V.ReadRadioConfiguration EX " + e.Message); }
        try { object rc = null; B.ReadRadioConfiguration(ref rc, 0); Dump("B.ReadRadioConfiguration", rc); }
        catch (Exception e) { Console.WriteLine("B.ReadRadioConfiguration EX " + e.Message); }
        for (int k = 0; k <= 6; k++) {
            try { object v = null; V.GetVersion(k, ref v); Dump("GetVersion(" + k + ")", v); }
            catch (Exception e) { Console.WriteLine("GetVersion(" + k + ") EX " + e.Message); }
        }
        try { V.Close(ref port); } catch { }
        return 0;
    }
}
