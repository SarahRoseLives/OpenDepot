#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

// IRawBytes vtable (PatComm10 adds GetConnectedModemFromPortMgr after Close)
struct IRawBytes : IUnknown {
    virtual HRESULT __stdcall Broadcast(BYTE* pbtData, int lDataLen) = 0;
    virtual HRESULT __stdcall Read(BYTE* pbtData, int* plBufferLen) = 0;
    virtual HRESULT __stdcall Request(BYTE* pbtData, int lDataLen, int lWaitTime,
                                      BYTE* pbtReplyData, int* plReplyBufLen) = 0;
    virtual HRESULT __stdcall Open(int lProtocol, int lFlags, int lAdvFlags) = 0;
    virtual HRESULT __stdcall Close() = 0;
};

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CLSID clsid;
    CLSIDFromProgID(L"PatComm10.PatCommProto10.1", &clsid);
    IUnknown* u = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&u);
    printf("CoCreateInstance(IUnknown) hr=0x%08lX u=%p\n", (unsigned long)hr, (void*)u);
    if (!u) return 1;

    IRawBytes* rb = nullptr;
    GUID iid = {0xD9DCF31B, 0x7FB0, 0x4DDA, {0x82, 0xB1, 0x52, 0x03, 0x64, 0x2A, 0x3A, 0x4B}};
    hr = u->QueryInterface(iid, (void**)&rb);
    printf("QI IRawBytes hr=0x%08lX rb=%p\n", (unsigned long)hr, (void*)rb);
    IDispatch* disp = nullptr;
    hr = u->QueryInterface(IID_IDispatch, (void**)&disp);
    printf("QI IDispatch hr=0x%08lX disp=%p\n", (unsigned long)hr, (void*)disp);

    if (rb) {
        for (int proto = 0; proto <= 4; proto++) {
            printf("Open(%d,0,0)...\n", proto);
            HRESULT h = rb->Open(proto, 0, 0);
            printf("  hr=0x%08lX\n", (unsigned long)h);
        }
        printf("Close...\n");
        printf("  hr=0x%08lX\n", (unsigned long)rb->Close());
        rb->Release();
    }
    if (disp) disp->Release();
    u->Release();
    CoUninitialize();
    return 0;
}
