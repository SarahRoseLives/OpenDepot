using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;

[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct VARIANT { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; }

class RadioHelper {
    const ushort VT_ARRAY = 0x2000, VT_UI1 = 0x11;
    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lb, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
    [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
    [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);
    [DllImport("oleaut32.dll")] static extern int SafeArrayDestroy(IntPtr psa);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetModuleHandle(string name);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ReadAllDel(IntPtr self, IntPtr pOut, int part, int tag);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int WriteIshDel(IntPtr self, IntPtr pList);
    [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate int SendCmdDel(IntPtr self, int cmd, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8);

    static IntPtr Slot(IntPtr vtbl, int slot) { return Marshal.ReadIntPtr(vtbl, slot * 4); }
    static byte[] GetSa(IntPtr sa) {
        if (sa == IntPtr.Zero) return null;
        int lb, ub;
        if (SafeArrayGetLBound(sa, 1, out lb) != 0 || SafeArrayGetUBound(sa, 1, out ub) != 0) return null;
        int n = ub - lb + 1; if (n <= 0) return new byte[0];
        IntPtr d; if (SafeArrayAccessData(sa, out d) != 0) return null;
        byte[] b = new byte[n]; Marshal.Copy(d, b, 0, n); SafeArrayUnaccessData(sa); return b;
    }

    sealed class Session : IDisposable {
        public IVirtualRadio V; public IRcmpBundler B; public IntPtr Punk, Vtbl; public string Port;
        public Session(string port) {
            Port = port;
            Guid vclsid = new Guid("CDC45D7E-0681-11D5-A74A-0010B57A9C7A");
            V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromCLSID(vclsid));
            tagCAUUID uuid = new tagCAUUID(); string sysrel = "";
            V.Open(4, ref Port, null, ref uuid, 0, 0, 0, ref sysrel);
            V.GetBundlerPointer(ref B);
            B.EnterProgramMode();
            Punk = Marshal.GetIUnknownForObject(B);
            Vtbl = Marshal.ReadIntPtr(Punk, 0);
        }
        public byte[] ReadTag(int part, int tag) {
            IntPtr ov = Marshal.AllocHGlobal(16);
            for (int i = 0; i < 16; i++) Marshal.WriteByte(ov, i, 0);
            var f = (ReadAllDel)Marshal.GetDelegateForFunctionPointer(Slot(Vtbl, 11), typeof(ReadAllDel));
            int hr = f(Punk, ov, part, tag);
            byte[] data = (hr == 0) ? GetSa(Marshal.ReadIntPtr(ov, 8)) : null;
            Marshal.FreeHGlobal(ov);
            return data;
        }
        public string Flashcode() { try { string fc = ""; B.GetFlashCode(ref fc); return fc; } catch { return ""; } }
        public int SendCommand(int cmd, int a1) {
            IntPtr h = GetModuleHandle("NextGen10.dll");
            if (h == IntPtr.Zero) throw new Exception("NextGen10 not loaded");
            IntPtr fn = new IntPtr(h.ToInt64() + 0xB5B0);
            var f = (SendCmdDel)Marshal.GetDelegateForFunctionPointer(fn, typeof(SendCmdDel));
            return f(Punk, cmd, a1, 0, 0, 0, 0, 0, 0, 0);
        }
        public void Commit() {
            int hr = SendCommand(0x106, 1);
            Console.WriteLine("SendCommand(0x106,1) hr=0x" + hr.ToString("X8"));
        }
        public void Dispose() {
            try { if (B != null) Marshal.ReleaseComObject(B); } catch { }
            B = null;
            try { if (V != null) { V.Close(ref Port); Marshal.ReleaseComObject(V); } } catch { }
            V = null;
            try { if (Punk != IntPtr.Zero) Marshal.Release(Punk); } catch { }
            Punk = IntPtr.Zero;
        }
    }

    static void Append(FileStream fs, HashSet<uint> got, byte[] data) {
        int o = 0;
        while (o + 6 <= data.Length) {
            uint id = (uint)((data[o] << 24) | (data[o + 1] << 16) | (data[o + 2] << 8) | data[o + 3]);
            int len = (data[o + 4] << 8) | data[o + 5];
            if (o + 6 + len > data.Length) break;
            if (got.Add(id)) fs.Write(data, o, 6 + len);
            o += 6 + len;
        }
    }

    static ushort Crc16(byte[] d, int len) {
        ushort crc = 0;
        for (int i = 0; i < len; i++) {
            crc ^= (ushort)(d[i] << 8);
            for (int b = 0; b < 8; b++) crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
        }
        return crc;
    }
    static string Hex(byte[] b) { var sb = new System.Text.StringBuilder(); foreach (var x in b) sb.Append(x.ToString("x2")); return sb.ToString(); }
    static string Fmt(object o) {
        if (o == null) return "null";
        if (o is byte[]) { var b = (byte[])o; return "bytes[" + b.Length + "] " + Hex(b); }
        if (o is string) return "\"" + o + "\"";
        if (o is Array) {
            var ar = (Array)o; var sb = new System.Text.StringBuilder("arr[" + ar.Length + "] ");
            foreach (var x in ar) { try { sb.Append(Convert.ToByte(x).ToString("x2")); } catch { sb.Append(x.ToString()); } }
            return sb.ToString();
        }
        return o.GetType().Name + " " + o.ToString();
    }
    [STAThread] static int Main(string[] a) {
        if (a.Length < 2) { Console.Error.WriteLine("usage: radio_helper --read <port> <taglist> <out.rec> | --write <port> <in.rec>"); return 1; }
        string mode = a[0], port = a[1], inPath = a.Length > 2 ? a[2] : null, outPath = a.Length > 3 ? a[3] : null;

        if (mode == "--reset") {
            try {
                using (var sp = new System.IO.Ports.SerialPort(port, 9600, System.IO.Ports.Parity.None, 8, System.IO.Ports.StopBits.One)) {
                    sp.Handshake = System.IO.Ports.Handshake.RequestToSend;
                    sp.DtrEnable = true; sp.RtsEnable = true;
                    sp.ReadTimeout = 500; sp.WriteTimeout = 1000;
                    sp.Open();
                    sp.DiscardInBuffer(); sp.DiscardOutBuffer();
                    sp.Write("ATZ\r"); System.Threading.Thread.Sleep(800);
                    try { sp.ReadExisting(); } catch { }
                    sp.Write("AT+IPR=9600\r"); System.Threading.Thread.Sleep(400);
                    try { sp.ReadExisting(); } catch { }
                }
                Console.WriteLine("reset ok");
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("reset error: " + e.Message); return 3; }
        }
        if (mode == "--erase") {
            try {
                using (var s = new Session(port)) {
                    s.B.DelCodeplug(0);
                    Console.WriteLine("DelCodeplug ok");
                }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("erase error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--deltype") {
            int part = Convert.ToInt32(a[2], 16); int tag = Convert.ToInt32(a[3], 16);
            try {
                using (var s = new Session(port)) {
                    s.B.DelIshTypes(tag, tag, part);
                    Console.WriteLine("DelIshTypes ok part=0x" + part.ToString("X4") + " tag=0x" + tag.ToString("X4"));
                }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("deltype error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--delitem") {
            int part = Convert.ToInt32(a[2], 16); int tag = Convert.ToInt32(a[3], 16);
            int start = Convert.ToInt32(a[4], 16); int end = Convert.ToInt32(a[5], 16);
            try {
                using (var s = new Session(port)) {
                    s.B.DelIshItem(start, end, part, tag);
                    Console.WriteLine("DelIshItem ok part=0x" + part.ToString("X4") + " tag=0x" + tag.ToString("X4") + " " + start + ".." + end);
                }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("delitem error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--replace") {
            string oldPath = a[2], newPath = a[3];
            byte[] ish = File.ReadAllBytes(newPath);
            byte[] old = File.ReadAllBytes(oldPath);
            var oldLen = new Dictionary<uint, int>();
            var oldByTag = new Dictionary<long, HashSet<int>>();
            int po = 0;
            while (po + 8 <= old.Length) {
                uint id = (uint)((old[po] << 24) | (old[po + 1] << 16) | (old[po + 2] << 8) | old[po + 3]);
                int len = (old[po + 4] << 8) | old[po + 5];
                if (po + 6 + len > old.Length) break;
                int tag = (int)(id >> 16), inst = (int)(id & 0xFFFF);
                int part = (tag == 0x1004 || tag == 0x1005 || tag == 0x1006) ? 0x0081 : 0x0080;
                oldLen[id] = len;
                long key = ((long)part << 16) | (uint)tag;
                HashSet<int> hs; if (!oldByTag.TryGetValue(key, out hs)) { hs = new HashSet<int>(); oldByTag[key] = hs; }
                hs.Add(inst);
                po += 6 + len;
            }
            var newByTag = new Dictionary<long, HashSet<int>>();
            var ish2 = new List<byte>();
            int no = 0;
            while (no + 8 <= ish.Length) {
                int tag = (ish[no] << 8) | ish[no + 1];
                int inst = (ish[no + 2] << 8) | ish[no + 3];
                int part = (ish[no + 4] << 8) | ish[no + 5];
                int len = (ish[no + 6] << 8) | ish[no + 7];
                if (tag == 0 && inst == 0 && part == 0 && len == 0) break;
                if (no + 8 + len > ish.Length) break;
                uint id = ((uint)tag << 16) | (uint)inst;
                long key = ((long)part << 16) | (uint)tag;
                HashSet<int> hs; if (!newByTag.TryGetValue(key, out hs)) { hs = new HashSet<int>(); newByTag[key] = hs; }
                hs.Add(inst);
                int outLen = len;
                byte[] payload = new byte[len];
                Array.Copy(ish, no + 8, payload, 0, len);
                int oldL;
                if (oldLen.TryGetValue(id, out oldL) && oldL > len && oldL >= 3) {
                    int plen = oldL - 2;
                    byte[] padded = new byte[plen];
                    int copy = Math.Min(len - 2, plen);
                    if (copy > 0) Array.Copy(payload, 0, padded, 0, copy);
                    ushort crc = Crc16(padded, plen);
                    byte[] np = new byte[oldL];
                    Array.Copy(padded, 0, np, 0, plen);
                    np[plen] = (byte)(crc >> 8);
                    np[plen + 1] = (byte)crc;
                    payload = np;
                    outLen = oldL;
                }
                ish2.Add((byte)(tag >> 8)); ish2.Add((byte)tag);
                ish2.Add((byte)(inst >> 8)); ish2.Add((byte)inst);
                ish2.Add((byte)(part >> 8)); ish2.Add((byte)part);
                ish2.Add((byte)(outLen >> 8)); ish2.Add((byte)outLen);
                ish2.AddRange(payload);
                no += 8 + len;
            }
            ish2.Add(0); ish2.Add(0);
            byte[] wdata = ish2.ToArray();
            try {
                using (var s = new Session(port)) {
                    IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, (uint)wdata.Length);
                    IntPtr d; SafeArrayAccessData(sa, out d); Marshal.Copy(wdata, 0, d, wdata.Length); SafeArrayUnaccessData(sa);
                    IntPtr pv = Marshal.AllocHGlobal(16);
                    for (int i = 0; i < 16; i++) Marshal.WriteByte(pv, i, 0);
                    Marshal.WriteInt16(pv, 0, (short)(VT_ARRAY | VT_UI1));
                    Marshal.WriteIntPtr(pv, 8, sa);
                    var f = (WriteIshDel)Marshal.GetDelegateForFunctionPointer(Slot(s.Vtbl, 12), typeof(WriteIshDel));
                    int hr = f(s.Punk, pv);
                    Marshal.FreeHGlobal(pv); SafeArrayDestroy(sa);
                    Console.WriteLine("WriteIshList hr=0x" + hr.ToString("X8") + " bytes=" + wdata.Length);
                    int delTags = 0, delRanges = 0;
                    foreach (var kv in oldByTag) {
                        int part = (int)(kv.Key >> 16), tag = (int)(kv.Key & 0xFFFF);
                        HashSet<int> ni;
                        newByTag.TryGetValue(kv.Key, out ni);
                        if (ni == null) {
                            try { s.B.DelIshTypes(tag, tag, part); delTags++; }
                            catch (Exception e) { Console.WriteLine("DelIshTypes 0x" + tag.ToString("X4") + " EX " + e.Message); }
                        } else {
                            var extra = new List<int>();
                            foreach (int x in kv.Value) if (!ni.Contains(x)) extra.Add(x);
                            extra.Sort();
                            int idx = 0;
                            while (idx < extra.Count) {
                                int start = extra[idx], end = start;
                                while (idx + 1 < extra.Count && extra[idx + 1] == end + 1) { idx++; end = extra[idx]; }
                                try { s.B.DelIshItem(start, end, part, tag); delRanges++; }
                                catch (Exception e) { Console.WriteLine("DelIshItem 0x" + tag.ToString("X4") + " " + start + ".." + end + " EX " + e.Message); }
                                idx++;
                            }
                        }
                    }
                    Console.WriteLine("replace done: deleted tags=" + delTags + " item-ranges=" + delRanges);
                    try { s.B.ResetRadio(); Console.WriteLine("ResetRadio ok"); } catch (Exception e) { Console.WriteLine("ResetRadio EX " + e.Message); }
                }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("replace error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--resetradio") {
            try {
                using (var s = new Session(port)) { s.B.ResetRadio(); Console.WriteLine("ResetRadio ok"); }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("resetradio error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--authprobe") {
            try {
                using (var s = new Session(port)) {
                    object key = null;
                    int hr = s.B.ReadRadioKey(ref key);
                    Console.WriteLine("ReadRadioKey hr=0x" + hr.ToString("X8") + " key=" + Fmt(key));
                    object dt = null;
                    hr = s.B.ReadWriteRadioDateCode(ref dt, 1);
                    Console.WriteLine("ReadDateCode hr=0x" + hr.ToString("X8") + " date=" + Fmt(dt));
                    object a0 = null;
                    hr = s.B.ReadWriteCalcAuthCode(ref a0, 0);
                    Console.WriteLine("CalcAuth(0) hr=0x" + hr.ToString("X8") + " auth=" + Fmt(a0));
                    object a1 = null;
                    hr = s.B.ReadWriteCalcAuthCode(ref a1, 1);
                    Console.WriteLine("CalcAuth(1) hr=0x" + hr.ToString("X8") + " auth=" + Fmt(a1));
                    string pw = ""; ushort en = 0;
                    try { s.B.GetRadioPassword(ref pw, ref en); Console.WriteLine("GetRadioPassword en=" + en + " pw=" + pw); }
                    catch (Exception e) { Console.WriteLine("GetRadioPassword EX " + e.Message); }
                    try { object lk = null; hr = s.B.UnLockRadio(ref lk); Console.WriteLine("UnLockRadio hr=0x" + hr.ToString("X8") + " r=" + Fmt(lk)); }
                    catch (Exception e) { Console.WriteLine("UnLockRadio EX " + e.Message); }
                }
                return 0;
            } catch (Exception e) { Console.Error.WriteLine("authprobe error: " + e.GetType().Name + ": " + e.Message); return 3; }
        }
        if (mode == "--vwrite") {
            byte[] data = File.ReadAllBytes(inPath);
            uint ver = 12;
            if (a.Length > 3) uint.TryParse(a[3], out ver);
            try {
                Guid vclsid = new Guid("CDC45D7E-0681-11D5-A74A-0010B57A9C7A");
                var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromCLSID(vclsid));
                tagCAUUID uuid = new tagCAUUID(); string sysrel = "R17.00.02"; string p = port;
                V.Open(4, ref p, null, ref uuid, ver, 0, ver, ref sysrel);
                NEXTGENLib.IRcmpBundler B = null;
                try { V.GetBundlerPointer(ref B); Console.WriteLine("GetBundlerPointer ok"); } catch (Exception e) { Console.WriteLine("GetBundlerPointer EX " + e.Message); }
                object obj = data;
                V.WriteCodeplug(ref obj);
                Console.WriteLine("WriteCodeplug ok ver=" + ver + " bytes=" + data.Length);
                try { V.Close(ref p); } catch { }
                return 0;
            } catch (Exception e) {
                var ce = e as COMException;
                Console.Error.WriteLine("vwrite ERROR ver=" + ver + ": " + e.GetType().Name + ": " + e.Message + (ce != null ? (" hr=0x" + ce.ErrorCode.ToString("X8")) : ""));
                return 3;
            }
        }
        if (mode == "--protoall") {
            foreach (int pv in new[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 })
            {
                try
                {
                    Guid clsid = new Guid("CDC45D76-0681-11D5-A74A-0010B57A9C7A");
                    var proto = (IRcmpProtocol)Activator.CreateInstance(Type.GetTypeFromCLSID(clsid));
                    string pp = port;
                    try { proto.LoadServers(pv); } catch (Exception e) { Console.WriteLine(pv + ": LoadServers EX " + e.Message); }
                    try
                    {
                        proto.OpenPort(pv, ref pp, null);
                        object req = new byte[] { 0, 0x0f, 0 };
                        proto.SendRequest(ref req, 3000);
                        object rep = null; proto.GetReply(ref rep);
                        Console.WriteLine(pv + ": OK reply=" + Fmt(rep));
                        try { string p2 = port; proto.ClosePort(ref p2); } catch { }
                        try { proto.CloseServers(pv); } catch { }
                        return 0;
                    }
                    catch (Exception e) { Console.WriteLine(pv + ": OpenPort EX " + e.GetType().Name + ": " + e.Message); }
                    try { proto.CloseServers(pv); } catch { }
                }
                catch (Exception e) { Console.WriteLine(pv + ": create EX " + e.Message); }
            }
            return 0;
        }
        if (mode == "--readcp") {
            try {
                uint ver = 0;
                if (a.Length > 2) uint.TryParse(a[2], out ver);
                Guid vclsid = new Guid("CDC45D7E-0681-11D5-A74A-0010B57A9C7A");
                var V = (IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromCLSID(vclsid));
                tagCAUUID uuid = new tagCAUUID(); string sysrel = ""; string p = port;
                V.Open(4, ref p, null, ref uuid, ver, 0, ver, ref sysrel);
                Console.WriteLine("Open ok ver=" + ver + " sysrel=" + sysrel);
                NEXTGENLib.IRcmpBundler B = null;
                try { V.GetBundlerPointer(ref B); Console.WriteLine("GetBundlerPointer ok"); } catch (Exception e) { Console.WriteLine("GetBundlerPointer EX " + e.Message); }
                object shop = null, data = null;
                V.ReadCodeplug(ref shop, ref data);
                byte[] db = data as byte[];
                Console.WriteLine("ReadCodeplug ok shopType=" + (shop == null ? "null" : shop.GetType().Name) +
                                  " dataType=" + (data == null ? "null" : data.GetType().Name) +
                                  " dataLen=" + (db != null ? db.Length : -1));
                if (db != null) { int n = Math.Min(64, db.Length); var sb = new System.Text.StringBuilder(); for (int i = 0; i < n; i++) sb.Append(db[i].ToString("x2")); Console.WriteLine("  head=" + sb); }
                try { V.Close(ref p); } catch { }
                return 0;
            } catch (Exception e) {
                var ce = e as COMException;
                Console.Error.WriteLine("readcp ERROR: " + e.GetType().Name + ": " + e.Message + (ce != null ? (" hr=0x" + ce.ErrorCode.ToString("X8")) : ""));
                return 3;
            }
        }
        if (mode == "--roundtrip") {
            try {
                using (var s = new Session(port)) {
                    object shop = null, data = null;
                    s.V.ReadCodeplug(ref shop, ref data);
                    byte[] db = data as byte[];
                    Console.WriteLine("ReadCodeplug shopType=" + (shop == null ? "null" : shop.GetType().Name) +
                                      " dataType=" + (data == null ? "null" : data.GetType().Name) +
                                      " dataLen=" + (db != null ? db.Length : -1));
                    if (db != null) {
                        int n = Math.Min(64, db.Length);
                        var sb = new System.Text.StringBuilder();
                        for (int i = 0; i < n; i++) sb.Append(db[i].ToString("x2"));
                        Console.WriteLine("  data head=" + sb.ToString());
                    }
                    s.V.WriteCodeplug(ref data);
                    Console.WriteLine("WriteCodeplug ok");
                    try { Console.WriteLine("ResetRadio rc=" + s.B.ResetRadio()); } catch (Exception e) { Console.WriteLine("ResetRadio EX " + e.Message); }
                }
                return 0;
            } catch (Exception e) {
                var ce = e as COMException;
                Console.Error.WriteLine("roundtrip ERROR: " + e.GetType().Name + ": " + e.Message + (ce != null ? (" hr=0x" + ce.ErrorCode.ToString("X8")) : ""));
                return 3;
            }
        }
        if (mode == "--prototest") {
            try {
                Guid clsid = new Guid("CDC45D76-0681-11D5-A74A-0010B57A9C7A");
                var proto = (IRcmpProtocol)Activator.CreateInstance(Type.GetTypeFromCLSID(clsid));
                Console.WriteLine("RcmpProtocol created");
                string p = port;
                proto.LoadServers(4);
                Console.WriteLine("LoadServers ok");
                proto.OpenPort(4, ref p, null);
                Console.WriteLine("OpenPort ok");
                object req = new byte[] { 0x00, 0x0f, 0x00 };
                proto.SendRequest(ref req, 3000);
                Console.WriteLine("SendRequest ok");
                object rep = null;
                proto.GetReply(ref rep);
                Console.WriteLine("reply=" + Fmt(rep));
                try { proto.ClosePort(ref p); } catch { }
                try { proto.CloseServers(4); } catch { }
                return 0;
            } catch (Exception e) {
                var ce = e as COMException;
                Console.Error.WriteLine("prototest ERROR: " + e.GetType().Name + ": " + e.Message + (ce != null ? (" hr=0x" + ce.ErrorCode.ToString("X8")) : ""));
                return 3;
            }
        }
        if (mode == "--write") {
            // SAFETY: IVirtualRadio.WriteToRadio() expects the ADK codeplug block,
            // NOT the tag/inst ISH list; using it here wipes the identity partition
            // (FAIL 01/92). Keep the basic vendor WriteIshList (non-destructive).
            byte[] wdata = File.ReadAllBytes(inPath);
            try {
                using (var s = new Session(port)) {
                    IntPtr sa = SafeArrayCreateVector(VT_UI1, 0, (uint)wdata.Length);
                    IntPtr d; SafeArrayAccessData(sa, out d); Marshal.Copy(wdata, 0, d, wdata.Length); SafeArrayUnaccessData(sa);
                    IntPtr pv = Marshal.AllocHGlobal(16);
                    Marshal.WriteInt16(pv, 0, (short)(VT_ARRAY | VT_UI1)); Marshal.WriteIntPtr(pv, 8, sa);
                    var f = (WriteIshDel)Marshal.GetDelegateForFunctionPointer(Slot(s.Vtbl, 12), typeof(WriteIshDel));
                    int hr = f(s.Punk, pv);
                    Marshal.FreeHGlobal(pv); SafeArrayDestroy(sa);
                    Console.WriteLine("WriteIshList hr=0x" + hr.ToString("X8") + " bytes=" + wdata.Length);
                    try { int er = 0; s.B.ResetRequest(ref er); Console.WriteLine("ResetRequest err=" + er); }
                    catch (Exception e) { Console.WriteLine("ResetRequest EX " + e.Message); }
                    try { s.B.ResetRadio(); Console.WriteLine("ResetRadio ok"); }
                    catch (Exception e) { Console.WriteLine("ResetRadio EX " + e.Message); }
                    return hr == 0 ? 0 : 4;
                }
            } catch (Exception e) { Console.Error.WriteLine("ERROR: " + e.GetType().Name + ": " + e.Message); return 3; }
        }

        var tags = new List<Tuple<int, int>>();
        byte[] raw = File.ReadAllBytes(inPath);
        for (int o = 0; o + 4 <= raw.Length; o += 4)
            tags.Add(Tuple.Create((raw[o] << 8) | raw[o + 1], (raw[o + 2] << 8) | raw[o + 3]));
        tags.Sort(delegate (Tuple<int, int> x, Tuple<int, int> y) {
            int px = (x.Item1 == 0x80 && x.Item2 == 0x0FFF) ? 0 : 1;
            int py = (y.Item1 == 0x80 && y.Item2 == 0x0FFF) ? 0 : 1;
            int c = px.CompareTo(py);
            if (c != 0) return c;
            c = x.Item1.CompareTo(y.Item1);
            return c != 0 ? c : x.Item2.CompareTo(y.Item2);
        });

        var got = new HashSet<uint>();
        using (var fs = new FileStream(outPath, FileMode.Create, FileAccess.Write)) {
            using (var s = new Session(port)) {
                string fc = s.Flashcode(); if (fc.Length > 0) Console.WriteLine("flashcode " + fc);
                int empty = 0;
                foreach (var t in tags) {
                    byte[] data = null;
                    try { data = s.ReadTag(t.Item1, t.Item2); } catch (Exception e) { Console.WriteLine("0x" + t.Item1.ToString("X4") + "/0x" + t.Item2.ToString("X4") + " EX " + e.Message); }
                    if (data != null && data.Length > 0) {
                        int before = got.Count; Append(fs, got, data);
                        if (got.Count != before) Console.WriteLine("0x" + t.Item1.ToString("X4") + "/0x" + t.Item2.ToString("X4") + " +" + (got.Count - before));
                    } else { empty++; Console.WriteLine("0x" + t.Item1.ToString("X4") + "/0x" + t.Item2.ToString("X4") + " empty"); }
                    System.Threading.Thread.Sleep(60);
                }
                Console.WriteLine("empty tags: " + empty);
            }
        }
        Console.WriteLine("wrote " + outPath + " records=" + got.Count);
        return got.Count > 0 ? 0 : 3;
    }
}

