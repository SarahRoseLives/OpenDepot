using System;
using System.Runtime.InteropServices;
using NEXTGENLib;
class P { [STAThread] static void Main(){
  var b=(IRcmpBundler)Activator.CreateInstance(Type.GetTypeFromProgID("NextGen.RcmpBundler.1"));
  IntPtr punk=Marshal.GetIUnknownForObject(b); IntPtr vtbl=Marshal.ReadIntPtr(punk,0);
  Console.WriteLine("punk=0x"+punk.ToString("X")+" vtbl=0x"+vtbl.ToString("X"));
  for(int s=7;s<=20;s++) Console.WriteLine("slot "+s+" -> 0x"+Marshal.ReadIntPtr(vtbl,s*4).ToString("X"));
  Marshal.Release(punk);
}}
