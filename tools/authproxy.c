#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char (__stdcall *FN)(void*, void*, void*);
static FN real_enc, real_dec;
static HMODULE real_mod;
static FILE* logf;
static CRITICAL_SECTION cs;

static void loghex(const char* tag, const void* p, int n) {
    if (!logf) return;
    const unsigned char* b = (const unsigned char*)p;
    fprintf(logf, "%s ptr=%p ", tag, p);
    for (int i = 0; i < n; i++) fprintf(logf, "%02x", b[i]);
    fprintf(logf, "\n");
    fflush(logf);
}

static void init(void) {
    if (real_mod) return;
    EnterCriticalSection(&cs);
    if (!real_mod) {
        real_mod = LoadLibraryA("C:\\Program Files\\Common Files\\Motorola\\RDK\\ADK7_2_SP04_SR13\\authAES.dll");
        if (real_mod) {
            real_enc = (FN)GetProcAddress(real_mod, "_encrypt_function@12");
            real_dec = (FN)GetProcAddress(real_mod, "_decrypt_function@12");
        }
        char path[MAX_PATH]; GetTempPathA(MAX_PATH, path); strcat(path, "authaes_proxy.log");
        logf = fopen(path, "a");
        if (logf) { fprintf(logf, "--- proxy init real=%p enc=%p dec=%p ---\n", real_mod, real_enc, real_dec); fflush(logf); }
    }
    LeaveCriticalSection(&cs);
}

__declspec(dllexport) unsigned char __stdcall encrypt_proxy(void* a, void* b, void* c) {
    init();
    loghex("ENC key", a, 16);
    loghex("ENC in ", b, 48);
    unsigned char r = real_enc ? real_enc(a, b, c) : 0;
    loghex("ENC out", c, 48);
    if (logf) { fprintf(logf, "ENC ret=%d\n", r); fflush(logf); }
    return r;
}

__declspec(dllexport) unsigned char __stdcall decrypt_proxy(void* a, void* b, void* c) {
    init();
    loghex("DEC key", a, 16);
    loghex("DEC in ", b, 48);
    unsigned char r = real_dec ? real_dec(a, b, c) : 0;
    loghex("DEC out", c, 48);
    if (logf) { fprintf(logf, "DEC ret=%d\n", r); fflush(logf); }
    return r;
}

__declspec(dllexport) void __stdcall shuming_proxy(void) { init(); }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { InitializeCriticalSection(&cs); DisableThreadLibraryCalls(h); }
    return TRUE;
}
