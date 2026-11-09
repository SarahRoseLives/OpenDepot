#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

typedef HRESULT (__stdcall *PFN_GetClassObject)(REFCLSID, REFIID, void**);
typedef HRESULT (__stdcall *PFN_CanUnloadNow)();
typedef HRESULT (__stdcall *PFN_RegisterServer)();
typedef HRESULT (__stdcall *PFN_UnregisterServer)();

static HMODULE g_real = NULL;
static PFN_GetClassObject g_realGCO = NULL;
static PFN_CanUnloadNow g_realCUN = NULL;
static PFN_RegisterServer g_realRS = NULL;
static PFN_UnregisterServer g_realUS = NULL;
static FILE* g_log = NULL;

static const CLSID CLSID_VirtualRadio =
    {0xCDC45D7E,0x0681,0x11D5,{0xA7,0x4A,0x00,0x10,0xB5,0x7A,0x9C,0x7A}};
static const CLSID CLSID_RcmpBundler =
    {0xE42ABAD6,0x915E,0x4E4F,{0x97,0xE1,0x4B,0x7E,0x37,0xB3,0xB4,0x75}};

static void Logf(const char* fmt, ...) {
    if (!g_log) g_log = fopen("C:\\Users\\Rose\\AppData\\Local\\Temp\\opencode\\ngproxy.log", "a");
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static void DumpVariant(const char* tag, VARIANT* v) {
    if (!v) { Logf("%s: null variant\n", tag); return; }
    Logf("%s: vt=0x%04x\n", tag, v->vt);
    if ((v->vt & VT_ARRAY) && (v->vt & VT_UI1) && v->parray) {
        SAFEARRAY* psa = v->parray;
        LONG lb = 0, ub = 0;
        SafeArrayGetLBound(psa, 1, &lb);
        SafeArrayGetUBound(psa, 1, &ub);
        LONG n = ub - lb + 1;
        void* data = NULL;
        if (SUCCEEDED(SafeArrayAccessData(psa, &data))) {
            FILE* f = fopen("C:\\Users\\Rose\\AppData\\Local\\Temp\\opencode\\writecodeplug.bin", "wb");
            if (f) { fwrite(data, 1, n, f); fclose(f); }
            SafeArrayUnaccessData(psa);
            Logf("%s: dumped %ld bytes\n", tag, n);
        }
    }
}

typedef struct _PATCH { void** real; void** patched; } PATCH;
/* per-class patched vtables so multiple objects of the same class share one copy */
static void** g_vrReal = NULL; static void** g_vrPatched = NULL;
static void** g_bReal = NULL;  static void** g_bPatched = NULL;

static HRESULT __stdcall My_Open(void* self, int protocol, BSTR* port, VARIANT* params,
                                 void* uuid, unsigned cpgver, int delVoice, unsigned sysver, BSTR* sysrel) {
    Logf("VR OPEN proto=%d port=%ls cpgver=%u sysver=%u sysrel=%ls\n", protocol,
         port && *port ? *port : L"", cpgver, sysver, sysrel && *sysrel ? *sysrel : L"");
    typedef HRESULT (__stdcall *FN)(void*, int, BSTR*, VARIANT*, void*, unsigned, int, unsigned, BSTR*);
    return ((FN)g_vrReal[7])(self, protocol, port, params, uuid, cpgver, delVoice, sysver, sysrel);
}
static HRESULT __stdcall My_WriteCodeplug(void* self, VARIANT* data) {
    DumpVariant("VR WRITECODEPLUG", data);
    typedef HRESULT (__stdcall *FN)(void*, VARIANT*);
    return ((FN)g_vrReal[6])(self, data);
}
static HRESULT __stdcall My_BOpen(void* self, int protocol, BSTR* port, VARIANT* params) {
    Logf("B OPEN proto=%d port=%ls\n", protocol, port && *port ? *port : L"");
    typedef HRESULT (__stdcall *FN)(void*, int, BSTR*, VARIANT*);
    return ((FN)g_bReal[11])(self, protocol, port, params);
}
static HRESULT __stdcall My_WriteIshList(void* self, VARIANT* list) {
    DumpVariant("B WRITEISHLIST", list);
    typedef HRESULT (__stdcall *FN)(void*, VARIANT*);
    return ((FN)g_bReal[8])(self, list);
}
static HRESULT __stdcall My_NetWriteIshList(void* self, VARIANT* list) {
    DumpVariant("B NETWRITEISHLIST", list);
    typedef HRESULT (__stdcall *FN)(void*, VARIANT*);
    return ((FN)g_bReal[8])(self, list); /* not used for serial; best-effort */
}

static void PatchVtable(void* obj, REFCLSID clsid) {
    if (!obj) return;
    void** vt = *(void***)obj;
    if (!vt) return;
    if (IsEqualCLSID(clsid, CLSID_VirtualRadio)) {
        if (!g_vrPatched) {
            g_vrReal = vt; g_vrPatched = (void**)calloc(96, sizeof(void*));
            for (int i = 0; i < 96; i++) g_vrPatched[i] = vt[i];
            g_vrPatched[6] = (void*)My_WriteCodeplug;
            g_vrPatched[7] = (void*)My_Open;
            Logf("VR base=%p vt=%p\n", g_real, vt);
        }
        *(void***)obj = g_vrPatched;
        Logf("VR PATCHED obj=%p\n", obj);
    } else if (IsEqualCLSID(clsid, CLSID_RcmpBundler)) {
        if (!g_bPatched) {
            g_bReal = vt; g_bPatched = (void**)calloc(96, sizeof(void*));
            for (int i = 0; i < 96; i++) g_bPatched[i] = vt[i];
            g_bPatched[8] = (void*)My_WriteIshList;
            g_bPatched[11] = (void*)My_BOpen;
            Logf("B base=%p vt=%p\n", g_real, vt);
        }
        *(void***)obj = g_bPatched;
        Logf("B PATCHED obj=%p\n", obj);
    }
}

class CFactory : public IClassFactory {
    IClassFactory* real; CLSID clsid; LONG ref;
public:
    CFactory(IClassFactory* r, REFCLSID c) : real(r), clsid(c), ref(1) { real->AddRef(); }
    ~CFactory() { if (real) real->Release(); }
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = (IClassFactory*)this; AddRef(); return S_OK; }
        return real->QueryInterface(riid, ppv);
    }
    ULONG __stdcall AddRef() { return InterlockedIncrement(&ref); }
    ULONG __stdcall Release() { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return r; }
    HRESULT __stdcall CreateInstance(IUnknown* outer, REFIID riid, void** ppv) {
        HRESULT hr = real->CreateInstance(outer, riid, ppv);
        if (SUCCEEDED(hr) && ppv && *ppv) PatchVtable(*ppv, clsid);
        return hr;
    }
    HRESULT __stdcall LockServer(BOOL f) { return real->LockServer(f); }
};

