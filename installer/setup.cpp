// GeSe v2.4.0 Setup - single-file installer with embedded LZMS payload and its own UI.
#include "core.h"
#include "gfx.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <compressapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <objbase.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
const ImU32 kOrange = IM_COL32(0xFF, 0x88, 0x01, 0xFF);

struct Entry {
    std::string path;
    uint64_t raw = 0, packed = 0;
    uint8_t method = 0;
    const uint8_t* data = nullptr;
};
std::vector<Entry> g_entries;
uint64_t g_rawTotal = 0;

enum class Page { Welcome, License, Location, Components, Installing, Finish, Uninstall, Uninstalled };
struct State {
    HWND hwnd = nullptr;
    float dpi = 1;
    Page page = Page::Welcome;
    bool uninstall = false;
    char path[1024] = {};
    bool accept = false;
    bool optEffects = true, optPresets = true, optPlugins = true, optDesktop = true, optStart = true, optAssoc = true, launch = true;
    // install progress
    std::thread worker;
    std::atomic<float> progress{0};
    std::atomic<bool> done{false}, failed{false};
    std::mutex mx;
    std::string current, error;
    std::deque<std::string> log;
    ImFont *fUI = nullptr, *fBold = nullptr, *fAkira = nullptr, *fArial = nullptr;
    TexPtr art;
    bool quit = false;
} S_;

float Sx(float v) { return v * S_.dpi; }

bool loadPayload() {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    const uint8_t* p = (const uint8_t*)LockResource(g);
    DWORD size = SizeofResource(nullptr, r);
    if (!p || size < 12 || memcmp(p, "GESEPAK1", 8) != 0) return false;
    const uint8_t* e = p + size;
    uint32_t count;
    memcpy(&count, p + 8, 4);
    p += 12;
    for (uint32_t i = 0; i < count && p < e; i++) {
        Entry en;
        uint16_t pl;
        memcpy(&pl, p, 2);
        p += 2;
        en.path.assign((const char*)p, pl);
        p += pl;
        memcpy(&en.raw, p, 8);
        memcpy(&en.packed, p + 8, 8);
        en.method = p[16];
        p += 17;
        en.data = p;
        p += en.packed;
        g_rawTotal += en.raw;
        g_entries.push_back(en);
    }
    return !g_entries.empty();
}

bool unpack(const Entry& en, std::vector<uint8_t>& out) {
    out.resize((size_t)en.raw);
    if (en.method == 0) { memcpy(out.data(), en.data, (size_t)en.raw); return true; }
    DECOMPRESSOR_HANDLE h = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) return false;
    SIZE_T got = 0;
    BOOL ok = Decompress(h, (PVOID)en.data, (SIZE_T)en.packed, out.data(), out.size(), &got);
    CloseDecompressor(h);
    return ok && got == en.raw;
}

const Entry* findEntry(const std::string& path) {
    for (auto& e : g_entries) if (toLower(e.path) == toLower(path)) return &e;
    return nullptr;
}

std::string defaultInstallDir() {
    if (dirExists("D:\\")) return "D:\\GeSe";
    PWSTR p = nullptr;
    std::string r = "C:\\GeSe";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_UserProgramFiles, 0, nullptr, &p))) r = joinPath(narrow(p), "GeSe");
    CoTaskMemFree(p);
    return r;
}

int64_t freeSpace(const std::string& path) {
    std::string root = path.size() >= 3 ? path.substr(0, 3) : path;
    ULARGE_INTEGER fb;
    if (GetDiskFreeSpaceExW(widen(root).c_str(), &fb, nullptr, nullptr)) return (int64_t)fb.QuadPart;
    return -1;
}

