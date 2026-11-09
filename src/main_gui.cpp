#include "adk.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <set>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

using namespace opendepot;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;
static bool g_DeviceLost = false;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct App {
    Codeplug cp;
    bool loaded = false;
    bool identityDirty = false;
    std::string status = "Open a .cpg file to begin.";
    int selected = -1;
    char serial[64] = {};
    char model[64] = {};
    char pathUtf8[512] = {};
    std::string saveError;

    bool showCreate = false;
    char createTemplate[512] = {};
    int createBand = 2;
    int createRadio = 1;
    int createType = 2;
    char createModel[64] = "H46UCF9PW6BN";
    char createSerial[64] = {};
    char createFlash[64] = {};
    char fcBase[32] = {};
    std::string createPlatform;
    std::vector<uint8_t> createPkg;
    std::vector<uint8_t> createAdd;
    std::string createError;

    char radioPort[16] = "COM20";
    bool radioBusy = false;
    bool showWriteConfirm = false;
    std::string radioStatus;
};

static App g_app;
static std::atomic<DWORD> g_radioCliPid{0};

static void SyncIdentityBuffers() {
    snprintf(g_app.serial, sizeof(g_app.serial), "%s", g_app.cp.header.serial.c_str());
    snprintf(g_app.model, sizeof(g_app.model), "%s", g_app.cp.header.model.c_str());
}

static void LoadPath(const std::wstring& path) {
    Codeplug cp;
    if (!LoadCodeplug(path, cp)) {
        g_app.status = "Load failed: " + cp.error;
        g_app.saveError = cp.error;
        return;
    }
    g_app.cp = std::move(cp);
    g_app.loaded = true;
    g_app.selected = -1;
    SyncIdentityBuffers();
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, g_app.pathUtf8, sizeof(g_app.pathUtf8), nullptr, nullptr);
    size_t good = 0, bad = 0;
    for (auto& r : g_app.cp.records) (r.verified ? good : bad)++;
    char buf[256];
    snprintf(buf, sizeof(buf), "Loaded %zu records (%zu verified, %zu bad).", g_app.cp.records.size(), good, bad);
    g_app.status = buf;
    g_app.saveError.clear();
}

static bool OpenDialog(std::wstring& out) {
    wchar_t file[1024] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"ASTRO 25 codeplug (*.cpg)\0*.cpg\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = file;
    return true;
}

static bool SaveDialog(std::wstring& out) {
    wchar_t file[1024] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"ASTRO 25 codeplug (*.cpg)\0*.cpg\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrDefExt = L"cpg";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    out = file;
    return true;
}

static void SaveTo(const std::wstring& path) {
    std::string err;
    if (!SaveCodeplug(path, g_app.cp, err)) {
        g_app.status = "Save failed: " + err;
        g_app.saveError = err;
        return;
    }
    char narrow[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    g_app.status = std::string("Saved ") + narrow;
    g_app.saveError.clear();
}

static void DrawHex(const std::vector<uint8_t>& d) {
    ImGui::BeginChild("hex", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    char line[160];
    for (size_t off = 0; off < d.size(); off += 16) {
        int p = snprintf(line, sizeof(line), "%08zX  ", off);
        for (size_t i = 0; i < 16; ++i) {
            if (off + i < d.size()) p += snprintf(line + p, sizeof(line) - p, "%02X ", d[off + i]);
            else p += snprintf(line + p, sizeof(line) - p, "   ");
        }
        p += snprintf(line + p, sizeof(line) - p, " |");
        for (size_t i = 0; i < 16 && off + i < d.size(); ++i) {
            uint8_t b = d[off + i];
            line[p++] = (b >= 32 && b < 127) ? char(b) : '.';
        }
        line[p++] = '|';
        line[p] = 0;
        ImGui::TextUnformatted(line);
    }
    ImGui::EndChild();
}

static int HexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static const FlashcodePlatform* CreatePlatform() {
    if (g_app.createRadio < 0 || g_app.createRadio >= (int)RadioOptions().size()) return nullptr;
    return FindFlashcodePlatform(PlatformForRadio(RadioOptions()[g_app.createRadio]));
}

static void SyncCreateOptsFromFlash(const FlashcodePlatform* plat) {
    g_app.createAdd.assign(plat ? plat->options.size() : 0, 0);
    if (!plat) return;
    std::string hex = FlashcodeToNibbles(g_app.createFlash);
    if (hex.size() != 12) hex.assign(12, '0');
    for (size_t i = 0; i < plat->options.size(); ++i) {
        const auto& o = plat->options[i];
        int v = HexNib(hex[o.nibble]);
        g_app.createAdd[i] = (v >> o.bit) & 1;
    }
}

static std::vector<std::string> SelectedCreateCodes(const FlashcodePlatform* plat) {
    std::vector<std::string> sel;
    if (!plat) return sel;
    for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
        if (g_app.createAdd[i]) sel.push_back(plat->options[i].code);
    return sel;
}

static void ResolveCreateRequires(const FlashcodePlatform* plat) {
    if (!plat) return;
    std::set<std::string> sel;
    for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
        if (g_app.createAdd[i]) sel.insert(plat->options[i].code);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& r : plat->requires) {
            if (!sel.count(r.key)) continue;
            for (const auto& c : r.codes)
                if (sel.insert(c).second) changed = true;
        }
    }
    for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
        g_app.createAdd[i] = sel.count(plat->options[i].code) ? 1 : 0;
}

