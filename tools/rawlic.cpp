#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <stdio.h>
int main(){
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HMODULE h=LoadLibraryA("C:\\Windows\\SysWOW64\\ByteData10.dll");
  auto fn=(HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*))GetProcAddress(h,"DllGetClassObject");
  GUID clsid={0x7AC0F7B4,0x9CBF,0x4544,{0x84,0x17,0xDD,0x39,0x8A,0xCF,0xAD,0x4D}};
  IClassFactory* cf=nullptr; HRESULT hr=fn(clsid,IID_IClassFactory,(void**)&cf);
  printf("cf hr=0x%08lX cf=%p\n",(unsigned long)hr,(void*)cf);
  if(!cf) return 1;
  IClassFactory2* cf2=nullptr; hr=cf->QueryInterface(IID_IClassFactory2,(void**)&cf2);
  printf("QI IClassFactory2 hr=0x%08lX cf2=%p\n",(unsigned long)hr,(void*)cf2);
  if(cf2){
    LICINFO li={sizeof(li)}; hr=cf2->GetLicInfo(&li);
    printf("GetLicInfo hr=0x%08lX runtimeKeyAvail=%d licVerified=%d\n",(unsigned long)hr,li.fRuntimeKeyAvail,li.fLicVerified);
    BSTR key=nullptr; hr=cf2->RequestLicKey(0,&key);
    printf("RequestLicKey hr=0x%08lX key=%ls\n",(unsigned long)hr,key?key:L"(null)");
    IUnknown* u=nullptr; hr=cf2->CreateInstanceLic(nullptr,nullptr,IID_IUnknown,key,(void**)&u);
    printf("CreateInstanceLic hr=0x%08lX u=%p\n",(unsigned long)hr,(void*)u);
    if(u){
      IDispatch* d=nullptr; HRESULT h2=u->QueryInterface(IID_IDispatch,(void**)&d);
      printf("QI IDispatch hr=0x%08lX d=%p\n",(unsigned long)h2,(void*)d);
      if(d) d->Release();
      u->Release();
    }
    if(key) SysFreeString(key);
    cf2->Release();
  }
  cf->Release();
  CoUninitialize(); return 0;
}
