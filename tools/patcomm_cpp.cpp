#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <stdio.h>

static HRESULT Invoke(IDispatch* d, const wchar_t* name, VARIANT* args, int n, VARIANT* out) {
    OLECHAR* nm = (OLECHAR*)name;
    DISPID id = 0;
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &nm, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) { wprintf(L"%ls: GetIDsOfNames hr=0x%08lX\n", name, (unsigned long)hr); return hr; }
    DISPPARAMS dp{};
    dp.rgvarg = args;
    dp.cArgs = n;
    EXCEPINFO ei{};
    UINT ae = 0;
    hr = d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, out, &ei, &ae);
    wprintf(L"%ls: invoke hr=0x%08lX argErr=%u", name, (unsigned long)hr, ae);
    if (hr == DISP_E_EXCEPTION)
        wprintf(L" ex=0x%08lX src='%ls' desc='%ls'", (unsigned long)ei.scode,
                ei.bstrSource ? ei.bstrSource : L"", ei.bstrDescription ? ei.bstrDescription : L"");
    wprintf(L"\n");
    return hr;
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CLSID clsid;
    CLSIDFromProgID(L"PatComm10.PatCommProto10.1", &clsid);
    IDispatch* d = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, (void**)&d);
    printf("CoCreateInstance PatComm hr=0x%08lX d=%p\n", (unsigned long)hr, (void*)d);
    if (!d) return 1;

    // GetConnectedModemFromPortMgr(BSTR* pbstrPort, int* pLFlags)
    BSTR port = nullptr;
    LONG flags = 0;
    VARIANT a[2]; VariantInit(&a[0]); VariantInit(&a[1]);
    a[1].vt = VT_BSTR | VT_BYREF; a[1].pbstrVal = &port;
    a[0].vt = VT_I4 | VT_BYREF;   a[0].plVal = &flags;
    VARIANT out; VariantInit(&out);
    Invoke(d, L"GetConnectedModemFromPortMgr", a, 2, &out);
    wprintf(L"  port='%ls' flags=%ld\n", port ? port : L"(null)", flags);
    if (port) { wprintf(L"  using port '%ls'\n", port); }
    else { port = SysAllocString(L"COM20"); wprintf(L"  defaulting to COM20\n"); }

    // Open(int lProtocol, int lFlags, int lAdvFlags)
    int protos[] = {4, 1, 0, 2, 8};
    for (int i = 0; i < 5; i++) {
        VARIANT o[3]; VariantInit(&o[0]); VariantInit(&o[1]); VariantInit(&o[2]);
        o[2].vt = VT_I4; o[2].lVal = protos[i];
        o[1].vt = VT_I4; o[1].lVal = flags;
        o[0].vt = VT_I4; o[0].lVal = 0;
        VARIANT r; VariantInit(&r);
        wprintf(L"--- trying Open(%d,%ld,0)\n", protos[i], (long)flags);
        Invoke(d, L"Open", o, 3, &r);
    }

    // Close()
    VARIANT r; VariantInit(&r);
    VariantInit(&r);
    VARIANT none{};
    Invoke(d, L"Close", &none, 0, &r);

    SysFreeString(port);
    d->Release();
    CoUninitialize();
    return 0;
}
