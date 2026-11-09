using System;
using NEXTGENLib;
class P {
 static string Hex(byte[] b){ if(b==null) return "(null)"; int n=Math.Min(b.Length,48); string s=""; for(int i=0;i<n;i++) s+=b[i].ToString("X2")+" "; return s; }
 [STAThread] static void Main(string[] a){
   string port=a.Length>0?a[0]:"COM20";
   var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
   tagCAUUID u=new tagCAUUID(); string sysrel="";
   v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
   IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
   string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode="+fc);
   ushort[][] recs = new ushort[][]{
     new ushort[]{0x0081,0x1006,0,24},
     new ushort[]{0x0081,0x1006,0,32},
     new ushort[]{0x0081,0x1004,0,68},
     new ushort[]{0x0081,0x1005,0,43},
   };
   foreach(var r in recs){
     try { object data=new byte[4096]; int err=0; b.ReadBytesFromRadio(r[0],r[1],r[2],r[3],0, ref data, out err);
       Console.Write("Read part=0x"+r[0].ToString("X4")+" tag=0x"+r[1].ToString("X4")+" len="+r[3]+" err="+err+" data=");
       if(data is byte[]){ byte[] bb=(byte[])data; Console.WriteLine("byte["+bb.Length+"] "+Hex(bb)); }
       else Console.WriteLine(data==null?"null":data.GetType().FullName);
     } catch(Exception e){ Console.WriteLine("Read EX: "+e.GetType().Name+": "+e.Message); }
   }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.Message); }
 }
}
