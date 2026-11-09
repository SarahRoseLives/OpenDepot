using System;
using System.IO;
using System.Runtime.InteropServices;
using NEXTGENLib;
[StructLayout(LayoutKind.Explicit,Size=16)] public struct VARIANT { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; }
class P {
 const ushort VT_ARRAY=0x2000, VT_UI1=0x11;
 [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt,int lb,uint n);
 [DllImport("oleaut32.dll")] static extern int SafeArrayAccessData(IntPtr psa,out IntPtr ppv);
 [DllImport("oleaut32.dll")] static extern int SafeArrayUnaccessData(IntPtr psa);
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetLBound(IntPtr psa,uint dim,out int lb);
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetUBound(IntPtr psa,uint dim,out int ub);
 [DllImport("oleaut32.dll")] static extern int SafeArrayGetVartype(IntPtr psa,out ushort vt);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ReadIshDel(IntPtr self,IntPtr pIn,IntPtr pOut);
 static IntPtr Slot(IntPtr v,int s){ return Marshal.ReadIntPtr(v,s*4); }
 static byte[] GetSa(IntPtr sa){ if(sa==IntPtr.Zero) return null; ushort vt; SafeArrayGetVartype(sa,out vt); int lb,ub; if(SafeArrayGetLBound(sa,1,out lb)!=0||SafeArrayGetUBound(sa,1,out ub)!=0) return null; int n=ub-lb+1; if(n<=0) return new byte[0]; IntPtr d; if(SafeArrayAccessData(sa,out d)!=0) return null; byte[] b=new byte[n]; Marshal.Copy(d,b,0,n); SafeArrayUnaccessData(sa); return b; }
 [STAThread] static void Main(string[] a){
  string port=a.Length>0?a[0]:"COM20"; byte[] list=File.ReadAllBytes(a.Length>1?a[1]:@"build\out\radio-read-zero.ish");
  Console.WriteLine("list bytes="+list.Length);
  var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1")); tagCAUUID u=new tagCAUUID(); string sq="";
  v.Open(4,ref port,null,ref u,0,0,0,ref sq); IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FC="+fc);
  IntPtr punk=Marshal.GetIUnknownForObject(b); IntPtr vtbl=Marshal.ReadIntPtr(punk,0);
  IntPtr inSa=SafeArrayCreateVector(VT_UI1,0,(uint)list.Length); IntPtr d; SafeArrayAccessData(inSa,out d); Marshal.Copy(list,0,d,list.Length); SafeArrayUnaccessData(inSa);
  IntPtr inVar=Marshal.AllocHGlobal(16); Marshal.WriteInt16(inVar,0,(short)(VT_ARRAY|VT_UI1)); Marshal.WriteIntPtr(inVar,8,inSa);
  IntPtr outVar=Marshal.AllocHGlobal(16); for(int i=0;i<16;i++) Marshal.WriteByte(outVar,i,0); // empty
  var f=(ReadIshDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl,14),typeof(ReadIshDel));
  Console.WriteLine("calling ReadIshList ..."); int hr=f(punk,inVar,outVar); Console.WriteLine("hr=0x"+hr.ToString("X8"));
  byte[] orr=new byte[16]; Marshal.Copy(outVar,orr,0,16); Console.Write("outVar="); foreach(byte x in orr) Console.Write(x.ToString("X2")+" "); Console.WriteLine();
  IntPtr osa=Marshal.ReadIntPtr(outVar,8); byte[] oa=GetSa(osa); Console.WriteLine("out data="+(oa==null?"null":oa.Length.ToString()));
  if(oa!=null&&oa.Length>0){ string s=""; for(int i=0;i<Math.Min(oa.Length,96);i++) s+=oa[i].ToString("X2")+" "; Console.WriteLine(s); File.WriteAllBytes(@"build\out\readlist-zero-out.bin",oa); }
  Marshal.Release(punk); try{ v.Close(ref port); Console.WriteLine("Close OK"); }catch(Exception e){ Console.WriteLine("Close EX: "+e.Message); }
 }
}