bool makeShortcut(const std::string& target, const std::string& lnk, const std::string& workDir, const std::string& desc) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(widen(target).c_str());
    sl->SetWorkingDirectory(widen(workDir).c_str());
    sl->SetDescription(widen(desc).c_str());
    sl->SetIconLocation(widen(target).c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        makeDirs(parentDir(lnk));
        ok = SUCCEEDED(pf->Save(widen(lnk).c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

std::string knownDir(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = narrow(p);
    CoTaskMemFree(p);
    return r;
}

void regSet(HKEY root, const std::string& key, const std::string& name, const std::string& val) {
    HKEY h;
    if (RegCreateKeyExW(root, widen(key).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        std::wstring w = widen(val);
        RegSetValueExW(h, name.empty() ? nullptr : widen(name).c_str(), 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * 2));
        RegCloseKey(h);
    }
}
void regSetDword(HKEY root, const std::string& key, const std::string& name, DWORD v) {
    HKEY h;
    if (RegCreateKeyExW(root, widen(key).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, widen(name).c_str(), 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(h);
    }
}
std::string regGet(HKEY root, const std::string& key, const std::string& name) {
    wchar_t buf[2048];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(root, widen(key).c_str(), widen(name).c_str(), RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) return narrow(buf);
    return {};
}
const char* kUninstKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GeSe";

void addLog(const std::string& s) {
    std::lock_guard<std::mutex> lk(S_.mx);
    S_.current = s;
    S_.log.push_back(s);
    while (S_.log.size() > 8) S_.log.pop_front();
}

void removeTree(const std::string& dir);

void installThread() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string dir = trim(S_.path);
    uint64_t doneBytes = 0;
    auto fail = [&](const std::string& e) {
        std::lock_guard<std::mutex> lk(S_.mx);
        S_.error = e;
        S_.failed = true;
        S_.done = true;
    };
    makeDirs(dir);
    if (!dirExists(dir)) { fail("Cannot create folder " + dir + ". Choose another location or run Setup as administrator."); return; }
    // older versions kept every DLL next to GeSe.exe and the assets in Main\: tidy that up first
    addLog("removing the old file layout");
    for (const char* old : {"avformat-63.dll", "avcodec-63.dll", "avutil-61.dll", "swscale-10.dll", "swresample-7.dll", "avfilter-12.dll",
                            "onnxruntime.dll", "DirectML.dll", "msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll"})
        DeleteFileW(widen(joinPath(dir, old)).c_str());
    if (dirExists(joinPath(dir, "Main"))) removeTree(joinPath(dir, "Main"));
    for (auto& en : g_entries) {
        static const char* essential[] = {"Filters", "Motion", "Transitions", "ColorCorrection", "Blur", "Time"};
        bool isEssential = false;
        for (auto* e : essential) if (en.path.find(e) != std::string::npos) isEssential = true;
        bool skip = (!S_.optEffects && startsWith(en.path, "resources/Effects/") && !isEssential) ||
                    (!S_.optPresets && startsWith(en.path, "resources/Presets/")) || (!S_.optPlugins && startsWith(en.path, "plugins/"));
        if (!skip) {
            std::string dst = joinPath(dir, normPath(en.path));
            addLog("installing " + dir + "/" + en.path);
            std::vector<uint8_t> data;
            if (!unpack(en, data)) { fail("Damaged package entry: " + en.path); return; }
            if (!writeFileBin(dst, data.data(), data.size())) { fail("Cannot write " + dst + " (is GeSe running?)"); return; }
        }
        doneBytes += en.raw;
        S_.progress = (float)doneBytes / std::max<uint64_t>(1, g_rawTotal) * 0.95f;
    }
    // uninstaller = copy of this setup
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::string uninst = joinPath(dir, "Uninstall GeSe.exe");
    CopyFileW(self, widen(uninst).c_str(), FALSE);
    std::string exe = joinPath(dir, "GeSe.exe");
    addLog("creating shortcuts");
    // shortcuts of the previous version are replaced
    DeleteFileW(widen(joinPath(knownDir(FOLDERID_Desktop), "GeSe v2.3.02.lnk")).c_str());
    DeleteFileW(widen(joinPath(joinPath(knownDir(FOLDERID_Programs), "GeSe"), "GeSe v2.3.02.lnk")).c_str());
    if (S_.optDesktop) makeShortcut(exe, joinPath(knownDir(FOLDERID_Desktop), "GeSe v2.4.0.lnk"), dir, "GeSe v2.4.0");
    if (S_.optStart) {
        std::string sm = joinPath(knownDir(FOLDERID_Programs), "GeSe");
        makeShortcut(exe, joinPath(sm, "GeSe v2.4.0.lnk"), dir, "GeSe v2.4.0");
        makeShortcut(uninst, joinPath(sm, "Uninstall GeSe.lnk"), dir, "Remove GeSe");
    }
    if (S_.optAssoc) {
        addLog("registering .gese projects");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\.gese", "", "GeSe.Project");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\GeSe.Project", "", "GeSe Project");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\GeSe.Project\\DefaultIcon", "", "\"" + exe + "\",0");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\GeSe.Project\\shell\\open\\command", "", "\"" + exe + "\" \"%1\"");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\.gsfx", "", "GeSe.Effect");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\GeSe.Effect", "", "GeSe Effect Plugin");
        regSet(HKEY_CURRENT_USER, "Software\\Classes\\GeSe.Effect\\DefaultIcon", "", "\"" + exe + "\",0");
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    addLog("registering uninstaller");
    regSet(HKEY_CURRENT_USER, kUninstKey, "DisplayName", "GeSe v2.4.0");
    regSet(HKEY_CURRENT_USER, kUninstKey, "DisplayVersion", "2.4.0");
    regSet(HKEY_CURRENT_USER, kUninstKey, "Publisher", "GeSe");
    regSet(HKEY_CURRENT_USER, kUninstKey, "InstallLocation", dir);
    regSet(HKEY_CURRENT_USER, kUninstKey, "DisplayIcon", exe);
    regSet(HKEY_CURRENT_USER, kUninstKey, "UninstallString", "\"" + uninst + "\" /uninstall");
    regSetDword(HKEY_CURRENT_USER, kUninstKey, "EstimatedSize", (DWORD)(g_rawTotal / 1024));
    regSetDword(HKEY_CURRENT_USER, kUninstKey, "NoModify", 1);
    regSetDword(HKEY_CURRENT_USER, kUninstKey, "NoRepair", 1);
    S_.progress = 1.f;
    addLog("done");
    S_.done = true;
    CoUninitialize();
}

