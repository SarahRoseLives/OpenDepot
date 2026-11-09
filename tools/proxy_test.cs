using System; using System.Runtime.InteropServices;
class P {
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryW(string p);
  [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr h,string n);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate byte Fn(IntPtr a,IntPtr b,IntPtr c);
  static byte[] Hex(string s){var b=new byte[s.Length/2];for(int i=0;i<b.Length;i++)b[i]=Convert.ToByte(s.Substring(i*2,2),16);return b;}
  static IntPtr Ptr(byte[] b,int n){var x=new byte[n];Array.Copy(b,x,Math.Min(b.Length,n));var p=Marshal.AllocHGlobal(n);Marshal.Copy(x,0,p,n);return p;}
  [STAThread] static int Main(){
    IntPtr h=LoadLibraryW(@"C:\Users\Rose\Projects\OpenDepot\build\out\proxy\authAES.dll");
    Console.WriteLine("load="+h);
    IntPtr pe=GetProcAddress(h,"_encrypt_function@12"); Console.WriteLine("enc="+pe);
    var f=(Fn)Marshal.GetDelegateForFunctionPointer(pe,typeof(Fn));
    var K=Hex("86ce74567d98ea4cdc7c6ea7e0f593da4343c32124e60e5db7d9b6b2ac8f76e2");
    var key=System.Text.Encoding.ASCII.GetBytes("Mot-SolsA8Z6J9QP");
    var op=Marshal.AllocHGlobal(64); for(int i=0;i<64;i++)Marshal.WriteByte(op,i,0);
    byte r=f(Ptr(key,64),Ptr(K,128),op);
    var o=new byte[32];Marshal.Copy(op,o,0,32);
    Console.WriteLine("ret="+r+" out="+BitConverter.ToString(o).Replace("-","").ToLower());
    return 0;
  }
}
