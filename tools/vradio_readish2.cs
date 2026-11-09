using System;
using System.Collections.Generic;
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
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ReadIshDel(IntPtr self,IntPtr pIn,IntPtr pOut);
 static IntPtr Slot(IntPtr v,int s){ return Marshal.ReadIntPtr(v,s*4); }
 static byte[] GetSa(IntPtr sa){ int lb,ub; if(sa==IntPtr.Zero) return null; if(SafeArrayGetLBound(sa,1,out lb)!=0||SafeArrayGetUBound(sa,1,out ub)!=0) return null; int n=ub-lb+1; if(n<=0) return new byte[0]; IntPtr d; if(SafeArrayAccessData(sa,out d)!=0) return null; byte[] b=new byte[n]; Marshal.Copy(d,b,0,n); SafeArrayUnaccessData(sa); return b; }
 static byte[] Rec(byte[] all,uint id){ int o=0; while(o+6<=all.Length){ uint rid=(uint)((all[o]<<24)|(all[o+1]<<16)|(all[o+2]<<8)|all[o+3]); int len=(all[o+4]<<8)|all[o+5]; if(o+6+len>all.Length) break; if(rid==id){ byte[] r=new byte[6+len]; Array.Copy(all,o,r,0,r.Length); return r; } o+=6+len; } return null; }
 [STAThread] static void Main(string[] a){
  string port=a.Length>0?a[0]:"COM20"; byte[] backup=File.ReadAllBytes(@"build\out\backup.rec"); byte[] rec=Rec(backup,0x10060000);
  int len=(rec[4]<<8)|rec[5]; var list=new List<byte>(); list.Add(0x10);list.Add(0x06);list.Add(0);list.Add(0);list.Add(0);list.Add(0x81);list.Add((byte)(len>>8));list.Add((byte)len); for(int i=0;i<len;i++) list.Add(rec[6+i]); list.Add(0);list.Add(0);
  byte[] inData=list.ToArray(); Console.WriteLine("list bytes="+inData.Length+" len="+len);
  var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1")); tagCAUUID u=new tagCAUUID(); string sq=""; v.Open(4,ref port,null,ref u,0,0,0,ref sq);
  IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FC="+fc);
  IntPtr punk=Marshal.GetIUnknownForObject(b); IntPtr vtbl=Marshal.ReadIntPtr(punk,0);
  IntPtr inSa=SafeArrayCreateVector(VT_UI1,0,(uint)inData.Length); IntPtr d; SafeArrayAccessData(inSa,out d); Marshal.Copy(inData,0,d,inData.Length); SafeArrayUnaccessData(inSa);
  IntPtr inVar=Marshal.AllocHGlobal(16); Marshal.WriteInt16(inVar,0,(short)(VT_ARRAY|VT_UI1)); Marshal.WriteIntPtr(inVar,8,inSa);
  IntPtr outSa0=SafeArrayCreateVector(VT_UI1,0,0x100000);
  IntPtr outVar=Marshal.AllocHGlobal(16); Marshal.WriteInt16(outVar,0,(short)(VT_ARRAY|VT_UI1)); Marshal.WriteIntPtr(outVar,8,outSa0);
  var f=(ReadIshDel)Marshal.GetDelegateForFunctionPointer(Slot(vtbl,14),typeof(ReadIshDel)); int hr=f(punk,inVar,outVar);
  Console.WriteLine("hr=0x"+hr.ToString("X8"));
  byte[] ir=new byte[16]; Marshal.Copy(inVar,ir,0,16); byte[] orr=new byte[16]; Marshal.Copy(outVar,orr,0,16);
  Console.Write("inVar="); foreach(byte x in ir) Console.Write(x.ToString("X2")+" "); Console.WriteLine();
  Console.Write("outVar="); foreach(byte x in orr) Console.Write(x.ToString("X2")+" "); Console.WriteLine();
  byte[] ia=GetSa(Marshal.ReadIntPtr(inVar,8)); byte[] oa=GetSa(Marshal.ReadIntPtr(outVar,8));
  Console.WriteLine("inSa data="+ (ia==null?"null":ia.Length.ToString())); if(ia!=null){ string s=""; for(int i=0;i<Math.Min(ia.Length,64);i++) s+=ia[i].ToString("X2")+" "; Console.WriteLine(s); }
  Console.WriteLine("outSa data="+ (oa==null?"null":oa.Length.ToString())); if(oa!=null){ string s=""; for(int i=0;i<Math.Min(oa.Length,64);i++) s+=oa[i].ToString("X2")+" "; Console.WriteLine(s); }
  Marshal.Release(punk); try{ v.Close(ref port); }catch{}
 }
}
