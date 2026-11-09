using System;
using System.Reflection;
class P {
  static void Main(string[] a){
    var asm = Assembly.LoadFrom(a[0]);
    foreach(var t in asm.GetTypes()){
      Console.WriteLine("TYPE " + t.FullName + " [" + t.Attributes + "]");
      foreach(var m in t.GetMethods(BindingFlags.Public|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly)){
        var ps = m.GetParameters();
        Console.WriteLine("  " + m.ReturnType.Name + " " + m.Name + "(" + string.Join(", ", Array.ConvertAll(ps, p => p.ParameterType.Name + " " + p.Name)) + ")");
      }
    }
  }
}
