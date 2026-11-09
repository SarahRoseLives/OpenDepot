#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static HRESULT (WINAPI *RealCoCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*) = nullptr;

static void PrintGuid(const char* what, REFGUID g, const char* extra) {
    printf("%s {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}%s\n",
           what, (unsigned long)g.Data1, g.Data2, g.Data3,
           g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
           g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], extra ? extra : "");
    fflush(stdout);
}

static HRESULT WINAPI HookCoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext,
                                           REFIID riid, LPVOID* ppv) {
    PrintGuid("CoCreateInstance clsid=", rclsid, "");
    PrintGuid("                    iid=", riid, "");
    HRESULT hr = RealCoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    printf("                    hr=0x%08lX\n", (unsigned long)hr);
    fflush(stdout);
    return hr;
}

static void PatchModule(HMODULE h, const char* label) {
    if (!h) return;
    BYTE* base = (BYTE*)h;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    int patched = 0;
    for (; imp->Name; ++imp) {
        const char* dll = (const char*)(base + imp->Name);
        if (dll) {
            char low[64];
            size_t n = strlen(dll);
            if (n >= sizeof(low)) n = sizeof(low) - 1;
            for (size_t i = 0; i < n; ++i) low[i] = (char)tolower((unsigned char)dll[i]);
            low[n] = 0;
            if (!strstr(low, "ole32")) continue;
        }
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* orig = imp->OriginalFirstThunk
                                     ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk)
                                     : nullptr;
        for (; thunk->u1.Function; ++thunk) {
            const char* name = nullptr;
            if (orig && !(orig->u1.Ordinal & IMAGE_ORDINAL_FLAG))
                name = (const char*)(base + orig->u1.AddressOfData) + 2;
            if (orig) ++orig;
            if (!name) continue;
            if (strcmp(name, "CoCreateInstance") != 0) continue;
            DWORD old = 0;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                thunk->u1.Function = (ULONG_PTR)HookCoCreateInstance;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                patched++;
            }
        }
    }
    printf("patched %s: %d CoCreateInstance slot(s)\n", label, patched);
    fflush(stdout);
}

static void InvokeOpen(IDispatch* disp, int proto, const wchar_t* port) {
    OLECHAR* name = (OLECHAR*)L"Open";
    DISPID id = 0;
    HRESULT hr = disp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);
    printf("GetIDsOfNames(Open) hr=0x%08lX id=%ld\n", (unsigned long)hr, (long)id);
    if (FAILED(hr)) return;

    VARIANT args[3];
    VariantInit(&args[0]);
    VariantInit(&args[1]);
    VariantInit(&args[2]);
    // dispatch parameters are in reverse order: protocol, port, params
    args[2].vt = VT_I4;
    args[2].lVal = proto;
    BSTR b = SysAllocString(port);
    args[1].vt = VT_BSTR | VT_BYREF;
    args[1].pbstrVal = &b;
    args[0].vt = VT_EMPTY;
    DISPPARAMS dp;
    dp.rgvarg = args;
    dp.rgdispidNamedArgs = nullptr;
    dp.cArgs = 3;
    dp.cNamedArgs = 0;
    VARIANT result;
    VariantInit(&result);
    EXCEPINFO ei;
    memset(&ei, 0, sizeof(ei));
    UINT argErr = 0;
    hr = disp->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &result, &ei, &argErr);
    printf("Invoke(Open) hr=0x%08lX argErr=%u", (unsigned long)hr, argErr);
    if (hr == DISP_E_EXCEPTION) {
        printf(" excep=0x%08lX src='%ls' desc='%ls'", (unsigned long)ei.scode,
               ei.bstrSource ? ei.bstrSource : L"", ei.bstrDescription ? ei.bstrDescription : L"");
    }
    printf("\n");
    fflush(stdout);
    SysFreeString(b);
}

static void InvokeNoArg(IDispatch* disp, const wchar_t* method) {
    OLECHAR* name = (OLECHAR*)method;
    DISPID id = 0;
    HRESULT hr = disp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) { printf("%ls: GetIDsOfNames hr=0x%08lX\n", method, (unsigned long)hr); return; }
    DISPPARAMS dp = {nullptr, nullptr, 0, 0};
    VARIANT result; VariantInit(&result);
    EXCEPINFO ei; memset(&ei, 0, sizeof(ei));
    UINT argErr = 0;
    hr = disp->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &result, &ei, &argErr);
    printf("%ls: hr=0x%08lX\n", method, (unsigned long)hr);
    fflush(stdout);
}

int main(int argc, char** argv) {
    int proto = argc > 1 ? atoi(argv[1]) : 4;
    const wchar_t* port = argc > 2 ? L"COM20" : L"COM20";
    wchar_t portW[64];
    if (argc > 2) { MultiByteToWideChar(CP_ACP, 0, argv[2], -1, portW, 64); port = portW; }

    RealCoCreateInstance = (HRESULT (WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*))
        GetProcAddress(GetModuleHandleA("ole32.dll"), "CoCreateInstance");
    printf("real CoCreateInstance=%p\n", (void*)RealCoCreateInstance);

    const char* mods[] = {
        "C:\\Windows\\SysWOW64\\NextGen10.dll",
        "C:\\Program Files\\Common Files\\Motorola\\RDK\\ADK7_2_SP04_SR13\\PBT2.dll",
        "C:\\Windows\\SysWOW64\\PatComm10.dll",
        "C:\\Windows\\SysWOW64\\RadioSerial10.dll",
        "C:\\Windows\\SysWOW64\\PortManager10.dll",
        "C:\\Windows\\SysWOW64\\RadioCustStack10.dll",
        "C:\\Windows\\SysWOW64\\ByteData10.dll",
    };
    for (const char* m : mods) {
        HMODULE h = LoadLibraryA(m);
        printf("LoadLibrary %s -> %p (err %lu)\n", m, (void*)h, (unsigned long)GetLastError());
        if (h) PatchModule(h, m);
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx hr=0x%08lX\n", (unsigned long)hr);

    CLSID clsid;
    hr = CLSIDFromProgID(L"NextGen.RcmpBundler.1", &clsid);
    printf("CLSIDFromProgID hr=0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) return 2;

    IDispatch* disp = nullptr;
    hr = RealCoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, (void**)&disp);
    printf("Create RcmpBundler hr=0x%08lX disp=%p\n", (unsigned long)hr, (void*)disp);
    if (FAILED(hr) || !disp) return 3;

    InvokeOpen(disp, proto, port);
    InvokeNoArg(disp, L"EnterProgramMode");

    disp->Release();
    CoUninitialize();
    return 0;
}