void removeTree(const std::string& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = narrow(fd.cFileName);
            if (n == "." || n == "..") continue;
            std::string p = joinPath(dir, n);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(p);
            else if (!DeleteFileW(widen(p).c_str())) MoveFileExW(widen(p).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(widen(dir).c_str());
}

void uninstallNow() {
    std::string dir = regGet(HKEY_CURRENT_USER, kUninstKey, "InstallLocation");
    if (dir.empty()) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        dir = parentDir(narrow(self));
    }
    DeleteFileW(widen(joinPath(knownDir(FOLDERID_Desktop), "GeSe v2.4.0.lnk")).c_str());
    DeleteFileW(widen(joinPath(knownDir(FOLDERID_Desktop), "GeSe v2.3.02.lnk")).c_str());
    removeTree(joinPath(knownDir(FOLDERID_Programs), "GeSe"));
    for (const char* k : {"Software\\Classes\\.gese", "Software\\Classes\\GeSe.Project", "Software\\Classes\\.gsfx", "Software\\Classes\\GeSe.Effect", kUninstKey})
        RegDeleteTreeW(HKEY_CURRENT_USER, widen(k).c_str());
    // everything except the running uninstaller; it schedules itself for deletion
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = narrow(fd.cFileName);
            if (n == "." || n == ".." || toLower(n) == "uninstall gese.exe") continue;
            std::string p = joinPath(dir, n);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(p);
            else DeleteFileW(widen(p).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    MoveFileExW(widen(joinPath(dir, "Uninstall GeSe.exe")).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(widen(dir).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// ------------------------------------------------------------------------------------------
bool pill(const char* label, bool primary, ImVec2 size) {
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal::v4(IM_COL32(0xC8, 0x62, 0x08, 0xFF)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal::v4(IM_COL32(0xA0, 0x48, 0x00, 0xFF)));
        ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::White));
    }
    bool r = ImGui::Button(label, size);
    if (primary) ImGui::PopStyleColor(4);
    return r;
}

// Crops the art to the side panel (cover fit, slightly left of centre) and bakes the shading:
// darker overall so the step list reads, deeper toward the bottom for the title, and a soft
// fade into the window colour on the right edge.
std::vector<uint8_t> bakeArt(const std::vector<uint8_t>& src, int w, int h, float aspect, int& ow, int& oh) {
    int cw = w, ch = h;
    if ((float)w / h > aspect) cw = std::max(1, (int)(h * aspect)); else ch = std::max(1, (int)(w / aspect));
    int x0 = (int)((w - cw) * 0.3f), y0 = (h - ch) / 2;
    ow = cw;
    oh = ch;
    std::vector<uint8_t> out((size_t)cw * ch * 4);
    const float bg[3] = {0x23 / 255.f, 0x23 / 255.f, 0x23 / 255.f};
    auto smooth = [](float a, float b, float x) { float t = clampv((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); };
    for (int y = 0; y < ch; y++) {
        float v = (y + 0.5f) / ch;
        float dark = 0.52f * (1.f - 0.85f * smooth(0.5f, 1.0f, v));
        for (int x = 0; x < cw; x++) {
            float u = (x + 0.5f) / cw;
            float fade = smooth(0.62f, 1.0f, u);
            const uint8_t* s = &src[((size_t)(y + y0) * w + (x + x0)) * 4];
            uint8_t* d = &out[((size_t)y * cw + x) * 4];
            for (int k = 0; k < 3; k++) {
                float c = s[k] / 255.f * dark;
                c = c + (bg[k] - c) * fade;
                d[k] = (uint8_t)clampv((int)(c * 255.f + 0.5f), 0, 255);
            }
            d[3] = 255;
        }
    }
    return out;
}

void stepList(ImDrawList* dl, ImVec2 p) {
    static const char* steps[] = {"Welcome", "License", "Location", "Components", "Install", "Finish"};
    int cur = (int)S_.page;
    if (S_.page == Page::Uninstall || S_.page == Page::Uninstalled) return;
    for (int i = 0; i < 6; i++) {
        ImVec2 c = p + ImVec2(Sx(8), Sx(30) * i + Sx(8));
        bool active = i == cur, past = i < cur;
        dl->AddCircleFilled(c, Sx(4.5f), active ? kOrange : (past ? pal::Accent : IM_COL32(255, 255, 255, 60)));
        if (i < 5) dl->AddLine(c + ImVec2(0, Sx(6)), c + ImVec2(0, Sx(24)), IM_COL32(255, 255, 255, 40), Sx(1));
        ImGui::PushFont(active ? S_.fBold : S_.fUI, 0);
        dl->AddText(c + ImVec2(Sx(16), -ImGui::GetTextLineHeight() * 0.5f), active ? pal::White : (past ? pal::Text : pal::TextDim), steps[i]);
        ImGui::PopFont();
    }
}

void drawUI() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = vp->Pos, sz = vp->Size;
    float leftW = Sx(300);
    dl->AddRectFilled(p0, p0 + sz, pal::Bg2);
    // art panel: a rounded card inset from the window edge; the shading and the fade into the
    // window colour are baked into the picture (bakeArt) so the rounded corners stay clean
    if (S_.art) {
        float m = Sx(10);
        dl->AddImageRounded(S_.art->id(), p0 + ImVec2(m, m), p0 + ImVec2(leftW, sz.y - m), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, Sx(14));
    }
    if (S_.fAkira) {
        dl->AddText(S_.fAkira, Sx(52), p0 + ImVec2(Sx(26), sz.y - Sx(118)), kOrange, "GESE");
        dl->AddText(S_.fAkira, Sx(17), p0 + ImVec2(Sx(28), sz.y - Sx(56)), IM_COL32(0x5A, 0x43, 0x25, 0xFF), "V2.4.0");
    }
    stepList(dl, p0 + ImVec2(Sx(28), Sx(36)));
    // drag area + window buttons
    ImGui::SetCursorScreenPos(p0 + ImVec2(leftW, 0));
    ImGui::InvisibleButton("drag", ImVec2(sz.x - leftW - Sx(80), Sx(36)));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        ReleaseCapture();
        SendMessageW(S_.hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    // window buttons drawn as vector glyphs, centred in their boxes (a "_" text glyph sits on the baseline)
    auto winButton = [&](const char* id, bool close, bool enabled) {
        ImVec2 bp = ImGui::GetCursorScreenPos(), bs(Sx(34), Sx(26));
        ImGui::BeginDisabled(!enabled);
        bool pressed = ImGui::InvisibleButton(id, bs);
        bool hov = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        if (hov || held)
            dl->AddRectFilled(bp, bp + bs, close ? (held ? IM_COL32(0xA8, 0x2A, 0x20, 0xFF) : IM_COL32(0xC4, 0x2B, 0x1C, 0xFF))
                                                 : (held ? IM_COL32(0x48, 0x48, 0x48, 0xFF) : IM_COL32(0x3A, 0x3A, 0x3A, 0xFF)), Sx(5));
        ImU32 col = !enabled ? IM_COL32(0x70, 0x70, 0x70, 0xFF) : (hov ? pal::White : pal::Text);
        ImVec2 c = bp + bs * 0.5f;
        float r = Sx(5);
        if (close) {
            dl->AddLine(c - ImVec2(r, r), c + ImVec2(r, r), col, Sx(1.4f));
            dl->AddLine(c + ImVec2(-r, r), c + ImVec2(r, -r), col, Sx(1.4f));
        } else {
            dl->AddLine(c - ImVec2(r, 0), c + ImVec2(r, 0), col, Sx(1.4f));
        }
        return pressed && enabled;
    };
    ImGui::SetCursorScreenPos(p0 + ImVec2(sz.x - Sx(80), Sx(6)));
    if (winButton("##min", false, true)) ShowWindow(S_.hwnd, SW_MINIMIZE);
    ImGui::SameLine(0, Sx(4));
    bool busy = S_.page == Page::Installing && !S_.done;
    if (winButton("##close", true, !busy)) S_.quit = true;

    // content
    float cx = leftW + Sx(44), cw = sz.x - leftW - Sx(88);
    ImGui::SetCursorScreenPos(p0 + ImVec2(cx, Sx(54)));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    auto title = [&](const char* t) {
        ImGui::PushFont(S_.fBold, ImGui::GetStyle().FontSizeBase * 1.7f);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, Sx(6)));
    };
    float bh = Sx(36), bw = Sx(132);
    ImVec2 navPos = p0 + ImVec2(sz.x - Sx(44) - bw * 2 - Sx(10), sz.y - Sx(28) - bh);
    auto nav = [&](bool back, const char* nextLabel, bool nextEnabled) -> int {
        int r = 0;
        ImGui::SetCursorScreenPos(navPos);
        if (back) { if (pill("Back", false, ImVec2(bw, bh))) r = -1; }
        else ImGui::Dummy(ImVec2(bw, bh));
        ImGui::SameLine(0, Sx(10));
        ImGui::BeginDisabled(!nextEnabled);
        if (pill(nextLabel, true, ImVec2(bw, bh))) r = 1;
        ImGui::EndDisabled();
        return r;
    };
    switch (S_.page) {
    case Page::Welcome: {
        title("Welcome to GeSe");
        ImGui::TextColored(pal::v4(pal::TextDim), "Version 2.4.0");
        ImGui::Dummy(ImVec2(0, Sx(10)));
        ImGui::TextWrapped("GeSe is a motion graphics and video editor: compositions, layers, keyframes, masks, track mattes, 3D with camera and lights, motion blur, voice tools and a GPU renderer.");
        ImGui::Dummy(ImVec2(0, Sx(10)));
        for (const char* b : {"217 GPU effects in 16 categories - filters, transitions, motion, glitch, light...",
                              "Effect layers you can move above or below any clip",
                              "AI background removal (remove.bg, Photoroom or offline rembg)",
                              "Export H.264, H.265, ProRes 422/4444, VP9 with alpha, PNG sequences",
                              "Your own effects as .gsfx plugins"}) {
            ImVec2 bp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(bp + ImVec2(Sx(4), ImGui::GetTextLineHeight() * 0.55f), Sx(3), kOrange);
            ImGui::SetCursorScreenPos(bp + ImVec2(Sx(16), 0));
            ImGui::TextWrapped("%s", b);
        }
        ImGui::Dummy(ImVec2(0, Sx(10)));
        ImGui::TextColored(pal::v4(pal::TextDim), "Setup will copy %s of files.", fmtBytes((int64_t)g_rawTotal).c_str());
        if (nav(false, "Next", true) == 1) S_.page = Page::License;
        break;
    }
    case Page::License: {
        title("License Agreement");
        ImGui::BeginChild("lic", ImVec2(cw, sz.y - Sx(250)), ImGuiChildFlags_Borders);
        ImGui::TextWrapped(
            "GeSe v2.4.0 - End User License\n\n"
            "1. You may install and use GeSe on your own computers for personal and commercial video projects.\n"
            "2. Projects, renders and presets you create belong to you.\n"
            "3. GeSe is provided \"as is\", without warranty of any kind.\n\n"
            "Third-party components\n\n"
            "FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample) is licensed under the GNU GPL v3 in this build. "
            "Source code is available at https://ffmpeg.org.\n"
            "Dear ImGui - MIT License, Copyright (c) Omar Cornut.\n"
            "Akira Expanded (splash font) - by its respective author, demo license for personal use.\n"
            "AI background removal services (remove.bg, Photoroom) are governed by their own terms; GeSe only sends the frames you choose.");
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, Sx(6)));
        ImGui::Checkbox("I accept the license agreement", &S_.accept);
        int r = nav(true, "Next", S_.accept);
        if (r < 0) S_.page = Page::Welcome;
        if (r > 0) S_.page = Page::Location;
        break;
    }
    case Page::Location: {
        title("Install Location");
        ImGui::TextWrapped("GeSe will be installed into this folder. GeSe.exe goes to the root; DLLs to runtime, effects, fonts and the AI model to resources, your plugins to plugins.");
        ImGui::Dummy(ImVec2(0, Sx(10)));
        ImGui::SetNextItemWidth(cw - Sx(110));
        ImGui::InputText("##path", S_.path, sizeof(S_.path));
        ImGui::SameLine();
        if (pill("Browse...", false, ImVec2(Sx(100), 0))) {
            IFileOpenDialog* d = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
                DWORD o;
                d->GetOptions(&o);
                d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(d->Show(S_.hwnd))) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(d->GetResult(&it))) {
                        PWSTR s = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &s))) {
                            std::string pick = narrow(s);
                            if (toLower(fileName(pick)) != "gese") pick = joinPath(pick, "GeSe");
                            snprintf(S_.path, sizeof(S_.path), "%s", pick.c_str());
                            CoTaskMemFree(s);
                        }
                        it->Release();
                    }
                }
                d->Release();
            }
        }
        ImGui::Dummy(ImVec2(0, Sx(10)));
        int64_t fs = freeSpace(S_.path);
        ImGui::TextColored(pal::v4(pal::TextDim), "Space required: %s", fmtBytes((int64_t)g_rawTotal).c_str());
        bool enough = fs < 0 || fs > (int64_t)g_rawTotal + 50 * 1024 * 1024;
        if (fs >= 0) ImGui::TextColored(enough ? pal::v4(pal::TextDim) : ImVec4(0.9f, 0.4f, 0.4f, 1), "Space available: %s", fmtBytes(fs).c_str());
        int r = nav(true, "Next", enough && strlen(S_.path) > 3);
        if (r < 0) S_.page = Page::License;
        if (r > 0) S_.page = Page::Components;
        break;
    }
    case Page::Components: {
        title("Components");
        ImGui::BeginDisabled();
        bool core = true;
        ImGui::Checkbox("GeSe editor, renderer and FFmpeg codecs (required)", &core);
        ImGui::EndDisabled();
        ImGui::Checkbox("Full effects library (217 effects; off = filters, motion, transitions, color, blur only)", &S_.optEffects);
        ImGui::Checkbox("Animation presets", &S_.optPresets);
        ImGui::Checkbox("Example plugin and Plugin SDK readme", &S_.optPlugins);
        ImGui::Dummy(ImVec2(0, Sx(8)));
        ImGui::Checkbox("Desktop shortcut", &S_.optDesktop);
        ImGui::Checkbox("Start menu shortcuts", &S_.optStart);
        ImGui::Checkbox("Open .gese projects and .gsfx plugins with GeSe", &S_.optAssoc);
        int r = nav(true, "Install", true);
        if (r < 0) S_.page = Page::Location;
        if (r > 0) {
            S_.page = Page::Installing;
            S_.worker = std::thread(installThread);
        }
        break;
    }
    case Page::Installing: {
        title(S_.done ? (S_.failed ? "Installation failed" : "Installed") : "Installing GeSe...");
        float pr = S_.progress.load();
        ImVec2 bp = ImGui::GetCursorScreenPos();
        ImDrawList* d2 = ImGui::GetWindowDrawList();
        d2->AddRectFilled(bp, bp + ImVec2(cw, Sx(8)), IM_COL32(255, 255, 255, 25), Sx(4));
        d2->AddRectFilled(bp, bp + ImVec2(cw * pr, Sx(8)), kOrange, Sx(4));
        ImGui::Dummy(ImVec2(cw, Sx(14)));
        ImGui::TextColored(pal::v4(pal::TextDim), "%d%%", (int)(pr * 100));
        ImGui::Dummy(ImVec2(0, Sx(10)));
        {
            std::lock_guard<std::mutex> lk(S_.mx);
            ImGui::PushFont(S_.fArial, ImGui::GetStyle().FontSizeBase * 0.95f);
            for (auto& l : S_.log) ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.42f, 1), "%s", l.c_str());
            ImGui::PopFont();
            if (S_.failed) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "%s", S_.error.c_str());
        }
        if (S_.done) {
            if (S_.worker.joinable()) S_.worker.join();
            if (!S_.failed) S_.page = Page::Finish;
            else if (nav(true, "Retry", true) != 0) { S_.done = S_.failed = false; S_.progress = 0; S_.page = Page::Location; }
        }
        break;
    }
    case Page::Finish: {
        title("GeSe is ready");
        ImGui::TextWrapped("GeSe v2.4.0 was installed to %s.", S_.path);
        ImGui::Dummy(ImVec2(0, Sx(10)));
        ImGui::TextColored(pal::v4(pal::TextDim), "Tip: drag an effect onto an empty part of the timeline to create an Effect Layer.");
        ImGui::Dummy(ImVec2(0, Sx(10)));
        ImGui::Checkbox("Launch GeSe now", &S_.launch);
        ImGui::SetCursorScreenPos(navPos + ImVec2(bw + Sx(10), 0));
        if (pill("Finish", true, ImVec2(bw, bh))) {
            if (S_.launch) {
                std::string exe = joinPath(S_.path, "GeSe.exe");
                ShellExecuteW(nullptr, L"open", widen(exe).c_str(), nullptr, widen(S_.path).c_str(), SW_SHOWNORMAL);
            }
            S_.quit = true;
        }
        break;
    }
    case Page::Uninstall: {
        title("Remove GeSe");
        std::string dir = regGet(HKEY_CURRENT_USER, kUninstKey, "InstallLocation");
        ImGui::TextWrapped("This removes GeSe v2.4.0 from %s, its shortcuts and file associations.", dir.empty() ? "this computer" : dir.c_str());
        ImGui::TextColored(pal::v4(pal::TextDim), "Your projects, renders and %%APPDATA%%\\GeSe (settings, user plugins) are kept.");
        int r = nav(false, "Uninstall", true);
        if (r > 0) { uninstallNow(); S_.page = Page::Uninstalled; }
        break;
    }
    case Page::Uninstalled: {
        title("GeSe was removed");
        ImGui::TextWrapped("Thanks for using GeSe.");
        ImGui::SetCursorScreenPos(navPos + ImVec2(bw + Sx(10), 0));
        if (pill("Close", true, ImVec2(bw, bh))) S_.quit = true;
        break;
    }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::End();
    ImGui::PopStyleVar();
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return 1;
    switch (msg) {
    case WM_SIZE: if (wp != SIZE_MINIMIZED) gfx::resize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_CLOSE: if (!(S_.page == Page::Installing && !S_.done)) S_.quit = true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) if (_wcsicmp(argv[i], L"/uninstall") == 0) S_.uninstall = true;
    LocalFree(argv);
    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!S_.uninstall && !loadPayload()) {
        MessageBoxW(nullptr, L"The installer package is damaged.", L"GeSe Setup", MB_ICONERROR);
        return 1;
    }
    snprintf(S_.path, sizeof(S_.path), "%s", defaultInstallDir().c_str());
    if (S_.uninstall) S_.page = Page::Uninstall;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GeSeSetup";
    RegisterClassExW(&wc);
    POINT pt = {0, 0};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    S_.dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (mh / S_.dpi >= 1350) S_.dpi *= 1.2f;
    int W = (int)(880 * S_.dpi), H = (int)(560 * S_.dpi);
    int sx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - W) / 2, sy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - H) / 2;
    S_.hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, S_.uninstall ? L"Uninstall GeSe" : L"GeSe v2.4.0 Setup", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                              sx, sy, W, H, nullptr, nullptr, inst, nullptr);
    int d = (int)(24 * S_.dpi);
    SetWindowRgn(S_.hwnd, CreateRoundRectRgn(0, 0, W + 1, H + 1, d, d), TRUE);
    if (!gfx::init(S_.hwnd)) { MessageBoxW(nullptr, L"Direct3D 11 is required.", L"GeSe Setup", MB_ICONERROR); return 1; }
    ShowWindow(S_.hwnd, SW_SHOW);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplWin32_Init(S_.hwnd);
    ImGui_ImplDX11_Init(gfx::dev(), gfx::ctx());
    ImFontConfig cfg;
    S_.fUI = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &cfg);
    S_.fBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 16.f, &cfg);
    S_.fArial = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.f, &cfg);
    if (!S_.fUI) S_.fUI = io.Fonts->AddFontDefault();
    if (!S_.fBold) S_.fBold = S_.fUI;
    if (!S_.fArial) S_.fArial = S_.fUI;
    io.FontDefault = S_.fUI;
    static std::vector<uint8_t> akira;
    if (const Entry* e = findEntry("resources/Fonts/AkiraExpanded.otf")) {
        if (unpack(*e, akira)) {
            ImFontConfig fc;
            fc.FontDataOwnedByAtlas = false;
            S_.fAkira = io.Fonts->AddFontFromMemoryTTF(akira.data(), (int)akira.size(), 24.f, &fc);
        }
    } else if (fileExists(joinPath(exeDir(), "resources\\Fonts\\AkiraExpanded.otf"))) {
        S_.fAkira = io.Fonts->AddFontFromFileTTF(joinPath(exeDir(), "resources\\Fonts\\AkiraExpanded.otf").c_str(), 24.f);
    }
    {
        std::vector<uint8_t> png, px;
        int w = 0, h = 0;
        bool ok = false;
        if (const Entry* e = findEntry("resources/Splash/setup_art.png")) ok = unpack(*e, png) && gfx::decodeImageMemory(png.data(), png.size(), px, w, h);
        else ok = gfx::loadImageFile(joinPath(exeDir(), "resources\\Splash\\setup_art.png"), px, w, h);
        if (ok) {
            int aw = 0, ah = 0;
            std::vector<uint8_t> art = bakeArt(px, w, h, 290.f / 540.f, aw, ah);
            S_.art = gfx::createTexture(aw, ah, DXGI_FORMAT_R8G8B8A8_UNORM, false, art.data(), aw * 4);
        }
    }
    // theme: neutral greys + logo orange, pill buttons
    ImGuiStyle& st = ImGui::GetStyle();
    st.FrameRounding = 6;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(8, 8);
    st.ChildRounding = 6;
    st.ScrollbarSize = 10;
    st.ScrollbarRounding = 6;
    st.WindowBorderSize = 0;
    st.Colors[ImGuiCol_WindowBg] = pal::v4(pal::Bg2);
    st.Colors[ImGuiCol_ChildBg] = pal::v4(pal::Bg1);
    st.Colors[ImGuiCol_Border] = pal::v4(pal::Bg4);
    st.Colors[ImGuiCol_Text] = pal::v4(pal::Text);
    st.Colors[ImGuiCol_FrameBg] = pal::v4(IM_COL32(0x19, 0x19, 0x19, 0xFF));
    st.Colors[ImGuiCol_FrameBgHovered] = pal::v4(IM_COL32(0x16, 0x16, 0x16, 0xFF));
    st.Colors[ImGuiCol_FrameBgActive] = pal::v4(IM_COL32(0x12, 0x12, 0x12, 0xFF));
    st.Colors[ImGuiCol_CheckMark] = pal::v4(kOrange);
    st.Colors[ImGuiCol_Button] = pal::v4(IM_COL32(0x33, 0x33, 0x33, 0xFF));
    st.Colors[ImGuiCol_ButtonHovered] = pal::v4(IM_COL32(0x3E, 0x3E, 0x3E, 0xFF));
    st.Colors[ImGuiCol_ButtonActive] = pal::v4(IM_COL32(0x48, 0x48, 0x48, 0xFF));
    st.Colors[ImGuiCol_TextSelectedBg] = pal::v4(pal::AccentA(110));
    st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    st.ScaleAllSizes(S_.dpi);
    st.FontScaleDpi = S_.dpi;

    while (!S_.quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) S_.quit = true;
        }
        if (S_.quit) break;
        if (IsIconic(S_.hwnd)) { Sleep(30); continue; }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawUI();
        ImGui::Render();
        gfx::beginBackbuffer(0.137f, 0.137f, 0.137f);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gfx::present(true);
    }
    if (S_.worker.joinable()) S_.worker.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    gfx::shutdown();
    DestroyWindow(S_.hwnd);
    CoUninitialize();
    return 0;
}
