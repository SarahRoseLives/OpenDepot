using System; using System.Reflection;
class P { static void Main(string[] a){ var asm=Assembly.LoadFrom(a[0]); foreach(var t in asm.GetTypes()){ if(t.IsInterface && t.Namespace=="NEXTGENLib") Console.WriteLine(t.Name+" "+t.GUID); } } }
