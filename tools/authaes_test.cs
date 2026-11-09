using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

class KV {
    public string Name; public byte[] Data;
    public KV(string n, byte[] d) { Name = n; Data = d; }
}

class P {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryW(string p);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr h, string n);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate byte Fn(IntPtr a, IntPtr b, IntPtr c);

    static byte[] Hex(string s) {
        var b = new byte[s.Length / 2];
        for (int i = 0; i < b.Length; i++) b[i] = Convert.ToByte(s.Substring(i * 2, 2), 16);
        return b;
    }
    static IntPtr Ptr(byte[] b, int size) {
        var buf = new byte[size];
        Array.Copy(b, buf, Math.Min(b.Length, size));
        var p = Marshal.AllocHGlobal(size);
        Marshal.Copy(buf, 0, p, size);
        return p;
    }
    static byte[] Read(IntPtr p, int n) { var b = new byte[n]; Marshal.Copy(p, b, 0, n); return b; }
    static byte[] Cat(byte[] a, byte[] b) { var r = new byte[a.Length + b.Length]; a.CopyTo(r, 0); b.CopyTo(r, a.Length); return r; }
    static byte[] Head(byte[] a, int n) { var r = new byte[n]; Array.Copy(a, r, n); return r; }

    [STAThread] static int Main(string[] args) {
        string dll = @"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\authAES.dll";
        IntPtr h = LoadLibraryW(dll);
        IntPtr pe = GetProcAddress(h, "_encrypt_function@12");
        var enc = (Fn)Marshal.GetDelegateForFunctionPointer(pe, typeof(Fn));

        byte[] K3 = Hex("86ce74567d98ea4cdc7c6ea7e0f593da4343c32124e60e5db7d9b6b2ac8f76e2");
        byte[] A3 = Hex("2c8c52debfb6160448e0da559e192d4e6b95adad724206c169e9c8c014cd304c");
        byte[] K1 = Hex("fa4c83ad28ffce625efa8e3576cdc07416fc164ef48d9641cb53f1481cf9a4c0");
        byte[] A1 = Hex("72d8e9fdca13a064fe181ef320b152b05a740aba1e5fd637a7f1db4410bb200a");

        byte[] suffix = System.Text.Encoding.ASCII.GetBytes("A8Z6J9QP");
        byte[] suffix2 = System.Text.Encoding.ASCII.GetBytes("A8Z6J9QP5");
        byte[] mot = System.Text.Encoding.ASCII.GetBytes("Mot-Sols");

        KV[] pairsK = { new KV("K3", K3), new KV("K1", K1) };
        KV[] pairsA = { new KV("K3", A3), new KV("K1", A1) };

        for (int pi = 0; pi < 2; pi++) {
            byte[] K = pairsK[pi].Data; byte[] A = pairsA[pi].Data;
            var keys = new List<KV>();
            keys.Add(new KV("mot+suffix", Cat(mot, suffix))); keys.Add(new KV("serial8", Cat(System.Text.Encoding.ASCII.GetBytes("205CNX28"), suffix))); keys.Add(new KV("model8", Cat(System.Text.Encoding.ASCII.GetBytes("H46UCF9P"), suffix))); keys.Add(new KV("serial", Cat(System.Text.Encoding.ASCII.GetBytes("205CNX2873"), suffix))); keys.Add(new KV("MOTOROLA", Cat(System.Text.Encoding.ASCII.GetBytes("MOTOROLA"), suffix))); keys.Add(new KV("zeros", Cat(new byte[8], suffix)));
            keys.Add(new KV("K8+suffix", Cat(Head(K, 8), suffix)));
            keys.Add(new KV("K8+suffix2", Cat(Head(K, 8), suffix2)));
            keys.Add(new KV("K8+mot", Cat(Head(K, 8), mot)));
            keys.Add(new KV("mot+K8", Cat(mot, Head(K, 8))));
            keys.Add(new KV("K16", Head(K, 16)));
            keys.Add(new KV("Kfull", K));
            var inputs = new List<KV>();
            inputs.Add(new KV("K32", K));
            inputs.Add(new KV("K48", Cat(K, new byte[16])));
            inputs.Add(new KV("Khex", System.Text.Encoding.ASCII.GetBytes(BitConverter.ToString(K).Replace("-", "").ToLower())));
            inputs.Add(new KV("KhexU", System.Text.Encoding.ASCII.GetBytes(BitConverter.ToString(K).Replace("-", "")))); var w=new byte[K.Length*2]; for(int wi=0;wi<K.Length;wi++){w[wi*2]=K[wi];w[wi*2+1]=0;} inputs.Add(new KV("Kutf16", w));

            foreach (var k in keys) {
                foreach (var inp in inputs) {
                    IntPtr kp = Ptr(k.Data, 64), ip = Ptr(inp.Data, 256), op = Marshal.AllocHGlobal(256);
                    for (int i = 0; i < 256; i++) Marshal.WriteByte(op, i, 0);
                    byte r = enc(kp, ip, op);
                    var outb = Read(op, 64);
                    for (int off = 0; off + 32 <= 64; off += 16) {
                        bool match = true;
                        for (int i = 0; i < 32; i++) if (outb[off + i] != A[i]) { match = false; break; }
                        if (match) Console.WriteLine("MATCH {0} key={1} input={2} r={3} off={4}", pairsK[pi].Name, k.Name, inp.Name, r, off);
                    }
                    if (k.Name.StartsWith("K8+") || k.Name == "mot+suffix")
                        Console.WriteLine("{0} key={1} input={2} out={3}", pairsK[pi].Name, k.Name, inp.Name,
                            BitConverter.ToString(outb, 0, 32).Replace("-", "").ToLower());
                    Marshal.FreeHGlobal(kp); Marshal.FreeHGlobal(ip); Marshal.FreeHGlobal(op);
                }
            }
        }
        Console.WriteLine("done");
        return 0;
    }
}

