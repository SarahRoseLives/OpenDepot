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
   object ish = new byte[0];
   object data = null;
   try { b.ReadIshList(ref ish, out data); Console.WriteLine("ReadIshList OK"); Console.WriteLine("ish:"); Save(@"build\out\radio-ish.bin", ish); Console.WriteLine("data:"); Save(@"build\out\radio-data.bin", data); }
   catch(Exception e){ Console.WriteLine("ReadIshList EX: "+e.GetType().Name+": "+e.Message); }
   object rc = new byte[0];
   try { b.ReadRadioConfiguration(ref rc, 0); Console.WriteLine("ReadRadioConfiguration OK"); Save(@"build\out\radio-config.bin", rc); }
   catch(Exception e){ Console.WriteLine("ReadRadioConfiguration EX: "+e.GetType().Name+": "+e.Message); }
   for(int part=0; part<4; part++){
     object d = new byte[0];
     try { b.ReadAllTypeInstances(ref d, part, 0); Console.WriteLine("ReadAllTypeInstances(part="+part+",type=0) OK"); Save(@"build\out\radio-part"+part+".bin", d); }
     catch(Exception e){ Console.WriteLine("ReadAllTypeInstances(part="+part+") EX: "+e.GetType().Name+": "+e.Message); }
   }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
