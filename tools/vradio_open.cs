using System;
using NEXTGENLib;
class P {
 [STAThread] static void Main(string[] a){
   string port = a.Length>0?a[0]:"COM20";
   int proto = a.Length>1?int.Parse(a[1]):4;
   Console.WriteLine("x86="+(IntPtr.Size==4));
   var t=Type.GetTypeFromProgID("NextGen.VirtualRadio.1");
   var o=Activator.CreateInstance(t);
   var v=(IVirtualRadio)o;
   Console.WriteLine("created "+o.GetType().FullName);
   tagCAUUID u=new tagCAUUID();
   Console.WriteLine("cElems="+u.cElems+" pElems="+u.pElems);
   string sysrel="";
   try { v.Open(proto, ref port, null, ref u, 0, 0, 0, ref sysrel); Console.WriteLine("Open OK port="+port+" sysrel='"+sysrel+"'"); }
   catch(Exception e){ Console.WriteLine("Open EX: "+e.GetType().Name+": "+e.Message); return; }
   try { string sn=""; v.ReadWriteRadioSerialNumber(ref sn, 1); Console.WriteLine("Serial="+sn); } catch(Exception e){ Console.WriteLine("Serial EX: "+e.GetType().Name+": "+e.Message); }
   try { string mn=""; v.ReadWriteRadioModelNumber(ref mn, 1); Console.WriteLine("Model="+mn); } catch(Exception e){ Console.WriteLine("Model EX: "+e.GetType().Name+": "+e.Message); }
   try { object ver=null; int hr=v.GetVersion(0, ref ver); Console.WriteLine("GetVersion hr="+hr+" ver="+(ver==null?"null":ver.ToString())); } catch(Exception e){ Console.WriteLine("GetVersion EX: "+e.GetType().Name+": "+e.Message); }
   try { v.Close(ref port); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
