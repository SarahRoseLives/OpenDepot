using System;
class P { [STAThread] static void Main(){
 string[] ids={"NextGen.VirtualRadio.1","NextGen.RcmpProtocol.1","NextGen.RcmpBundler.1","NextGen.RcmpBuilder.1","NextGen.RawRS232Protocol.1","NextGen.SRecHandler.1","PatComm10.PatCommProto10.1","ByteData.RawBytes.1","MultiIntPbt4.Pbt4.1"};
 foreach(var id in ids){ try{ var t=Type.GetTypeFromProgID(id); if(t==null){ Console.WriteLine(id+" -> ProgID not found"); continue;} var o=Activator.CreateInstance(t); Console.WriteLine(id+" -> "+o.GetType().FullName); } catch(Exception e){ Console.WriteLine(id+" EX: "+e.GetType().Name+": "+e.Message); } }
}}
