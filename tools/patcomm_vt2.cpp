#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
struct IRawBytes : IUnknown {
    virtual HRESULT __stdcall Broadcast(BYTE* pbtData, int lDataLen) = 0;
    virtual HRESULT __stdcall Read(BYTE* pbtData, int* plBufferLen) = 0;
    virtual HRESULT __stdcall Request(BYTE* pbtData, int lDataLen, int lWaitTime, BYTE* pbtReplyData, int* plReplyBufLen) = 0;
    virtual HRESULT __stdcall Open(int lProtocol, int lFlags, int lAdvFlags) = 0;
    virtual HRESULT __stdcall Close() = 0;
    virtual HRESULT __stdcall GetConnectedModemFromPortMgr(BSTR* pbstrPort, int* pLFlags) = 0;
};
int main(int argc, char** argv){
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CLSID clsid; CLSIDFromProgID(L"PatComm10.PatCommProto10.1", &clsid);
    IUnknown* u=nullptr;
    HRESULT hr=CoCreateInstance(clsid,nullptr,CLSCTX_INPROC_SERVER,IID_IUnknown,(void**)&u);
    printf("create hr=0x%08lX u=%p\n",(unsigned long)hr,(void*)u); if(!u) return 1;
    GUID iid={0xD9DCF31B,0x7FB0,0x4DDA,{0x82,0xB1,0x52,0x03,0x64,0x2A,0x3A,0x4B}};
    IRawBytes* rb=nullptr; hr=u->QueryInterface(iid,(void**)&rb);
    printf("QI IRawBytes hr=0x%08lX rb=%p\n",(unsigned long)hr,(void*)rb); if(!rb) return 1;
    BSTR port=nullptr; int flags=0;
    hr=rb->GetConnectedModemFromPortMgr(&port,&flags);
    printf("GetConnectedModemFromPortMgr hr=0x%08lX port='%ls' flags=%d\n",(unsigned long)hr,port?port:L"(null)",flags);
    for(int proto=0;proto<=8;proto++){
        HRESULT h=rb->Open(proto,flags,0);
        printf("Open(%d,%d,0) hr=0x%08lX\n",proto,flags,(unsigned long)h);
        if(SUCCEEDED(h)){ printf("  open succeeded; closing\n"); rb->Close(); }
    }
    if(port) SysFreeString(port);
    rb->Release(); u->Release(); CoUninitialize(); return 0;
}
