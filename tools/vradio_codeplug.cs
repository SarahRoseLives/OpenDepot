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
            Console.WriteLine(n + " head=" + BitConverter.ToString(b, 0, Math.Min(64, b.Length)));
            return;
        }
        Array a = o as Array;
        if (a != null) {
            Console.WriteLine(n + " Array rank=" + a.Rank + " len=" + a.Length + " elem=" + a.GetType().GetElementType());
            int lim = Math.Min(a.Length, 8);
            for (int i = 0; i < lim; i++) {
                object e = a.GetValue(i);
                Console.WriteLine("  [" + i + "] " + (e == null ? "null" : e.GetType().FullName + " " + e));
            }
            return;
        }
        Console.WriteLine(n + " value=" + o);
    }
    [STAThread] static int Main(string[] args) {
        string port = args.Length > 0 ? args[0] : "COM20";
        var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        V.Open(4, ref port, null, ref uuid, 3, 0, 5, ref sysrel);
        Console.WriteLine("opened sysrel=" + sysrel);
        IRcmpBundler B = null; V.GetBundlerPointer(ref B); B.EnterProgramMode();
        for (int t = 0; t <= 5; t++) {
            try { object v = null; V.GetVersion(t, ref v); Dump("GetVersion(" + t + ")", v); }
            catch (Exception e) { Console.WriteLine("GetVersion(" + t + ") EX " + e.Message); }
        }
        try {
            object list = null, data = null;
            V.ReadCodeplug(ref list, ref data);
            Console.WriteLine("ReadCodeplug ok");
            Dump("shoppingList", list);
            Dump("codeplugData", data);
            byte[] bb = data as byte[];
            if (bb != null) File.WriteAllBytes(@"C:\Users\Rose\Projects\OpenDepot\build\out\radio-codeplug.bin", bb);
        } catch (Exception e) {
            Console.WriteLine("ReadCodeplug EX " + e.GetType().Name + ": " + e.Message);
        }
        try {
            object il = null, id = null;
            B.ReadIshList(ref il, out id);
            Console.WriteLine("ReadIshList ok");
            Dump("ishList", il);
            Dump("ishData", id);
        } catch (Exception e) { Console.WriteLine("ReadIshList EX " + e.GetType().Name + ": " + e.Message); }
        try { V.Close(ref port); } catch { }
        return 0;
    }
}
