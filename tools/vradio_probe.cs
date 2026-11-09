using System;
using NEXTGENLib;
class P {
 [STAThread] static void Main(string[] a){
   string port = a.Length>0?a[0]:"COM20";
   int proto = a.Length>1?int.Parse(a[1]):4;
   Console.WriteLine("CLR="+Environment.Version+" x86="+(IntPtr.Size==4));
   object o = Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
   Console.WriteLine("object="+o.GetType().FullName);
   var b = (IRcmpBundler)o;
   try { object pars=null; b.Open(proto, ref port, pars); Console.WriteLine("Open OK port="+port); }
   catch(Exception e){ Console.WriteLine("Open EX: "+e.GetType().Name+": "+e.Message); return; }
   try { b.EnterProgramMode(); Console.WriteLine("EnterProgramMode OK"); } catch(Exception e){ Console.WriteLine("EPM EX: "+e.GetType().Name+": "+e.Message); }
   try { string fc=""; b.GetFlashCode(ref fc); Console.WriteLine("FlashCode="+fc); } catch(Exception e){ Console.WriteLine("FC EX: "+e.GetType().Name+": "+e.Message); }
   try { object ish=null, data=null; b.ReadIshList(ref ish, out data); Console.WriteLine("ReadIshList OK ish="+(ish==null?"null":ish.GetType().FullName)+" data="+(data==null?"null":data.GetType().FullName)); }
   catch(Exception e){ Console.WriteLine("RIL EX: "+e.GetType().Name+": "+e.Message); }
   try { b.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
