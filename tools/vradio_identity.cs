using System;
using System.Runtime.InteropServices;
using NEXTGENLib;

class P {
    [STAThread] static int Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        string model = a.Length > 1 ? a[1] : "H46UCF9PW6BN";
        string serial = a.Length > 2 ? a[2] : "205CNX2873";
        var B = (IRcmpBundler)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
        try {
            B.Open(4, ref port, null);
            Console.WriteLine("B.Open ok port=" + port);
        } catch (Exception e) { Console.WriteLine("B.Open EX " + e.GetType().Name + ": " + e.Message); return 2; }
        try { B.EnterProgramMode(); Console.WriteLine("EnterProgramMode ok"); } catch (Exception e) { Console.WriteLine("EnterProgramMode EX " + e.Message); }
        try { string m = model; B.ReadWriteRadioModelNumber(ref m, 0); Console.WriteLine("write model ok"); } catch (Exception e) { Console.WriteLine("write model EX " + e.GetType().Name + ": " + e.Message); }
        try { string s = serial; B.ReadWriteRadioSerialNumber(ref s, 0); Console.WriteLine("write serial ok"); } catch (Exception e) { Console.WriteLine("write serial EX " + e.GetType().Name + ": " + e.Message); }
        try { string m = ""; B.ReadWriteRadioModelNumber(ref m, 1); Console.WriteLine("read model='" + m + "'"); } catch (Exception e) { Console.WriteLine("read model EX " + e.Message); }
        try { string s = ""; B.ReadWriteRadioSerialNumber(ref s, 1); Console.WriteLine("read serial='" + s + "'"); } catch (Exception e) { Console.WriteLine("read serial EX " + e.Message); }
        return 0;
    }
}
