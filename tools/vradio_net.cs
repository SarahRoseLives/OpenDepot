using System;
using System.IO;
using NEXTGENLib;
class P {
 static void Save(string path, object o){
   if(o==null){ Console.WriteLine("  (null)"); return; }
   Console.WriteLine("  type="+o.GetType().FullName);
   if(o is Array){ Array a=(Array)o; Console.WriteLine("  len="+a.Length+" elem="+a.GetType().GetElementType());
     if(o is byte[]){ byte[] b=(byte[])o; File.WriteAllBytes(path,b); Console.WriteLine("  saved "+b.Length); } }
 }
 [STAThread] static void Main(string[] a){
   string port=a.Length>0?a[0]:"COM20";
   var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
   tagCAUUID u=new tagCAUUID(); string sysrel="";
   v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK");
   IRcmpBundler b=null; v.GetBundlerPointer(ref b); b.EnterProgramMode(); Console.WriteLine("ProgramMode OK");
   string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode="+fc);
   var nb = (INetRcmpBundler)b;
   try { Array ish = new byte[0]; Array data = null; nb.NetReadIshList(ish, out data); Console.WriteLine("NetReadIshList OK"); Console.WriteLine("data:"); Save(@"build\out\radio-net.bin", data); }
   catch(Exception e){ Console.WriteLine("NetReadIshList EX: "+e.GetType().Name+": "+e.Message); }
   try { object bytes = new byte[0]; int err=0; b.ReadBytesFromRadio(0,0,0,256,0, out bytes, out err); Console.WriteLine("ReadBytesFromRadio err="+err); Save(@"build\out\radio-bytes.bin", bytes); }
   catch(Exception e){ Console.WriteLine("ReadBytesFromRadio EX: "+e.GetType().Name+": "+e.Message); }
   try { object ver=null; GetVersion2(b, ref ver); Console.WriteLine("GetVersion:"); Save(@"build\out\radio-ver.bin", ver); }
   catch(Exception e){ Console.WriteLine("GetVersion EX: "+e.GetType().Name+": "+e.Message); }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
 static void GetVersion2(IRcmpBundler b, ref object ver){ b.GetVersion(ref ver, 0, ref ver); }
}
