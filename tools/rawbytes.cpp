#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <stdio.h>

static void DumpTypeLib(const wchar_t* path){
  ITypeLib* tlb=nullptr;
  HRESULT hr=LoadTypeLibEx(path, REGKIND_NONE, &tlb);
  printf("LoadTypeLibEx hr=0x%08lX tlb=%p\n",(unsigned long)hr,(void*)tlb);
  if(!tlb) return;
  UINT n=tlb->GetTypeInfoCount(); printf("types=%u\n",n);
  for(UINT i=0;i<n;i++){
    ITypeInfo* ti=nullptr; if(FAILED(tlb->GetTypeInfo(i,&ti))||!ti) continue;
    TYPEATTR* ta=nullptr;
    if(SUCCEEDED(ti->GetTypeAttr(&ta))&&ta){
      BSTR name=nullptr; tlb->GetDocumentation(i,&name,nullptr,nullptr,nullptr);
      printf("TYPE[%u] kind=%d guid={%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} name=%ls funcs=%u\n",
        i,(int)ta->typekind,(unsigned long)ta->guid.Data1,ta->guid.Data2,ta->guid.Data3,
        ta->guid.Data4[0],ta->guid.Data4[1],ta->guid.Data4[2],ta->guid.Data4[3],ta->guid.Data4[4],ta->guid.Data4[5],ta->guid.Data4[6],ta->guid.Data4[7],
        name?name:L"",ta->cFuncs);
      if(ta->typekind==TKIND_INTERFACE||ta->typekind==TKIND_DISPATCH){
        for(WORD f=0;f<ta->cFuncs;f++){
          FUNCDESC* fd=nullptr; if(FAILED(ti->GetFuncDesc(f,&fd))||!fd) continue;
          BSTR fn=nullptr; ti->GetDocumentation(fd->memid,&fn,nullptr,nullptr,nullptr);
          printf("   FUNC %ls invkind=%d cParams=%d\n",fn?fn:L"",(int)fd->invkind,(int)fd->cParams);
          for(short p=0;p<fd->cParams;p++){
            printf("      param[%d] vt=%u flags=0x%04X\n",p,fd->lprgscode?0:0, fd->lprgelemdescParam?fd->lprgelemdescParam[p].tdesc.vt:0, fd->lprgelemdescParam?fd->lprgelemdescParam[p].paramdesc.wParamFlags:0);
          }
          SysFreeString(fn);
          ti->ReleaseFuncDesc(fd);
        }
      }
      SysFreeString(name);
      ti->ReleaseTypeAttr(ta);
    }
    ti->Release();
  }
  tlb->Release();
}

int main(){
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  DumpTypeLib(L"C:\\Windows\\SysWOW64\\ByteData10.dll");
  DumpTypeLib(L"C:\\Windows\\SysWOW64\\PatComm10.dll");

  HMODULE h=LoadLibraryA("C:\\Windows\\SysWOW64\\ByteData10.dll");
  auto fn=(HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*))GetProcAddress(h,"DllGetClassObject");
  GUID clsid={0x7AC0F7B4,0x9CBF,0x4544,{0x84,0x17,0xDD,0x39,0x8A,0xCF,0xAD,0x4D}};
  IClassFactory* cf=nullptr; HRESULT hr=fn(clsid,IID_IClassFactory,(void**)&cf);
  printf("DllGetClassObject hr=0x%08lX cf=%p\n",(unsigned long)hr,(void*)cf);
  if(cf){
    IUnknown* u=nullptr; hr=cf->CreateInstance(nullptr,IID_IUnknown,(void**)&u);
    printf("CreateInstance(IUnknown) hr=0x%08lX u=%p\n",(unsigned long)hr,(void*)u);
    if(u){
      IDispatch* d=nullptr; hr=u->QueryInterface(IID_IDispatch,(void**)&d);
      printf("QI IDispatch hr=0x%08lX d=%p\n",(unsigned long)hr,(void*)d);
      if(d) d->Release();
      u->Release();
    }
    cf->Release();
  }
  CoUninitialize();
  return 0;
}
