using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

class KV { public string N; public byte[] D; public KV(string n, byte[] d){N=n;D=d;} }

class P {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryW(string p);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, SetLastError=true)] static extern IntPtr GetProcAddress(IntPtr h, string n);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate byte Fn(IntPtr a, IntPtr b, IntPtr c);
    static byte[] Hex(string s){ var b=new byte[s.Length/2]; for(int i=0;i<b.Length;i++) b[i]=Convert.ToByte(s.Substring(i*2,2),16); return b; }
    static IntPtr Ptr(byte[] b,int size){ var buf=new byte[size]; Array.Copy(b,buf,Math.Min(b.Length,size)); var p=Marshal.AllocHGlobal(size); Marshal.Copy(buf,0,p,size); return p; }
    static byte[] Read(IntPtr p,int n){ var b=new byte[n]; Marshal.Copy(p,b,0,n); return b; }
    static byte[] Cat(byte[] a,byte[] b){ var r=new byte[a.Length+b.Length]; a.CopyTo(r,0); b.CopyTo(r,a.Length); return r; }
    static byte[] Head(byte[] a,int n){ var r=new byte[n]; Array.Copy(a,r,n); return r; }
    static byte[] Widen(byte[] a){ var r=new byte[a.Length*2]; for(int i=0;i<a.Length;i++){r[i*2]=a[i]; r[i*2+1]=0;} return r; }
    static byte[] Asc(string s){ return System.Text.Encoding.ASCII.GetBytes(s); }

    [STAThread] static int Main(string[] args){
        IntPtr h=LoadLibraryW(@"C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13\authAES.dll");
        IntPtr pe=GetProcAddress(h,"_encrypt_function@12");
        Fn enc=(Fn)Marshal.GetDelegateForFunctionPointer(pe,typeof(Fn));
        byte[] K3=Hex("86ce74567d98ea4cdc7c6ea7e0f593da4343c32124e60e5db7d9b6b2ac8f76e2");
        byte[] A3=Hex("2c8c52debfb6160448e0da559e192d4e6b95adad724206c169e9c8c014cd304c");
        byte[] K1=Hex("fa4c83ad28ffce625efa8e3576cdc07416fc164ef48d9641cb53f1481cf9a4c0");
        byte[] A1=Hex("72d8e9fdca13a064fe181ef320b152b05a740aba1e5fd637a7f1db4410bb200a");
        KV[] pairs = { new KV("K3",K3), new KV("K1",K1) };
        KV[] outs = { new KV("K3",A3), new KV("K1",A1) };
        byte[] suf=Asc("A8Z6J9QP");
        for(int pi=0; pi<2; pi++){
            byte[] K=pairs[pi].D; byte[] A=outs[pi].D;
            var keys=new List<KV>();
            keys.Add(new KV("mot+suf", Cat(Asc("Mot-Sols"),suf)));
            keys.Add(new KV("K8+suf", Cat(Head(K,8),suf)));
            keys.Add(new KV("ser8+suf", Cat(Asc("205CNX28"),suf)));
            keys.Add(new KV("mod8+suf", Cat(Asc("H46UCF9P"),suf)));
            keys.Add(new KV("K16", Head(K,16)));
            var ins=new List<KV>();
            ins.Add(new KV("widenK16", Widen(Head(K,16))));
            ins.Add(new KV("widenK16+16", Cat(Widen(Head(K,16)), new byte[16])));
            ins.Add(new KV("widenK", Widen(K)));
            ins.Add(new KV("K32", K));
            ins.Add(new KV("K48", Cat(K,new byte[16])));
            ins.Add(new KV("K16", Head(K,16)));
            foreach(var k in keys) foreach(var inp in ins){
                IntPtr kp=Ptr(k.D,64), ip=Ptr(inp.D,256), op=Marshal.AllocHGlobal(256);
                for(int i=0;i<256;i++) Marshal.WriteByte(op,i,0);
                byte r=enc(kp,ip,op); var o=Read(op,64);
                for(int off=0; off+32<=64; off+=16){
                    bool m=true; for(int i=0;i<32;i++) if(o[off+i]!=A[i]){m=false;break;}
                    if(m) Console.WriteLine("MATCH {0} key={1} in={2} off={3}", pairs[pi].N,k.N,inp.N,off);
                }
                if(k.N=="mot+suf" || k.N=="K8+suf")
                    Console.WriteLine("{0} key={1} in={2} out={3}", pairs[pi].N,k.N,inp.N, BitConverter.ToString(o,0,32).Replace("-","").ToLower());
                Marshal.FreeHGlobal(kp); Marshal.FreeHGlobal(ip); Marshal.FreeHGlobal(op);
            }
        }
        Console.WriteLine("done");
        return 0;
    }
}
