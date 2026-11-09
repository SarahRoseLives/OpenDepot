using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct VARIANT { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; }

class P {
    const ushort VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);
    [DllImport("oleaut32.dll")] static extern int SafeArrayDestroy(IntPtr psa);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int ReadIshDel(IntPtr self, IntPtr pIn, IntPtr pOut);

    static IntPtr Slot(IntPtr vtbl, int slot) { return Marshal.ReadIntPtr(vtbl, slot * 4); }
    static byte[] GetSa(IntPtr sa) {
        int lb, ub; if (SafeArrayGetLBound(sa, 1, out lb) != 0 || SafeArrayGetUBound(sa, 1, out ub) != 0) return null;
        int n = ub - lb + 1; if (n <= 0) return new byte[0];
        IntPtr d; if (SafeArrayAccessData(sa, out d) != 0) return null;
        byte[] b = new byte[n]; Marshal.Copy(d, b, 0, n); SafeArrayUnaccessData(sa); return b;
    }

    static byte[] FindRecord(byte[] all, uint id) {
        int o = 0;
        while (o + 6 <= all.Length) {
            uint rid = (uint)((all[o] << 24) | (all[o + 1] << 16) | (all[o + 2] << 8) | all[o + 3]);
            int len = (all[o + 4] << 8) | all[o + 5];
            if (o + 6 + len > all.Length) break;
            if (rid == id) { byte[] r = new byte[6 + len]; Array.Copy(all, o, r, 0, r.Length); return r; }
            o += 6 + len;
        }
        return null;
    }

    [STAThread] static void Main(string[] a) {
        string port = a.Length > 0 ? a[0] : "COM20";
        byte[] backup = File.ReadAllBytes(@"build\out\backup.rec");
        byte[] rec = FindRecord(backup, 0x10060000);
        if (rec == null) { Console.WriteLine("record not found"); return; }
        Console.WriteLine("record len=" + rec.Length);

        // one ISH item: tag, inst, partition, length, payload(len)
        int len = (rec[4] << 8) | rec[5];
        var list = new List<byte>();
        list.Add(0x10); list.Add(0x06); list.Add(0); list.Add(0); list.Add(0x00); list.Add(0x81);
        list.Add((byte)(len >> 8)); list.Add((byte)len);
        for (int i = 0; i < len; i++) list.Add(rec[6 + i]);
        list.Add(0); list.Add(0); // terminator
        byte[] inData = list.ToArray();
        Console.WriteLine("ish list bytes=" + inData.Length);

        var v = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
        tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
        v.Open(4, ref port, null, ref uuid, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
        IRcmpBundler b = null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
        string fc = ""; b.GetFlashCode(ref fc); Console.WriteLine("FC=" + fc);

        IntPtr punk = Marshal.GetIUnknownForObject(b);
        IntPtr vtbl = Marshal.ReadIntPtr(punk, 0);

        IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, (uint)inData.Length);
        IntPtr d; SafeArrayAccessData(sa, out d); Marshal.Copy(inData, 0, d, inData.Length); SafeArrayUnaccessData(sa);
        IntPtr pv = Marshal.AllocHGlobal(16);
        Marshal.WriteInt16(pv, 0, (short)(VT_ARRAY | VT_UI1));
        Marshal.WriteIntPtr(pv, 8, sa);
        IntPtr outVar = Marshal.AllocHGlobal(16);
        for (int i = 0; i < 16; i++) Marshal.WriteByte(outVar, i, 0);

        var f = (ReadIshDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl, 14), typeof(ReadIshDel));
        int hr = f(punk, pv, outVar);
        Console.WriteLine("ReadIshList hr=0x" + hr.ToString("X8"));
        byte[] outRaw = new byte[16]; Marshal.Copy(outVar, outRaw, 0, 16);
        Console.Write("out raw="); foreach (byte x in outRaw) Console.Write(x.ToString("X2") + " "); Console.WriteLine();
        IntPtr outSa = Marshal.ReadIntPtr(outVar, 8);
        byte[] outData = outSa != IntPtr.Zero ? GetSa(outSa) : null;
        if (outData != null) {
            Console.WriteLine("out len=" + outData.Length);
            string s = ""; for (int i = 0; i < Math.Min(outData.Length, 96); i++) s += outData[i].ToString("X2") + " ";
            Console.WriteLine(s);
            File.WriteAllBytes(@"build\out\readish-one.bin", outData);
        } else Console.WriteLine("out null");

        Marshal.FreeHGlobal(outVar); Marshal.FreeHGlobal(pv); SafeArrayDestroy(sa);
        Marshal.Release(punk);
        try { v.Close(ref port); Console.WriteLine("Close OK"); } catch (Exception e) { Console.WriteLine("Close EX: " + e.Message); }
    }
}