static bool LoadReal() {
    if (g_real) return true;
    g_real = LoadLibraryA("C:\\Windows\\SysWOW64\\NextGen10.dll");
    if (!g_real) return false;
    g_realGCO = (PFN_GetClassObject)GetProcAddress(g_real, "DllGetClassObject");
    g_realCUN = (PFN_CanUnloadNow)GetProcAddress(g_real, "DllCanUnloadNow");
    g_realRS = (PFN_RegisterServer)GetProcAddress(g_real, "DllRegisterServer");
    g_realUS = (PFN_UnregisterServer)GetProcAddress(g_real, "DllUnregisterServer");
    return g_realGCO != NULL;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!LoadReal()) return CLASS_E_CLASSNOTAVAILABLE;
    IClassFactory* real = NULL;
    HRESULT hr = g_realGCO(rclsid, IID_IClassFactory, (void**)&real);
    if (FAILED(hr)) return hr;
    CFactory* f = new CFactory(real, rclsid);
    hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return g_realCUN ? g_realCUN() : S_FALSE; }
extern "C" HRESULT __stdcall DllRegisterServer() { return g_realRS ? g_realRS() : S_OK; }
extern "C" HRESULT __stdcall DllUnregisterServer() { return g_realUS ? g_realUS() : S_OK; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) Logf("proxy attach\n");
    return TRUE;
}