static void RebuildCreateFlash(const FlashcodePlatform* plat) {
    if (!plat) return;
    ResolveCreateRequires(plat);
    std::string fc = BuildFlashcode(plat->name, g_app.fcBase, SelectedCreateCodes(plat));
    if (!fc.empty()) snprintf(g_app.createFlash, sizeof(g_app.createFlash), "%s", fc.c_str());
}

static const char* PackageOptionCode(const std::string& name) {
    if (name == "3600 Analog Trunk Data") return "Q443";
    if (name == "3600 Digital Trunk Data") return "Q575";
    if (name == "9600 Digital Trunk Data") return "Q574";
    if (name == "P25 MDC Conv Data") return "Q811";
    if (name == "Analog Cnv Data") return "Q241";
    return nullptr;
}

static void ApplyPackage(const FlashcodePlatform* plat, const char* code, bool on) {
    if (!plat || !code) return;
    bool applied = false;
    for (const auto& p : plat->packages) {
        if (p.key != code) continue;
        for (const auto& c : p.codes)
            for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
                if (plat->options[i].code == c) {
                    g_app.createAdd[i] = on ? 1 : 0;
                    applied = true;
                }
    }
    if (!applied)
        for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
            if (plat->options[i].code == code) g_app.createAdd[i] = on ? 1 : 0;
}

static void OpenCreateDialog() {
    g_app.showCreate = true;
    g_app.createError.clear();
    if (g_app.loaded) {
        snprintf(g_app.createSerial, sizeof(g_app.createSerial), "%s", g_app.cp.header.serial.c_str());
        snprintf(g_app.createModel, sizeof(g_app.createModel), "%s", g_app.cp.header.model.c_str());
        std::string fc = GetFlashcode(g_app.cp);
        snprintf(g_app.createFlash, sizeof(g_app.createFlash), "%s", fc.c_str());
        snprintf(g_app.fcBase, sizeof(g_app.fcBase), "%s", fc.c_str());
        snprintf(g_app.createTemplate, sizeof(g_app.createTemplate), "%s", g_app.pathUtf8);
    } else {
        g_app.createFlash[0] = 0;
        g_app.fcBase[0] = 0;
    }
    const auto pkgs = PackagesForModel(g_app.createModel);
    g_app.createPkg.assign(pkgs.size(), 0);
    const FlashcodePlatform* plat = CreatePlatform();
    SyncCreateOptsFromFlash(plat);
    g_app.createPlatform = plat ? plat->name : "";
}

