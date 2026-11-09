using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;
[StructLayout(LayoutKind.Explicit, Size=16)] public struct VARIANT { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; }
class P {
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetVartype(IntPtr psa, out ushort vt);
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa, uint dim, out int lb);
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa, uint dim, out int ub);
 [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa, out IntPtr ppv);
 [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ReadAllDel(IntPtr self, IntPtr pOut, int part, int typ);
 static IntPtr Slot(IntPtr v,int s){ return Marshal.ReadIntPtr(v,s*4); }
 static byte[] Get(IntPtr p){
  ushort svt; int lb,ub; if(SafeArrayGetVartype(p,out svt)!=0) return null;
  if(SafeArrayGetLBound(p,1,out lb)!=0||SafeArrayGetUBound(p,1,out ub)!=0) return null;
  int n=ub-lb+1; if(n<=0) return new byte[0];
  IntPtr d; if(SafeArrayAccessData(p,out d)!=0) return null;
  byte[] b=new byte[n]; Marshal.Copy(d,b,0,n); SafeArrayUnaccessData(p); return b;
 }
 [STAThread] static void Main(string[] a){
  string port=a.Length>0?a[0]:"COM20";
  var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
  tagCAUUID u=new tagCAUUID(); string sysrel="";
  v.Open(4, ref port, null, ref u,0,0,0,ref sysrel); IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode();
  Console.WriteLine("open+program OK"); string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FC="+fc);
  IntPtr punk=Marshal.GetIUnknownForObject(b); IntPtr vtbl=Marshal.ReadIntPtr(punk,0);
  int[][] specs=new int[][]{ new int[]{0x80,0x0FFD}, new int[]{0x80,0x0FFB}, new int[]{0x80,0x000D}, new int[]{0x80,0x0FFE}, new int[]{0x80,0x0FFC} };
  foreach(var s in specs){
    IntPtr ov=Marshal.AllocHGlobal(16); for(int i=0;i<16;i++) Marshal.WriteByte(ov,i,0);
    var f=(ReadAllDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl,11),typeof(ReadAllDel));
    int hr=f(punk,ov,s[0],s[1]); IntPtr p=Marshal.ReadIntPtr(ov,8); byte[] bts=Get(p);
    Console.WriteLine("part=0x"+s[0].ToString("X")+" tag=0x"+s[1].ToString("X")+" hr=0x"+hr.ToString("X")+" ptr=0x"+p.ToString("X")+" len="+(bts==null?-1:bts.Length));
    if(bts!=null){ string h=""; for(int i=0;i<Math.Min(bts.Length,96);i++) h+=bts[i].ToString("X2")+" "; Console.WriteLine("  "+h); File.WriteAllBytes(@"build\out\radio-0x"+s[1].ToString("X")+".bin",bts); }
  }
  Marshal.Release(punk); try{ v.Close(ref port); Console.WriteLine("Close OK"); }catch(Exception e){ Console.WriteLine("Close EX: "+e.Message); }
 }
}
