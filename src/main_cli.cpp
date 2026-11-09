#include "adk.h"

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

using namespace opendepot;

static void PrintRecordCsv(const Codeplug& cp) {
    printf("id,stream,size,type,checksum,verified\n");
    for (const auto& r : cp.records) {
        printf("0x%08X,%d,%zu,0x%04X,0x%04X,%d\n", r.id, r.streamNum, r.raw.size(),
               r.type, r.checksum, r.verified ? 1 : 0);
    }
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t i = s.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : s.substr(0, i);
}

static std::wstring HelperPath() {
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\radio_helper.exe", L"\\..\\..\\..\\tools\\radio_helper.exe",
                            L"\\tools\\radio_helper.exe"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return d + L"\\radio_helper.exe";
}

// Bundled per-model base used for template-free codeplug creation.
static std::wstring ModelBasePath(const std::string& model) {
    std::wstring m(model.begin(), model.end());
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\resources\\models\\", L"\\..\\..\\..\\resources\\models\\",
                            L"\\..\\resources\\models\\"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r + m + L".cpg";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";
}

static bool WriteTagListFile(const Codeplug& cp, const std::wstring& path) {
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
    for (const auto& r : cp.records) {
        std::pair<uint16_t, uint16_t> p{static_cast<uint16_t>(r.type),
                                        static_cast<uint16_t>(r.id >> 16)};
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
    }
    std::sort(pairs.begin(), pairs.end());
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (auto& p : pairs) {
        uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first), uint8_t(p.second >> 8),
                        uint8_t(p.second)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool WriteTagListFileFiltered(const Codeplug& cp, const std::wstring& path, bool onlyFff) {
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
    for (const auto& r : cp.records) {
        const uint16_t part = static_cast<uint16_t>(r.type);
        const uint16_t tag = static_cast<uint16_t>(r.id >> 16);
        const bool isFff = (part == 0x0080 && tag == 0x0FFF);
        if (isFff != onlyFff) continue;
        std::pair<uint16_t, uint16_t> p{part, tag};
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
    }
    std::sort(pairs.begin(), pairs.end());
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (auto& p : pairs) {
        uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first), uint8_t(p.second >> 8),
                        uint8_t(p.second)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool WriteIshListFile(const Codeplug& cp, const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    auto emit = [&](const Record& r) {
        if (r.raw.size() < 8) return;
        const uint16_t tag = uint16_t(r.id >> 16);
        const uint16_t inst = uint16_t(r.id & 0xFFFF);
        const uint16_t part = r.type;
        const uint16_t len = uint16_t(r.raw.size() - 6);
        uint8_t h[8] = {uint8_t(tag >> 8), uint8_t(tag), uint8_t(inst >> 8), uint8_t(inst),
                        uint8_t(part >> 8), uint8_t(part), uint8_t(len >> 8), uint8_t(len)};
        fwrite(h, 1, 8, f);
        fwrite(r.raw.data() + 6, 1, len, f);
    };
    // Match the depot write order: identity records first, then everything else,
    // and the 0x0047 directory/reorg record last.
    const uint32_t firstIds[] = {0x10050000u, 0x10040000u, 0x10060000u};
    const Record* dir = nullptr;
    std::vector<const Record*> rest;
    for (const auto& r : cp.records) {
        if (r.id == 0x00470000u) { dir = &r; continue; }
        bool isFirst = false;
        for (uint32_t id : firstIds) if (r.id == id) { isFirst = true; break; }
        if (!isFirst) rest.push_back(&r);
    }
    for (uint32_t id : firstIds)
        for (const auto& r : cp.records)
            if (r.id == id) emit(r);
    for (const Record* r : rest) emit(*r);
    if (dir) emit(*dir);
    uint8_t term[2] = {0, 0};
    fwrite(term, 1, 2, f);
    fclose(f);
    return true;
}

static bool ApplyRecBuffer(const std::vector<uint8_t>& buf, const Codeplug& tpl, Codeplug& out,
                           std::string& err) {
    out = tpl;
    out.records.clear();
    size_t o = 0;
    while (o + 6 <= buf.size()) {
        Record r;
        r.id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
               (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
        uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
        if (o + 6 + len > buf.size()) break;
        r.raw.assign(buf.begin() + o, buf.begin() + o + 6 + len);
        r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw[r.raw.size() - 1]);
        r.verified = (Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8) == r.checksum);
        const uint16_t tg = uint16_t(r.id >> 16);
        r.type = (tg == 0x1004 || tg == 0x1005 || tg == 0x1006) ? 0x0081 : 0x0080;
        for (const auto& t : tpl.records)
            if ((t.id >> 16) == (r.id >> 16)) { r.type = t.type; break; }
        out.records.push_back(std::move(r));
        o += 6 + len;
    }
    if (out.records.empty()) { err = "no records in read data"; return false; }
    // keep only defined template records? No: the read is authoritative.
    return true;
}

static bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) { out.resize((size_t)n); fread(out.data(), 1, (size_t)n, f); }
    fclose(f);
    return true;
}

