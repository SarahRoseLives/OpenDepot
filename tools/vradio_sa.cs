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
 [STAThread] static void Main(string[] a){
  string port=a.Length>0?a[0]:"COM20";
  var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
  tagCAUUID u=new tagCAUUID(); string sysrel="";
  v.Open(4, ref port, null, ref u, 0,0,0, ref sysrel); Console.WriteLine("Open OK");
  IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
  IntPtr punk=Marshal.GetIUnknownForObject(b); IntPtr vtbl=Marshal.ReadIntPtr(punk,0);
  IntPtr ov=Marshal.AllocHGlobal(16);
  for(int i=0;i<16;i++) Marshal.WriteByte(ov,i,0);
  var f=(ReadAllDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl,11),typeof(ReadAllDel));
  int hr=f(punk,ov,0x0081,0x1006);
  Console.WriteLine("hr=0x"+hr.ToString("X8"));
  byte[] raw=new byte[16]; Marshal.Copy(ov,raw,0,16);
  Console.Write("raw="); foreach(byte x in raw) Console.Write(x.ToString("X2")+" "); Console.WriteLine();
  IntPtr p=Marshal.ReadIntPtr(ov,8);
  ushort svt; int r1=SafeArrayGetVartype(p,out svt);
  int lb,ub; int r2=SafeArrayGetLBound(p,1,out lb); int r3=SafeArrayGetUBound(p,1,out ub);
  Console.WriteLine("p=0x"+p.ToString("X")+" GetVartype hr=0x"+r1.ToString("X")+" vt="+svt+" lb hr=0x"+r2.ToString("X")+" ub hr=0x"+r3.ToString("X")+" lb="+lb+" ub="+ub);
  if(r1==0 && r2==0 && r3==0){
    int n=ub-lb+1; Console.WriteLine("n="+n+" vartype="+svt);
    IntPtr d; if(SafeArrayAccessData(p,out d)==0){
      if(svt==0x11 && n>0){ byte[] bb=new byte[n]; Marshal.Copy(d,bb,0,n); string s=""; for(int i=0;i<Math.Min(n,80);i++) s+=bb[i].ToString("X2")+" "; Console.WriteLine(s); File.WriteAllBytes(@"build\out\readall-flashcode.bin",bb); }
      SafeArrayUnaccessData(p);
    }
  }
  Marshal.Release(punk);
  try{ v.Close(ref port); Console.WriteLine("Close OK"); }catch(Exception e){ Console.WriteLine("Close EX: "+e.Message); }
 }
}
