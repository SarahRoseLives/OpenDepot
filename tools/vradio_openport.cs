using System;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [STAThread] static int Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        var B = (IRcmpBundler)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
        try { B.OpenPort(4, ref port, null); Console.WriteLine("B.OpenPort ok"); }
        catch (Exception e) { Console.WriteLine("B.OpenPort EX " + e.GetType().Name + ": " + e.Message); return 2; }
        try { B.EnterProgramMode(); Console.WriteLine("EnterProgramMode ok"); } catch (Exception e) { Console.WriteLine("EnterProgramMode EX " + e.Message); }
        try { object d = null; B.ReadAllTypeInstances(ref d, 0x0081, 0x1004); Console.WriteLine("ReadAll 0x1004 type=" + (d == null ? "null" : d.GetType().FullName) + " len=" + (d is Array ? ((Array)d).Length : -1)); }
        catch (Exception e) { Console.WriteLine("ReadAll EX " + e.GetType().Name + ": " + e.Message); }
        try { string m = ""; B.ReadWriteRadioModelNumber(ref m, 1); Console.WriteLine("model='" + m + "'"); }
        catch (Exception e) { Console.WriteLine("model EX " + e.Message); }
        try { string s = ""; B.ReadWriteRadioSerialNumber(ref s, 1); Console.WriteLine("serial='" + s + "'"); }
        catch (Exception e) { Console.WriteLine("serial EX " + e.Message); }
        return 0;
    }
}