static bool RunHelper(const std::wstring& args, std::string& err, DWORD timeoutMs = 120000) {
    std::wstring cmd = L"\"" + HelperPath() + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start radio helper (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "radio operation timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "Radio helper failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

static std::wstring RawScriptPath() {
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\..\\..\\..\\tools\\radio_tcp.py", L"\\tools\\radio_tcp.py",
                            L"\\radio_tcp.py"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";
}

// Standalone flash: raw PPP/TCP transport, verified auth, full record set,
// 0x0047 directory, then reorg 0x01078000 and end 0x010601.
static bool RunRawWriter(const std::wstring& port, const std::wstring& cpg, std::string& err,
                         DWORD timeoutMs = 600000) {
    std::wstring script = RawScriptPath();
    if (script.empty()) {
        err = "radio_tcp.py not found";
        return false;
    }
    std::wstring cmd = L"python \"" + script + L"\" " + port + L" --write-cpg \"" + cpg +
                       L"\" --all";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start standalone writer (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "standalone write timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "standalone writer failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

// Native read over OpenDepot's own PPP/TCP transport (no vendor DLL).
static bool RunNativeReader(const std::wstring& port, const std::wstring& rec,
                            const std::wstring& reqs, std::string& err,
                            DWORD timeoutMs = 300000) {
    std::wstring script = RawScriptPath();
    if (script.empty()) {
        err = "radio_tcp.py not found";
        return false;
    }
    std::wstring cmd = L"python \"" + script + L"\" " + port + L" --native-read \"" + rec +
                       L"\" --reqs \"" + reqs + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start native reader (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "native read timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "native reader failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

static std::wstring TempPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    base += L"opendepot-radio";
    CreateDirectoryW(base.c_str(), nullptr);
    return base + L"\\" + name;
}


static bool WriteIdentityTagList(const std::wstring& path) {
    const uint16_t tags[3] = {0x1004, 0x1005, 0x1006};
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (uint16_t t : tags) {
        uint8_t b[4] = {0x00, 0x81, uint8_t(t >> 8), uint8_t(t)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool VerifyIdentityAfterWrite(const std::wstring& port, const Codeplug& cp,
                                     std::string& err) {
    std::wstring tags = TempPath(L"id-tags.bin");
    std::wstring rec = TempPath(L"id-read.rec");
    if (!WriteIdentityTagList(tags)) {
        err = "cannot stage identity tag list";
        return false;
    }
    std::string rerr;
    std::wstring args = L"--read " + port + L" \"" + tags + L"\" \"" + rec + L"\"";
    if (!RunHelper(args, rerr, 60000)) {
        DeleteFileW(tags.c_str());
        DeleteFileW(rec.c_str());
        err = "post-write identity read failed: " + rerr;
        return false;
    }
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(rec, buf)) {
        DeleteFileW(tags.c_str());
        DeleteFileW(rec.c_str());
        err = "post-write read produced no data";
        return false;
    }
    DeleteFileW(tags.c_str());
    DeleteFileW(rec.c_str());
    bool found = false;
    size_t o = 0;
    while (o + 6 <= buf.size()) {
        uint32_t id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
                      (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
        uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
        if (o + 6 + len > buf.size()) break;
        if (id == 0x10040000) {
            std::string payload(reinterpret_cast<const char*>(buf.data() + o + 6), len);
            found = payload.find(cp.header.serial) != std::string::npos &&
                    payload.find(cp.header.model) != std::string::npos;
        }
        o += 6 + len;
    }
    if (!found) {
        err = "identity readback does not contain the written serial/model";
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 2) {
        printf("WARNING: experimental; a write can BRICK your radio. Keep a verified backup.\n");
        printf("         Only tested on one XTS 2500. Firmware flashing is not supported.\n");
        printf("usage:\n");
        printf("  opendepot-cli --verify <in.cpg>\n");
        printf("  opendepot-cli --dump <in.cpg>\n");
        printf("  opendepot-cli --records <in.cpg>\n");
        printf("  opendepot-cli --clone <in.cpg> <out.cpg>\n");
        printf("  opendepot-cli --models\n");
        printf("  opendepot-cli --zones <in.cpg>\n");
        printf("  opendepot-cli --set-zone-name <in.cpg> <out.cpg> <zoneIndex> <name>\n");
        printf("  opendepot-cli --set-channel-name <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <name>\n");
        printf("  opendepot-cli --set-channel-freq <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <rxMHz> <txMHz>\n");
        printf("  opendepot-cli --set-identity <in.cpg> <out.cpg> <serial> <model>\n");
        printf("  opendepot-cli --flashcode <in.cpg> [radio|platform]\n");
        printf("  opendepot-cli --set-flashcode <in.cpg> <out.cpg> <flashcode>\n");
        printf("  opendepot-cli --flashcode-options <radio|platform>\n");
        printf("  opendepot-cli --build-flashcode <radio|platform> <base> <code,code,...>\n");
        printf("  opendepot-cli --create <template.cpg> <out.cpg> <serial> <model> <flashcode>\n");
        printf("  opendepot-cli --create-new <out.cpg> <model> <serial> [flashcode]\n");
        printf("  opendepot-cli --conventional <in.cpg> <out.cpg>\n");
        printf("  opendepot-cli --conventional-ref <ref.cpg> <src.cpg> <out.cpg>\n");
        printf("  opendepot-cli --create-conventional <template.cpg> <out.cpg> <serial> <model>\n");
        printf("  opendepot-cli --rec <in.cpg> <out.rec>\n");
        printf("  opendepot-cli --from-rec <template.cpg> <in.rec> <out.cpg>\n");
        printf("  opendepot-cli --taglist <in.cpg> <out.bin>\n");
        printf("  opendepot-cli --ishlist <in.cpg> <out.bin>\n");
        printf("  opendepot-cli --read-radio <port> <out.cpg>\n");
        printf("  opendepot-cli --read-radio <port> <template.cpg> <out.cpg>\n");
        printf("  opendepot-cli --check-write <in.cpg>\n");
        printf("  opendepot-cli --write-radio <port> <in.cpg>\n");
        printf("  opendepot-cli --erase-radio <port> [--yes-erase]\n");
        printf("  opendepot-cli --replace-radio <port> <old.cpg> <new.cpg>\n");
        return 1;
    }
    const std::wstring cmd = argv[1];
    if (cmd == L"--verify" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        printf("%s", VerifyCodeplug(cp).c_str());
        size_t bad = 0;
        for (auto& r : cp.records) if (!r.verified) ++bad;
        return bad ? 3 : 0;
    }
    if (cmd == L"--dump" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        printf("%s", DumpCodeplug(cp).c_str());
        return 0;
    }
    if (cmd == L"--records" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        PrintRecordCsv(cp);
        return 0;
    }
    if (cmd == L"--clone" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--models" && argc >= 2) {
        printf("Bands:  ");
        for (const auto& s : BandOptions()) printf("%s  ", s.c_str());
        printf("\nRadios: ");
        for (const auto& s : RadioOptions()) printf("%s  ", s.c_str());
        printf("\nTypes:  ");
        for (const auto& s : TypeOptions()) printf("%s  ", s.c_str());
        printf("\nModels:\n");
        for (const auto& m : ModelCatalog())
            printf("  %-8s %-10s %-10s %s\n", m.band, m.radio, m.type, m.model);
        printf("\nPackages (XTS5000):\n");
        for (const auto& s : PackagesForModel("H46UCF9PW6BN")) printf("  %s\n", s.c_str());
        printf("Add-ons (XTS5000):\n");
        for (const auto& s : AddonsForModel("H46UCF9PW6BN")) printf("  %s\n", s.c_str());
        return 0;
    }
    if (cmd == L"--create" && argc >= 7) {
        char serial[64] = {}, model[64] = {}, flash[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[6], -1, flash, sizeof(flash), nullptr, nullptr);
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) {
            printf("LOAD FAILED: %s\n", tpl.error.c_str());
            return 2;
        }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = flash;
        Codeplug created;
        std::string err;
        if (!CreateCodeplug(tpl, opt, created, err)) {
            printf("CREATE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[3], created, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls  model=%s serial=%s flashcode=%s\n", argv[3], model, serial,
               GetFlashcode(created).c_str());
        return 0;
    }
    if (cmd == L"--create-new" && argc >= 5) {
        // Template-free: --create-new <out.cpg> <model> <serial> [flashcode]
        char model[64] = {}, serial[64] = {}, flash[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, model, sizeof(model), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        if (argc >= 6)
            WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, flash, sizeof(flash), nullptr, nullptr);
        Codeplug tpl;
        std::string berr;
        if (!BuiltinTemplate(model, tpl, berr)) {
            printf("NO BUILT-IN SCHEMA for model %s: %s\n", model, berr.c_str());
            return 2;
        }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = flash;
        Codeplug created;
        std::string err;
        if (!CreateCodeplug(tpl, opt, created, err)) {
            printf("CREATE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[2], created, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls  model=%s serial=%s flashcode=%s\n", argv[2], model, serial,
               GetFlashcode(created).c_str());
        return 0;
    }
    if (cmd == L"--rec" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        for (const auto& r : cp.records) fwrite(r.raw.data(), 1, r.raw.size(), f);
        fclose(f);
        printf("wrote %ls (%zu records)\n", argv[3], cp.records.size());
        return 0;
    }
    if (cmd == L"--taglist" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::vector<std::pair<uint16_t, uint16_t>> pairs;
        for (const auto& r : cp.records) {
            std::pair<uint16_t, uint16_t> p{static_cast<uint16_t>(r.type),
                                            static_cast<uint16_t>(r.id >> 16)};
            if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
        }
        std::sort(pairs.begin(), pairs.end());
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        for (auto& p : pairs) {
            uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first),
                            uint8_t(p.second >> 8), uint8_t(p.second)};
            fwrite(b, 1, 4, f);
        }
        fclose(f);
        printf("wrote %ls (%zu partition/tag pairs)\n", argv[3], pairs.size());
        return 0;
    }
    if (cmd == L"--ishlist" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        size_t total = 0;
        for (const auto& r : cp.records) {
            if (r.raw.size() < 8) continue;
            const uint16_t tag = uint16_t(r.id >> 16);
            const uint16_t inst = uint16_t(r.id & 0xFFFF);
            const uint16_t part = r.type;
            const uint16_t len = uint16_t(r.raw.size() - 6);
            uint8_t h[8] = {uint8_t(tag >> 8), uint8_t(tag), uint8_t(inst >> 8), uint8_t(inst),
                            uint8_t(part >> 8), uint8_t(part), uint8_t(len >> 8), uint8_t(len)};
            fwrite(h, 1, 8, f);
            fwrite(r.raw.data() + 6, 1, len, f);
            total += 8 + len;
        }
        uint8_t term[2] = {0, 0};
        fwrite(term, 1, 2, f);
        fclose(f);
        printf("wrote %ls (%zu records, %zu bytes)\n", argv[3], cp.records.size(), total + 2);
        return 0;
    }
    if (cmd == L"--from-rec" && argc >= 5) {
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) { printf("LOAD FAILED: %s\n", tpl.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"rb");
        if (!f) { printf("open failed\n"); return 2; }
        std::vector<uint8_t> buf;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) { buf.resize((size_t)n); fread(buf.data(), 1, (size_t)n, f); }
        fclose(f);
        Codeplug out = tpl;
        out.records.clear();
        size_t o = 0;
        while (o + 6 <= buf.size()) {
            Record r;
            r.id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
                   (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
            uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
            if (o + 6 + len > buf.size()) break;
            r.raw.assign(buf.begin() + o, buf.begin() + o + 6 + len);
            r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw[r.raw.size() - 1]);
            r.verified = (Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8) == r.checksum);
            r.type = 0x0080;
            for (const auto& t : tpl.records)
                if ((t.id >> 16) == (r.id >> 16)) { r.type = t.type; break; }
            out.records.push_back(std::move(r));
            o += 6 + len;
        }
        std::string err;
        if (!SaveCodeplug(argv[4], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        size_t good = 0;
        for (auto& r : out.records) if (r.verified) ++good;
        printf("wrote %ls (%zu records, %zu CRC-ok)\n", argv[4], out.records.size(), good);
        return 0;
    }
    if (cmd == L"--read-radio" && argc >= 4) {
        // Two forms:
        //   --read-radio <port> <template.cpg> <out.cpg>
        //   --read-radio <port> <out.cpg>            (read any radio, no template)
        const bool useTemplate = (argc >= 5);
        Codeplug tpl;
        std::wstring outPath;
        if (useTemplate) {
            if (!LoadCodeplug(argv[3], tpl)) { printf("LOAD FAILED: %s\n", tpl.error.c_str()); return 2; }
            outPath = argv[4];
        } else {
            // Header/type basis from a built-in schema; records come from the radio.
            std::string berr;
            std::string first = BuiltinFirstModel();
            if (!first.empty()) BuiltinTemplate(first, tpl, berr);
            outPath = argv[3];
        }
        // Native read: compile the request list from the built-in schemas.
        std::vector<ReadRequest> reqs;
        BuiltinReadRequests(reqs);
        std::wstring reqsPath = TempPath(L"read.reqs");
        FILE* rf = _wfopen(reqsPath.c_str(), L"wb");
        if (!rf) { printf("reqs write failed\n"); return 2; }
        for (auto& r : reqs) {
            uint8_t b[8] = {uint8_t(r.part >> 8), uint8_t(r.part), uint8_t(r.tag >> 8),
                            uint8_t(r.tag),      uint8_t(r.inst >> 8), uint8_t(r.inst),
                            uint8_t(r.len >> 8), uint8_t(r.len)};
            fwrite(b, 1, 8, rf);
        }
        fclose(rf);
        std::wstring rec = TempPath(L"read.rec");
        std::string err;
        if (!RunNativeReader(std::wstring(argv[2]), rec, reqsPath, err)) {
            printf("READ FAILED: %s\n", err.c_str());
            DeleteFileW(reqsPath.c_str());
            return 2;
        }
        std::vector<uint8_t> buf;
        if (!ReadWholeFile(rec, buf)) { printf("read output missing\n"); return 2; }
        Codeplug out;
        if (!ApplyRecBuffer(buf, tpl, out, err)) { printf("READ PARSE FAILED: %s\n", err.c_str()); return 2; }
        if (!useTemplate) {
            std::string s, m;
            if (ReadIdentityFromCodeplug(out, s, m)) UpdateIdentity(out, s, m);
        }
        if (!SaveCodeplug(outPath, out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        size_t good = 0;
        for (auto& r : out.records) if (r.verified) ++good;
        DeleteFileW(reqsPath.c_str());
        DeleteFileW(rec.c_str());
        printf("read %zu records (%zu CRC-ok) -> %ls\n", out.records.size(), good, outPath.c_str());
        return good == out.records.size() ? 0 : 3;
    }
    if (cmd == L"--erase-radio" && argc >= 3) {
        bool confirmed = false;
        for (int i = 1; i < argc; ++i)
            if (std::wstring(argv[i]) == L"--yes-erase") confirmed = true;
        if (!confirmed) {
            printf("ERASE REFUSED: erasing deletes the identity block and causes FAIL 01/92.\n");
            printf("Re-run with --yes-erase only if you have a verified backup.\n");
            return 2;
        }
        std::string err;
        std::wstring args = L"--erase " + std::wstring(argv[2]);
        if (!RunHelper(args, err, 120000)) { printf("ERASE FAILED: %s\n", err.c_str()); return 2; }
        printf("erased codeplug on %ls\n", argv[2]);
        return 0;
    }
    if (cmd == L"--replace-radio" && argc >= 5) {
        Codeplug oldcp, newcp;
        if (!LoadCodeplug(argv[3], oldcp)) { printf("LOAD OLD FAILED: %s\n", oldcp.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[4], newcp)) { printf("LOAD NEW FAILED: %s\n", newcp.error.c_str()); return 2; }
        std::string verr;
        if (!ValidateRadioCodeplug(newcp, verr)) {
            printf("REPLACE REFUSED: %s\n", verr.c_str());
            return 2;
        }
        std::wstring oldrec = TempPath(L"old.rec");
        FILE* f = _wfopen(oldrec.c_str(), L"wb");
        if (!f) { printf("temp open failed\n"); return 2; }
        for (const auto& r : oldcp.records) fwrite(r.raw.data(), 1, r.raw.size(), f);
        fclose(f);
        std::wstring ish = TempPath(L"new.ish");
        if (!WriteIshListFile(newcp, ish)) { printf("ishlist write failed\n"); DeleteFileW(oldrec.c_str()); return 2; }
        std::string err;
        std::wstring args = L"--replace " + std::wstring(argv[2]) + L" \"" + oldrec + L"\" \"" + ish + L"\"";
        bool ok = RunHelper(args, err, 300000);
        DeleteFileW(oldrec.c_str());
        DeleteFileW(ish.c_str());
        if (!ok) { printf("REPLACE FAILED: %s\n", err.c_str()); return 2; }
        printf("replaced radio codeplug on %ls (old=%zu new=%zu records)\n", argv[2], oldcp.records.size(), newcp.records.size());
        return 0;
    }
    if (cmd == L"--check-write" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string err;
        if (!ValidateRadioCodeplug(cp, err)) {
            printf("REFUSED: %s\n", err.c_str());
            return 3;
        }
        printf("OK: safe to write (identity %s/%s, %zu records)\n", cp.header.serial.c_str(),
               cp.header.model.c_str(), cp.records.size());
        return 0;
    }
    if (cmd == L"--write-radio" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[3], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string verr;
        if (!ValidateRadioCodeplug(cp, verr)) {
            printf("WRITE REFUSED: %s\n", verr.c_str());
            return 2;
        }
        std::string err;
        if (!RunRawWriter(std::wstring(argv[2]), std::wstring(argv[3]), err)) {
            printf("WRITE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %zu records to the radio on %ls (identity %s/%s)\n", cp.records.size(),
               argv[2], cp.header.serial.c_str(), cp.header.model.c_str());
        std::string verr2;
        Sleep(8000);
        if (!VerifyIdentityAfterWrite(std::wstring(argv[2]), cp, verr2)) {
            Sleep(4000);
            if (!VerifyIdentityAfterWrite(std::wstring(argv[2]), cp, verr2)) {
                printf("WRITE FAILED: post-write verification failed: %s\n", verr2.c_str());
                return 3;
            }
            printf("verified identity on the radio\n");
        } else {
            printf("verified identity on the radio\n");
        }
        return 0;
    }
    if (cmd == L"--conventional" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string err;
        if (!MakeConventional(cp, err)) { printf("CONVENTIONAL FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records)\n", argv[3], cp.records.size());
        return 0;
    }
    if (cmd == L"--conventional-ref" && argc >= 5) {
        Codeplug ref, src, out;
        if (!LoadCodeplug(argv[2], ref)) { printf("LOAD REF FAILED: %s\n", ref.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[3], src)) { printf("LOAD SRC FAILED: %s\n", src.error.c_str()); return 2; }
        std::string err;
        if (!MakeConventionalFromRef(out, ref, src, err)) { printf("CONVENTIONAL REF FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[4], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records)\n", argv[4], out.records.size());
        return 0;
    }
    if (cmd == L"--create-conventional" && argc >= 6) {
        char serial[64] = {}, model[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) { printf("LOAD FAILED: %s\n", tpl.error.c_str()); return 2; }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = "600022150000";
        Codeplug out;
        std::string err;
        if (!CreateCodeplug(tpl, opt, out, err)) { printf("CREATE FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[3], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records) model=%s serial=%s flashcode=%s\n", argv[3],
               out.records.size(), model, serial, GetFlashcode(out).c_str());
        return 0;
    }
    if (cmd == L"--zones" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::vector<ZoneView> zones;
        if (!ParseZones(cp, zones)) {
            printf("no zone table\n");
            return 2;
        }
        for (size_t z = 0; z < zones.size(); ++z) {
            printf("Zone %zu: %s  (%zu channels)\n", z + 1, zones[z].name.c_str(), zones[z].channels.size());
            for (size_t c = 0; c < zones[z].channels.size(); ++c) {
                const auto& ch = zones[z].channels[c];
                printf("   %2zu. %-16s  rx=%.5f tx=%.5f  flags=0x%02X\n", c + 1,
                       ch.name.c_str(), WordToMHz(ch.rxWord), WordToMHz(ch.txWord), ch.flags);
            }
        }
        return 0;
    }
    if (cmd == L"--set-zone-name" && argc >= 6) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, name, sizeof(name), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetZoneName(cp, _wtoi(argv[4]), name)) { printf("zone set failed\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--set-channel-name" && argc >= 7) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[6], -1, name, sizeof(name), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetChannelName(cp, _wtoi(argv[4]), _wtoi(argv[5]), name)) { printf("channel set failed\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--set-channel-freq" && argc >= 8) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetChannelFreq(cp, _wtoi(argv[4]), _wtoi(argv[5]), _wtof(argv[6]), _wtof(argv[7]))) {
            printf("channel set failed\n");
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--flashcode-options" && argc >= 3) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, name, sizeof(name), nullptr, nullptr);
        const FlashcodePlatform* p = FindFlashcodePlatform(name);
        if (!p) p = FindFlashcodePlatform(PlatformForRadio(name));
        if (!p) { printf("no platform table for %s\n", name); return 2; }
        printf("platform %s (%zu options)\n", p->name.c_str(), p->options.size());
        for (const auto& o : p->options)
            printf("  d%db%d  %-5s %s\n", o.nibble, o.bit, o.code.c_str(), o.label.c_str());
        return 0;
    }
    if (cmd == L"--build-flashcode" && argc >= 5) {
        char name[64] = {}, base[64] = {}, codes[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, name, sizeof(name), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, base, sizeof(base), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, codes, sizeof(codes), nullptr, nullptr);
        const FlashcodePlatform* p = FindFlashcodePlatform(name);
        if (!p) p = FindFlashcodePlatform(PlatformForRadio(name));
        if (!p) { printf("no platform table for %s\n", name); return 2; }
        std::vector<std::string> sel;
        std::string cur;
        for (const char* c = codes;; ++c) {
            if (*c == ',' || *c == 0) {
                if (!cur.empty()) { sel.push_back(cur); cur.clear(); }
                if (*c == 0) break;
            } else {
                cur.push_back(*c);
            }
        }
        std::string fc = BuildFlashcode(p->name, base, sel);
        printf("%s\n", fc.empty() ? "FAILED" : fc.c_str());
        return fc.empty() ? 2 : 0;
    }
    if (cmd == L"--flashcode" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string platform = "XTS2500";
        if (argc >= 4) {
            char b[64] = {};
            WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, b, sizeof(b), nullptr, nullptr);
            platform = b;
        }
        std::string platName =
            FindFlashcodePlatform(platform) ? platform : PlatformForRadio(platform);
        std::string fc = GetFlashcode(cp);
        printf("FLASHcode: %s\n", fc.c_str());
        const FlashcodePlatform* p = platName.empty() ? nullptr : FindFlashcodePlatform(platName);
        printf("platform: %s\n", p ? p->name.c_str() : "(unknown)");
        for (const auto& f : DecodeFlashcode(platName, fc)) printf("  - %s\n", f.c_str());
        return 0;
    }
    if (cmd == L"--set-flashcode" && argc >= 5) {
        char fc[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, fc, sizeof(fc), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string canonical, err;
        if (!SetFlashcodeMap(cp, fc, canonical, err)) {
            printf("FLASHCODE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls with FLASHcode %s\n", argv[3], canonical.c_str());
        return 0;
    }
    if (cmd == L"--set-identity" && argc >= 6) {
        char serial[64] = {}, model[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        if (!UpdateIdentity(cp, serial, model)) {
            printf("IDENTITY REJECTED (new values must fit existing fields)\n");
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    printf("unknown or incomplete command\n");
    return 1;
}





