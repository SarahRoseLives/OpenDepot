using System;
using System.IO;
using NEXTGENLib;
class P {
 static void Save(string path, object o){
   if(o==null){ Console.WriteLine("  (null)"); return; }
   Console.WriteLine("  type="+o.GetType().FullName);
   if(o is Array){
     Array a=(Array)o; Console.WriteLine("  len="+a.Length+" elem="+a.GetType().GetElementType());
     if(o is byte[]){ byte[] b=(byte[])o; File.WriteAllBytes(path,b); Console.WriteLine("  saved "+b.Length); }
     return;
   }
 }
 [STAThread] static void Main(string[] a){
   string port=a.Length>0?a[0]:"COM20";
   var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
   tagCAUUID u=new tagCAUUID(); string sysrel="";
   try { v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK"); }
   catch(Exception e){ Console.WriteLine("Open EX: "+e.GetType().Name+": "+e.Message); return; }
   IRcmpBundler b=null;
   try { v.GetBundlerPointer(ref b); Console.WriteLine("GetBundlerPointer OK b="+(b==null?"null":b.GetType().FullName)); }
   catch(Exception e){ Console.WriteLine("GetBundlerPointer EX: "+e.GetType().Name+": "+e.Message); }
   if(b!=null){
     try { b.EnterProgramMode(); Console.WriteLine("EnterProgramMode OK"); } catch(Exception e){ Console.WriteLine("EnterProgramMode EX: "+e.GetType().Name+": "+e.Message); }
     try { string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode='"+fc+"'"); } catch(Exception e){ Console.WriteLine("GetFlashCode EX: "+e.GetType().Name+": "+e.Message); }
     try { string uuid=""; b.GetRadioUUID(ref uuid); Console.WriteLine("RadioUUID='"+uuid+"'"); } catch(Exception e){ Console.WriteLine("GetRadioUUID EX: "+e.GetType().Name+": "+e.Message); }
     try { object ish=null, data=null; b.ReadIshList(ref ish, out data); Console.WriteLine("ReadIshList OK"); Console.WriteLine("ish:"); Save(@"build\out\radio-ish.bin", ish); Console.WriteLine("data:"); Save(@"build\out\radio-data.bin", data); }
     catch(Exception e){ Console.WriteLine("ReadIshList EX: "+e.GetType().Name+": "+e.Message); }
   }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