static void DrawCreateDialog() {
    if (g_app.showCreate) {
        ImGui::OpenPopup("Create Codeplug");
        g_app.showCreate = false;
    }
    if (!ImGui::BeginPopupModal("Create Codeplug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted("Template .cpg (cloned, then identity/FLASHcode applied)");
    ImGui::SetNextItemWidth(520);
    ImGui::InputText("##template", g_app.createTemplate, sizeof(g_app.createTemplate));
    ImGui::SameLine();
    if (ImGui::Button("Use loaded")) {
        if (g_app.loaded)
            snprintf(g_app.createTemplate, sizeof(g_app.createTemplate), "%s", g_app.pathUtf8);
    }

    ImGui::Separator();
    auto combo = [](const char* label, const std::vector<std::string>& items, int& sel) {
        ImGui::SetNextItemWidth(240);
        if (ImGui::BeginCombo(label, (sel >= 0 && sel < (int)items.size()) ? items[sel].c_str() : "")) {
            for (int i = 0; i < (int)items.size(); ++i)
                if (ImGui::Selectable(items[i].c_str(), sel == i)) sel = i;
            ImGui::EndCombo();
        }
    };
    combo("Band", BandOptions(), g_app.createBand);
    ImGui::SameLine();
    combo("Radio", RadioOptions(), g_app.createRadio);
    ImGui::SameLine();
    combo("Type", TypeOptions(), g_app.createType);

    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Model", g_app.createModel, sizeof(g_app.createModel));
    ImGui::SameLine();
    if (ImGui::BeginCombo("##knownmodels", "Known")) {
        for (const auto& m : ModelCatalog())
            if (ImGui::Selectable(m.model)) {
                snprintf(g_app.createModel, sizeof(g_app.createModel), "%s", m.model);
                for (int i = 0; i < (int)BandOptions().size(); ++i)
                    if (BandOptions()[i] == m.band) g_app.createBand = i;
                for (int i = 0; i < (int)RadioOptions().size(); ++i)
                    if (RadioOptions()[i] == m.radio) g_app.createRadio = i;
                for (int i = 0; i < (int)TypeOptions().size(); ++i)
                    if (TypeOptions()[i] == m.type) g_app.createType = i;
            }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Serial", g_app.createSerial, sizeof(g_app.createSerial));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("FLASHcode", g_app.createFlash, sizeof(g_app.createFlash));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Base FLASHcode", g_app.fcBase, sizeof(g_app.fcBase));

    const FlashcodePlatform* plat = CreatePlatform();
    std::string pname = plat ? plat->name : "";
    if (pname != g_app.createPlatform) {
        g_app.createPlatform = pname;
        snprintf(g_app.createFlash, sizeof(g_app.createFlash), "%s", g_app.fcBase);
        SyncCreateOptsFromFlash(plat);
    }

    ImGui::Separator();
    ImGui::Text("Features (%s)", plat ? plat->name.c_str() : "no platform table");
    ImGui::TextUnformatted("Packages");
    const auto pkgs = PackagesForModel(g_app.createModel);
    if ((int)g_app.createPkg.size() != (int)pkgs.size()) g_app.createPkg.assign(pkgs.size(), 0);
    for (int i = 0; i < (int)pkgs.size(); ++i) {
        bool v = g_app.createPkg[i] != 0;
        if (ImGui::Checkbox(pkgs[i].c_str(), &v)) {
            g_app.createPkg[i] = v ? 1 : 0;
            ApplyPackage(plat, PackageOptionCode(pkgs[i]), v);
            RebuildCreateFlash(plat);
        }
        if ((i % 2) != 1) ImGui::SameLine();
    }

    ImGui::TextUnformatted("Add-on options");
    if (plat) {
        if (g_app.createAdd.size() != plat->options.size()) SyncCreateOptsFromFlash(plat);
        for (size_t i = 0; i < plat->options.size(); ++i) {
            const auto& o = plat->options[i];
            bool v = i < g_app.createAdd.size() && g_app.createAdd[i];
            std::string label = o.code + "  " + o.label;
            if (ImGui::Checkbox(label.c_str(), &v)) {
                if (i < g_app.createAdd.size()) g_app.createAdd[i] = v ? 1 : 0;
                RebuildCreateFlash(plat);
            }
            if ((i % 2) != 1) ImGui::SameLine();
        }
    } else {
        ImGui::TextUnformatted("(no feature table for this radio type)");
    }

    ImGui::Separator();
    std::string canon = CanonicalFlashcode(g_app.createFlash);
    if (canon.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "FLASHcode needs 12 hex nibbles");
    else
        ImGui::Text("Generated FLASHcode: %s", canon.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Use as base")) {
        snprintf(g_app.fcBase, sizeof(g_app.fcBase), "%s", g_app.createFlash);
        SyncCreateOptsFromFlash(plat);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to base")) {
        snprintf(g_app.createFlash, sizeof(g_app.createFlash), "%s", g_app.fcBase);
        SyncCreateOptsFromFlash(plat);
    }

    if (plat) {
        std::string selSummary;
        for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
            if (g_app.createAdd[i]) {
                if (!selSummary.empty()) selSummary += ", ";
                selSummary += plat->options[i].code;
            }
        if (!selSummary.empty()) ImGui::TextWrapped("Enabled: %s", selSummary.c_str());
    }

    if (!g_app.createError.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_app.createError.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create")) {
        g_app.createError.clear();
        Codeplug templ;
        bool haveTpl = false;
        if (g_app.createTemplate[0]) {
            wchar_t wpath[1024] = {};
            MultiByteToWideChar(CP_UTF8, 0, g_app.createTemplate, -1, wpath, 1024);
            haveTpl = LoadCodeplug(wpath, templ);
            if (!haveTpl) g_app.createError = "Template load failed: " + templ.error;
        } else if (g_app.loaded) {
            templ = g_app.cp;
            haveTpl = true;
        } else {
            g_app.createError = "Choose a template .cpg";
        }
        if (haveTpl) {
            CreateOptions opt;
            opt.band = BandOptions()[g_app.createBand];
            opt.radio = RadioOptions()[g_app.createRadio];
            opt.type = TypeOptions()[g_app.createType];
            opt.model = g_app.createModel;
            opt.serial = g_app.createSerial;
            opt.flashcode = g_app.createFlash;
            for (int i = 0; i < (int)pkgs.size(); ++i)
                if (i < (int)g_app.createPkg.size() && g_app.createPkg[i]) opt.packages.push_back(pkgs[i]);
            if (plat)
                for (size_t i = 0; i < plat->options.size() && i < g_app.createAdd.size(); ++i)
                    if (g_app.createAdd[i]) opt.addons.push_back(plat->options[i].code);
            Codeplug created;
            std::string err;
            if (!CreateCodeplug(templ, opt, created, err)) {
                g_app.createError = err;
            } else {
                g_app.cp = std::move(created);
                g_app.loaded = true;
                g_app.selected = -1;
                SyncIdentityBuffers();
                g_app.status = "Created codeplug: " + opt.model + " / " + opt.serial +
                               " / " + GetFlashcode(g_app.cp) +
                               " (save it, then write it from the radio menu)";
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static std::wstring GuiExeDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t i = s.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : s.substr(0, i);
}

static std::wstring GuiTemp(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    base += L"opendepot-gui";
    CreateDirectoryW(base.c_str(), nullptr);
    return base + L"\\" + name;
}

static std::wstring BackupDir() {
    std::wstring d = GuiExeDir() + L"\\..\\..\\radio-backups";
    if (GetFileAttributesW(d.c_str()) == INVALID_FILE_ATTRIBUTES) {
        d = GuiExeDir() + L"\\radio-backups";
        CreateDirectoryW(d.c_str(), nullptr);
    }
    return d;
}

static std::wstring Stamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t b[64];
    swprintf(b, 64, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
             st.wMinute, st.wSecond);
    return b;
}

static bool RunCli(const std::wstring& args, std::string& err) {
    const std::wstring cli = GuiExeDir() + L"\\opendepot-cli.exe";
    if (GetFileAttributesW(cli.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "opendepot-cli.exe not found next to OpenDepot";
        return false;
    }
    std::wstring cmd = L"\"" + cli + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        err = "Cannot start opendepot-cli (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    g_radioCliPid = pi.dwProcessId;
    const DWORD wait = WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
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
    g_radioCliPid = 0;
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "radio operation failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

struct RadioJob {
    std::atomic<bool> done{false};
    bool ok = false;
    bool isWrite = false;
    std::string message;
    std::unique_ptr<Codeplug> result;
};
static std::unique_ptr<RadioJob> g_radioJob;
static std::thread g_radioThread;

static void StartRadioJob(bool write) {
    if (g_app.radioBusy || !g_app.loaded) return;
    std::string err;
    if (write && !ValidateRadioCodeplug(g_app.cp, err)) {
        g_app.status = "Write refused: " + err;
        return;
    }
    const std::wstring tpl = GuiTemp(write ? L"gui-write.cpg" : L"gui-template.cpg");
    const std::wstring out = GuiTemp(L"gui-read.cpg");
    if (!SaveCodeplug(tpl, g_app.cp, err)) {
        g_app.status = "Radio: cannot stage codeplug: " + err;
        return;
    }
    std::wstring backup;
    if (write) {
        std::wstring serial(g_app.cp.header.serial.begin(), g_app.cp.header.serial.end());
        backup = BackupDir() + L"\\prewrite-" + serial + L"-" + Stamp() + L".cpg";
    }
    g_app.radioBusy = true;
    g_app.radioStatus = write ? "Writing to radio..." : "Reading from radio...";
    g_app.status = g_app.radioStatus;
    auto job = std::make_unique<RadioJob>();
    job->isWrite = write;
    RadioJob* raw = job.get();
    g_radioJob = std::move(job);
    const std::string port(g_app.radioPort);
    g_radioThread = std::thread([raw, write, port, tpl, out, backup]() {
        std::string e;
        if (write) {
            std::wstring bargs = L"--read-radio " + std::wstring(port.begin(), port.end()) +
                                 L" \"" + tpl + L"\" \"" + backup + L"\"";
            if (!RunCli(bargs, e)) {
                raw->ok = false;
                raw->message = "pre-write backup failed (" + e + "); refusing to write";
                raw->done = true;
                return;
            }
        }
        std::wstring args = (write ? L"--write-radio " : L"--read-radio ") +
                            std::wstring(port.begin(), port.end()) + L" \"" + tpl + L"\"";
        if (!write) args += L" \"" + out + L"\"";
        raw->ok = RunCli(args, e);
        if (!raw->ok) {
            raw->message = e;
        } else if (!write) {
            auto cp = std::make_unique<Codeplug>();
            if (LoadCodeplug(out, *cp)) {
                raw->result = std::move(cp);
            } else {
                raw->ok = false;
                raw->message = "read produced an unreadable codeplug: " + cp->error;
            }
        }
        raw->done = true;
    });
}

static void PollRadioJob() {
    if (!g_radioJob || !g_radioJob->done.load()) return;
    if (g_radioThread.joinable()) g_radioThread.join();
    if (g_radioJob->ok) {
        if (!g_radioJob->isWrite && g_radioJob->result) {
            g_app.cp = std::move(*g_radioJob->result);
            g_app.loaded = true;
            g_app.selected = -1;
            SyncIdentityBuffers();
            g_app.status = "Read from radio: " + std::to_string(g_app.cp.records.size()) +
                           " records.";
        } else {
            g_app.status = "Write to radio complete.";
        }
    } else {
        g_app.status = "Radio: " + g_radioJob->message;
    }
    g_app.radioBusy = false;
    g_radioJob.reset();
}

static void CancelRadio() {
    const DWORD pid = g_radioCliPid.load();
    if (pid) {
        std::wstring cmd = L"taskkill /T /F /PID " + std::to_wstring(pid);
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    g_app.radioStatus = "Cancelling...";
}

static void DrawRadioConfirm() {
    if (g_app.showWriteConfirm) {
        ImGui::OpenPopup("Write to Radio");
        g_app.showWriteConfirm = false;
    }
    if (!ImGui::BeginPopupModal("Write to Radio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("This overwrites the radio's codeplug with the open file.");
    ImGui::Text("Identity: %s / %s", g_app.cp.header.serial.c_str(), g_app.cp.header.model.c_str());
    ImGui::TextUnformatted("A fresh read of the radio is saved to build\\radio-backups\\ before writing.");
    ImGui::Separator();
    std::string verr;
    if (!ValidateRadioCodeplug(g_app.cp, verr)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Write refused: %s", verr.c_str());
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    } else {
        if (ImGui::Button("Write")) {
            StartRadioJob(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void DrawUI() {
    PollRadioJob();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("OpenDepot", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open .cpg...", "Ctrl+O")) {
                std::wstring p;
                if (OpenDialog(p)) LoadPath(p);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+S", false, g_app.loaded)) {
                std::wstring p;
                if (SaveDialog(p)) SaveTo(p);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Create Codeplug...", nullptr, false, true)) OpenCreateDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Re-verify", nullptr, false, g_app.loaded)) {
                size_t bad = 0;
                for (auto& r : g_app.cp.records) if (!r.verified) ++bad;
                g_app.status = bad ? "Verification found bad records." : "All records verified.";
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Radio")) {
            ImGui::SetNextItemWidth(90);
            ImGui::InputText("Port", g_app.radioPort, sizeof(g_app.radioPort));
            const bool can = g_app.loaded && !g_app.radioBusy;
            if (ImGui::MenuItem("Read from Radio...", nullptr, false, can)) StartRadioJob(false);
            if (ImGui::MenuItem("Write to Radio...", nullptr, false, can)) g_app.showWriteConfirm = true;
            if (g_app.radioBusy) {
                if (ImGui::MenuItem("Cancel")) CancelRadio();
                ImGui::TextUnformatted(g_app.radioStatus.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::TextUnformatted(g_app.status.c_str());
    ImGui::Separator();

    DrawCreateDialog();
    DrawRadioConfirm();

    if (!g_app.loaded) {
        ImGui::TextWrapped("Open an ASTRO 25 .cpg codeplug. OpenDepot decodes the ADK compound "
                           "container, verifies the stream directory, and can re-save a valid "
                           "codeplug. Use Tools > Create from template to clone with a new identity.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            const auto& H = g_app.cp.header;
            ImGui::Text("File:   %s", g_app.pathUtf8);
            ImGui::Text("Model:  %s", H.model.c_str());
            ImGui::Text("Serial: %s", H.serial.c_str());
            ImGui::Text("Version:%s   ADK type: %d   Encryption type: %d",
                        H.version.c_str(), H.adkType, H.encType);
            ImGui::Text("GUID:   %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                        H.guid[0], H.guid[1], H.guid[2], H.guid[3], H.guid[4], H.guid[5], H.guid[6],
                        H.guid[7], H.guid[8], H.guid[9], H.guid[10], H.guid[11], H.guid[12],
                        H.guid[13], H.guid[14], H.guid[15]);
            if (g_app.cp.seed.size() >= 9)
                ImGui::Text("Crypto seed: %.9s", reinterpret_cast<const char*>(g_app.cp.seed.data()));
            size_t good = 0;
            for (auto& r : g_app.cp.records) if (r.verified) ++good;
            ImGui::Text("Records: %zu   Verified: %zu   Bad: %zu",
                        g_app.cp.records.size(), good, g_app.cp.records.size() - good);

            ImGui::Separator();
            ImGui::TextUnformatted("Identity (patches header + 0x10040000 record; fields must fit)");
            ImGui::SetNextItemWidth(220);
            ImGui::InputText("Serial", g_app.serial, sizeof(g_app.serial));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            ImGui::InputText("Model", g_app.model, sizeof(g_app.model));
            if (ImGui::Button("Apply identity")) {
                if (UpdateIdentity(g_app.cp, g_app.serial, g_app.model)) {
                    g_app.status = "Identity updated (header + record 0x10040000).";
                    g_app.identityDirty = true;
                } else {
                    g_app.status = "Identity rejected: new values must fit the existing fields.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save As...")) {
                std::wstring p;
                if (SaveDialog(p)) SaveTo(p);
            }
            if (!g_app.saveError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_app.saveError.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Records")) {
            if (ImGui::BeginTable("recs", 6,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Strm");
                ImGui::TableSetupColumn("Size");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Checksum");
                ImGui::TableSetupColumn("OK");
                ImGui::TableHeadersRow();
                for (int i = 0; i < (int)g_app.cp.records.size(); ++i) {
                    const auto& r = g_app.cp.records[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char id[16];
                    snprintf(id, sizeof(id), "0x%08X", r.id);
                    if (ImGui::Selectable(id, g_app.selected == i)) g_app.selected = i;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", r.streamNum);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%zu", r.raw.size());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%04X", r.type);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("0x%04X", r.checksum);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextColored(r.verified ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
                                       r.verified ? "yes" : "NO");
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Record data")) {
            if (g_app.selected >= 0 && g_app.selected < (int)g_app.cp.records.size()) {
                const auto& r = g_app.cp.records[g_app.selected];
                ImGui::Text("ID 0x%08X  stream %d  size %zu  type 0x%04X",
                            r.id, r.streamNum, r.raw.size(), r.type);
                DrawHex(r.raw);
            } else {
                ImGui::TextUnformatted("Select a record in the Records tab.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                      &level, &g_pd3dDeviceContext) != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRenderTargetView);
        back->Release();
    }
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lParam;
            SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance, nullptr, nullptr, nullptr,
                   nullptr, L"OpenDepotWndClass", nullptr};
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowW(wc.lpszClassName, L"OpenDepot", WS_OVERLAPPEDWINDOW, 120, 80, 1280, 800,
                           nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        MessageBoxW(nullptr, L"Failed to create Direct3D 11 device.", L"OpenDepot", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUI();
        ImGui::Render();
        const float clear[4] = {0.11f, 0.11f, 0.13f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

