using System;
using NEXTGENLib;
class P {
 static object Make(string prog){ try { var o=Activator.CreateInstance(Type.GetTypeFromProgID(prog)); Console.WriteLine("created "+prog+" -> "+o.GetType().FullName); return o; } catch(Exception e){ Console.WriteLine("create "+prog+" EX: "+e.GetType().Name+": "+e.Message); return null; } }
 [STAThread] static void Main(string[] a){
   Console.WriteLine("x86="+(IntPtr.Size==4));
   // low level raw rs232
   var r=Make("NextGen.RawRS232Protocol.1");
   if(r!=null){ var raw=(IRawRS232Protocol)r; try{ raw.Open(0,0); Console.WriteLine("RawRS232.Open OK"); }catch(Exception e){ Console.WriteLine("RawRS232.Open EX: "+e.GetType().Name+": "+e.Message); } }
   // rcmp protocol
   var p=Make("NextGen.RcmpProtocol.1");
   if(p!=null){ var rp=(IRcmpProtocol)p; foreach(var port in new[]{"COM20","USB","OTAP"}){ try{ string pp=port; rp.OpenPort(4, ref pp, null); Console.WriteLine("RcmpProtocol.OpenPort("+port+",4) OK"); }catch(Exception e){ Console.WriteLine("RcmpProtocol.OpenPort("+port+",4) EX: "+e.GetType().Name+": "+e.Message); } } }
   // bundler protocols
   foreach(int proto in new[]{4,8,11}){
     var o=Make("NextGen.RcmpBundler.1"); if(o==null) continue; var b=(IRcmpBundler)o;
     string port = proto==11?"OTAP":(proto==8?"USB":"COM20");
     try{ object pars=null; b.Open(proto, ref port, pars); Console.WriteLine("Bundler.Open("+proto+","+port+") OK"); }
     catch(Exception e){ Console.WriteLine("Bundler.Open("+proto+","+port+") EX: "+e.GetType().Name+": "+e.Message); }
   }
 }
}
