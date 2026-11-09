using System;
using System.IO;
using NEXTGENLib;
class P {
 static void SaveObj(string path, object o){
   if(o==null){ Console.WriteLine("  (null)"); return; }
   Console.WriteLine("  type="+o.GetType().FullName);
   if(o is Array){
     Array arr=(Array)o;
     Console.WriteLine("  rank="+arr.Rank+" len="+arr.Length+" elem="+arr.GetType().GetElementType());
     if(o is byte[]){
       byte[] b=(byte[])o;
       File.WriteAllBytes(path,b);
       Console.WriteLine("  saved "+path+" bytes="+b.Length);
       return;
     }
     int n=0;
     foreach(object x in arr){ if(n<8) Console.Write(" "+x); n++; }
     Console.WriteLine(" ...");
     return;
   }
   Console.WriteLine("  value="+o.ToString());
 }
 [STAThread] static void Main(string[] a){
   string port=a.Length>0?a[0]:"COM20";
   Console.WriteLine("x86="+(IntPtr.Size==4));
   var v=(IVirtualRadio)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.VirtualRadio.1"));
   tagCAUUID u=new tagCAUUID(); string sysrel="";
   try { v.Open(4, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK sysrel='"+sysrel+"'"); }
   catch(Exception e){ Console.WriteLine("Open EX: "+e.GetType().Name+": "+e.Message); return; }
   for(int i=0;i<2;i++){
     int flag=i;
     try { string s=""; v.ReadWriteRadioSerialNumber(ref s, flag); Console.WriteLine("Serial flag="+flag+" '"+s+"'"); }
     catch(Exception e){ Console.WriteLine("Serial flag="+flag+" EX: "+e.GetType().Name+": "+e.Message); }
   }
   for(int i=0;i<2;i++){
     int flag=i;
     try { string s=""; v.ReadWriteRadioModelNumber(ref s, flag); Console.WriteLine("Model flag="+flag+" '"+s+"'"); }
     catch(Exception e){ Console.WriteLine("Model flag="+flag+" EX: "+e.GetType().Name+": "+e.Message); }
   }
   try { object rc=null; v.ReadRadioConfiguration(ref rc, 0); Console.WriteLine("ReadRadioConfiguration:"); SaveObj(@"build\out\radio-config.bin", rc); }
   catch(Exception e){ Console.WriteLine("ReadRadioConfiguration EX: "+e.GetType().Name+": "+e.Message); }
   try { object shop=null, data=null; v.ReadCodeplug(ref shop, ref data); Console.WriteLine("ReadCodeplug OK"); Console.WriteLine("shop:"); SaveObj(@"build\out\radio-shop.bin", shop); Console.WriteLine("data:"); SaveObj(@"build\out\radio-read.bin", data); }
   catch(Exception e){ Console.WriteLine("ReadCodeplug EX: "+e.GetType().Name+": "+e.Message); }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
