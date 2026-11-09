#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
struct G { const char* name; GUID g; };
int main(){
  CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  G gs[]={
    {"MISSING DEC90FAF",{0xDEC90FAF,0x8735,0x4DB9,{0xA1,0x7A,0xD8,0xBF,0x74,0x5C,0x22,0x54}}},
    {"PatComm 4F65EAF3",{0x4F65EAF3,0x3C05,0x4120,{0x9B,0x50,0x0E,0x80,0x95,0x6E,0x6F,0x61}}},
    {"RawBytes 7AC0F7B4",{0x7AC0F7B4,0x9CBF,0x4544,{0x84,0x17,0xDD,0x39,0x8A,0xCF,0xAD,0x4D}}},
    {"Pbt4 96B4EC82",{0x96B4EC82,0x4EE2,0x11D5,{0xA2,0x19,0x00,0x60,0xB0,0xFC,0x83,0xF1}}},
  };
  const char* mods[]={
    "C:\\Windows\\SysWOW64\\NextGen10.dll",
    "C:\\Program Files\\Common Files\\Motorola\\RDK\\ADK7_2_SP04_SR13\\PBT2.dll",
    "C:\\Windows\\SysWOW64\\PatComm10.dll",
    "C:\\Windows\\SysWOW64\\ByteData10.dll",
  };
  for(int i=0;i<4;i++){
    HMODULE h=LoadLibraryA(mods[i]); if(!h) continue;
    auto fn=(HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*))GetProcAddress(h,"DllGetClassObject");
    if(!fn) continue;
    for(int j=0;j<4;j++){
      IClassFactory* cf=nullptr;
      HRESULT hr=fn(gs[j].g,IID_IClassFactory,(void**)&cf);
      printf("%-28s in %-16s hr=0x%08lX cf=%p\n",gs[j].name,mods[i],(unsigned long)hr,(void*)cf);
      if(cf) cf->Release();
    }
  }
  CoUninitialize(); return 0;
}
