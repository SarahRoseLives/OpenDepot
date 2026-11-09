#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <stdio.h>
int main(){
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ITypeLib* tlb=nullptr;
  HRESULT hr=LoadTypeLibEx(L"C:\\Windows\\SysWOW64\\NextGen10.dll", REGKIND_NONE, &tlb);
  printf("LoadTypeLibEx hr=0x%08lX tlb=%p\n",(unsigned long)hr,(void*)tlb);
  if(!tlb) return 1;
  UINT n=tlb->GetTypeInfoCount(); printf("types=%u\n",n);
  for(UINT i=0;i<n;i++){
    ITypeInfo* ti=nullptr; if(FAILED(tlb->GetTypeInfo(i,&ti))||!ti) continue;
    TYPEATTR* ta=nullptr;
    if(SUCCEEDED(ti->GetTypeAttr(&ta))&&ta){
      BSTR nm=nullptr; tlb->GetDocumentation(i,&nm,nullptr,nullptr,nullptr);
      if(nm && wcscmp(nm,L"IRcmpBundler")==0){
        printf("TYPE %ls kind=%d guid={%08lX-%04X-%04X-...} cFuncs=%u\n",nm,(int)ta->typekind,
          (unsigned long)ta->guid.Data1,ta->guid.Data2,ta->guid.Data3,ta->cFuncs);
        for(WORD f=0;f<ta->cFuncs;f++){
          FUNCDESC* fd=nullptr; if(FAILED(ti->GetFuncDesc(f,&fd))||!fd) continue;
          BSTR fn=nullptr; ti->GetDocumentation(fd->memid,&fn,nullptr,nullptr,nullptr);
          printf("  oVft=0x%04X slot=%d  %ls  cParams=%d\n",(unsigned)fd->oVft,(int)(fd->oVft/4),fn?fn:L"",(int)fd->cParams);
          SysFreeString(fn); ti->ReleaseFuncDesc(fd);
        }
      }
      SysFreeString(nm); ti->ReleaseTypeAttr(ta);
    }
    ti->Release();
  }
  tlb->Release(); CoUninitialize(); return 0;
}
