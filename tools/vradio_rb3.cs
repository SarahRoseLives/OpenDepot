using System;
using NEXTGENLib;
class P {
 static void Show(string tag, object data){
   if(data==null){ Console.WriteLine(tag+": null"); return; }
   if(data is byte[]){ byte[] b=(byte[])data; string s=""; for(int i=0;i<Math.Min(b.Length,48);i++) s+=b[i].ToString("X2")+" "; Console.WriteLine(tag+": byte["+b.Length+"] "+s); }
   else Console.WriteLine(tag+": "+data.GetType().FullName+" "+data);
 }
 [STAThread] static void Main(string[] a){
   string port=a.Length>0?a[0]:"COM20";
   var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
   tagCAUUID u=new tagCAUUID(); string sysrel="";
   v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
   IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
   int[][] specs = new int[][]{ new int[]{0x81,0x1006}, new int[]{0x81,0x1004}, new int[]{0x81,0x1005}, new int[]{0x80,0x000D}, new int[]{0x80,0x0FFD}, new int[]{0x80,0x0FFB} };
   foreach(var s in specs){
     try { object d=null; b.ReadAllTypeInstances(ref d, s[0], s[1]); Show("ReadAllTypeInstances(0x"+s[0].ToString("X")+",0x"+s[1].ToString("X")+")", d); }
     catch(Exception e){ Console.WriteLine("ReadAllTypeInstances(0x"+s[0].ToString("X")+",0x"+s[1].ToString("X")+") EX: "+e.GetType().Name+": "+e.Message); }
   }
   byte[] list = new byte[]{0x10,0x06,0x00,0x00,0x00,0x81,0x00,0x18};
   try { object ish=list; object data=null; b.ReadIshList(ref ish, out data); Show("ReadIshList(one)", data); }
   catch(Exception e){ Console.WriteLine("ReadIshList EX: "+e.GetType().Name+": "+e.Message); }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.Message); }
 }
}
