using System;
using System.Reflection;
using System.Runtime.InteropServices;

class P {
    static void Dump(string n, object o) {
        if (o == null) { Console.WriteLine(n + " = null"); return; }
        Console.WriteLine(n + " type=" + o.GetType().FullName);
        byte[] b = o as byte[];
        if (b != null) {
            Console.WriteLine(n + " byte[] len=" + b.Length + " head=" + BitConverter.ToString(b, 0, Math.Min(128, b.Length)));
            return;
        }
        Array a = o as Array;
        if (a != null) {
            Console.WriteLine(n + " Array rank=" + a.Rank + " len=" + a.Length + " elem=" + a.GetType().GetElementType());
            int lim = Math.Min(a.Length, 16);
            for (int i = 0; i < lim; i++) {
                object e = a.GetValue(i);
                Console.WriteLine("  [" + i + "] " + (e == null ? "null" : e.GetType().FullName + " = " + e));
            }
            return;
        }
        Console.WriteLine(n + " value=" + o);
    }
    static object Invoke(object o, string name, object[] args, bool byref) {
        Type t = o.GetType();
        ParameterModifier[] pm = null;
        if (byref && args.Length > 0) {
            pm = new ParameterModifier[] { new ParameterModifier(args.Length) };
            for (int i = 0; i < args.Length; i++) pm[0][i] = true;
        }
        return t.InvokeMember(name, BindingFlags.InvokeMethod, null, o, args, pm, null, null);
    }
    static void Main(string[] a) {
        Type t = Type.GetTypeFromProgID("PatComm10.PatCommProto10.1");
        if (t == null) { Console.WriteLine("ProgID not found"); return; }
        object p = Activator.CreateInstance(t);
        object[] ga = new object[] { null, 0 };
        try { Invoke(p, "GetConnectedModemFromPortMgr", ga, true); } catch (Exception e) { Console.WriteLine("GetConnectedModemFromPortMgr EX " + e.Message); }
        string port = ga[0] as string; int flags = (int)ga[1];
        Console.WriteLine("port='" + port + "' flags=" + flags);
        if (string.IsNullOrEmpty(port)) port = "COM20";
        try { Invoke(p, "Open", new object[] { 4, flags, 0 }, false); Console.WriteLine("Open(4) ok"); }
        catch (Exception e) { Console.WriteLine("Open EX " + e.Message); }
        object[] ra = new object[] { null };
        try { Invoke(p, "ReadRadioConfiguration", ra, true); Dump("radioConfig", ra[0]); }
        catch (Exception e) { Console.WriteLine("ReadRadioConfiguration EX " + e.Message); }
        object[] cv = new object[] { null };
        try { Invoke(p, "GetCodeplugVersion", cv, true); Dump("codeplugVersion", cv[0]); }
        catch (Exception e) { Console.WriteLine("GetCodeplugVersion EX " + e.Message); }
        for (int k = 0; k <= 6; k++) {
            object[] v2 = new object[] { k, null };
            try { Invoke(p, "GetVersion", v2, true); Dump("GetVersion(" + k + ")", v2[1]); }
            catch (Exception e) { Console.WriteLine("GetVersion(" + k + ") EX " + e.Message); }
        }
        try { Invoke(p, "Close", new object[0], false); } catch { }
    }
}
