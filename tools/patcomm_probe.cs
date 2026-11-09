using System;
class P {
 [STAThread] static void Main(string[] a){
   dynamic o = Activator.CreateInstance(Type.GetTypeFromProgID("PatComm10.PatCommProto10.1"));
   Console.WriteLine("created "+o.GetType().FullName);
   string port=""; int flags=0;
   try { o.GetConnectedModemFromPortMgr(out port, out flags); Console.WriteLine("GetConnectedModemFromPortMgr port='"+port+"' flags="+flags); }
   catch(Exception e){ Console.WriteLine("GetConnectedModemFromPortMgr EX: "+e.GetType().Name+": "+e.Message); }
   if(a.Length>0) port=a[0];
   foreach(int proto in new[]{0,1,2,3,4}){
     try { o.Open(proto, flags, 0); Console.WriteLine("Open("+proto+","+flags+",0) OK"); }
     catch(Exception e){ Console.WriteLine("Open("+proto+","+flags+",0) EX: "+e.GetType().Name+": "+e.Message); }
   }
   try { o.Close(); Console.WriteLine("Close OK"); } catch(Exception e){ Console.WriteLine("Close EX: "+e.GetType().Name+": "+e.Message); }
 }
}
