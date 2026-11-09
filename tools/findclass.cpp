#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
int main(){
  CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  GUID clsid={0xDEC90FAF,0x8735,0x4DB9,{0xA1,0x7A,0xD8,0xBF,0x74,0x5C,0x22,0x54}};
  const char* mods[]={
    "C:\\Windows\\SysWOW64\\NextGen10.dll",
    "C:\\Program Files\\Common Files\\Motorola\\RDK\\ADK7_2_SP04_SR13\\PBT2.dll",
    "C:\\Windows\\SysWOW64\\PatComm10.dll",
    "C:\\Windows\\SysWOW64\\RadioSerial10.dll",
    "C:\\Windows\\SysWOW64\\PortManager10.dll",
    "C:\\Windows\\SysWOW64\\RadioCustStack10.dll",
    "C:\\Windows\\SysWOW64\\ByteData10.dll",
    "C:\\Windows\\SysWOW64\\fudally_usb.dll",
  };
  for(int i=0;i<8;i++){
    HMODULE h=LoadLibraryA(mods[i]);
    printf("%s h=%p\n",mods[i],(void*)h);
    if(!h) continue;
    auto fn=(HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*))GetProcAddress(h,"DllGetClassObject");
    printf("  DllGetClassObject=%p\n",(void*)fn);
    if(fn){
      IClassFactory* cf=nullptr;
      HRESULT hr=fn(clsid,IID_IClassFactory,(void**)&cf);
      printf("  GetClassObject(clsid,IClassFactory) hr=0x%08lX cf=%p\n",(unsigned long)hr,(void*)cf);
      if(cf) cf->Release();
      IUnknown* u=nullptr;
      hr=fn(clsid,IID_IUnknown,(void**)&u);
      printf("  GetClassObject(clsid,IUnknown) hr=0x%08lX u=%p\n",(unsigned long)hr,(void*)u);
      if(u) u->Release();
    }
  }
  CoUninitialize();
  return 0;
}
