#include "core.h"
#include <shlobj.h>
#include <cstdarg>
#include <ctime>

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::string toLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    size_t st = 0;
    for (size_t i = 0; i <= s.size(); i++)
        if (i == s.size() || s[i] == sep) { r.push_back(s.substr(st, i - st)); st = i + 1; }
    return r;
}
bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
std::string strf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

std::string exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return parentDir(narrow(buf));
}
// program layout: GeSe.exe + runtime (DLLs), resources (effects, fonts, splash, presets, models), plugins, licenses
std::string resDir() {
    static std::string d = [] {
        std::string r = joinPath(exeDir(), "resources");
        return dirExists(r) || !dirExists(joinPath(exeDir(), "Main")) ? r : joinPath(exeDir(), "Main");  // older flat installs
    }();
    return d;
}
std::string resPath(const std::string& rel) { return joinPath(resDir(), rel); }
std::string runtimeDir() { return joinPath(exeDir(), "runtime"); }
static std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = narrow(p);
    CoTaskMemFree(p);
    return r;
}
std::string appDataDir() {
    static std::string d;
    if (d.empty()) { d = joinPath(knownFolder(FOLDERID_RoamingAppData), "GeSe"); makeDirs(d); }
    return d;
}
std::string localDataDir() {
    static std::string d;
    if (d.empty()) { d = joinPath(knownFolder(FOLDERID_LocalAppData), "GeSe"); makeDirs(d); }
    return d;
}
std::string documentsDir() { return knownFolder(FOLDERID_Documents); }

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char c = a.back();
    if (c == '\\' || c == '/') return a + b;
    return a + "\\" + b;
}
std::string fileName(const std::string& p) {
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? p : p.substr(i + 1);
}
std::string fileStem(const std::string& p) {
    std::string n = fileName(p);
    size_t i = n.find_last_of('.');
    return i == std::string::npos ? n : n.substr(0, i);
}
std::string fileExt(const std::string& p) {
    std::string n = fileName(p);
    size_t i = n.find_last_of('.');
    return i == std::string::npos ? std::string() : toLower(n.substr(i));
}
std::string parentDir(const std::string& p) {
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? std::string() : p.substr(0, i);
}
std::string normPath(std::string p) {
    for (auto& c : p) if (c == '/') c = '\\';
    return p;
}
bool fileExists(const std::string& p) {
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool dirExists(const std::string& p) {
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
void makeDirs(const std::string& p) {
    if (p.empty() || dirExists(p)) return;
    makeDirs(parentDir(p));
    CreateDirectoryW(widen(p).c_str(), nullptr);
}
bool readFile(const std::string& p, std::string& out) {
    HANDLE h = CreateFileW(widen(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = sz.QuadPart == 0 || ReadFile(h, out.data(), (DWORD)sz.QuadPart, &rd, nullptr);
    CloseHandle(h);
    return ok;
}
bool writeFileBin(const std::string& p, const void* data, size_t n) {
    makeDirs(parentDir(p));
    HANDLE h = CreateFileW(widen(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = n == 0 || WriteFile(h, data, (DWORD)n, &wr, nullptr);
    CloseHandle(h);
    return ok && wr == n;
}
bool writeFile(const std::string& p, const std::string& data) { return writeFileBin(p, data.data(), data.size()); }
int64_t fileSize(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(widen(p).c_str(), GetFileExInfoStandard, &fa)) return 0;
    return ((int64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}
int64_t fileTime(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(widen(p).c_str(), GetFileExInfoStandard, &fa)) return 0;
    ULARGE_INTEGER u;
    u.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}
std::vector<std::string> listFiles(const std::string& dir, const std::string& ext, bool recursive) {
    std::vector<std::string> r;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return r;
    do {
        std::string n = narrow(fd.cFileName);
        if (n == "." || n == "..") continue;
        std::string full = joinPath(dir, n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) { auto sub = listFiles(full, ext, true); r.insert(r.end(), sub.begin(), sub.end()); }
        } else if (ext.empty() || fileExt(n) == ext) {
            r.push_back(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(r.begin(), r.end());
    return r;
}
std::string fmtBytes(int64_t n) {
    if (n < 1024) return strf("%lld B", (long long)n);
    if (n < 1024 * 1024) return strf("%.1f KB", n / 1024.0);
    if (n < 1024LL * 1024 * 1024) return strf("%.1f MB", n / 1048576.0);
    return strf("%.2f GB", n / 1073741824.0);
}
std::string fmtDate(int64_t s) {
    time_t t = (time_t)s;
    struct tm tmv;
    localtime_s(&tmv, &t);
    char b[64];
    strftime(b, sizeof(b), "%d.%m.%Y %H:%M", &tmv);
    return b;
}

// --------------------------------------------------------------------------
static std::mutex g_logMx;
static std::deque<std::string> g_logLines;
static FILE* g_logFile = nullptr;
void logf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_logFile) g_logFile = _wfopen(widen(joinPath(localDataDir(), "gese.log")).c_str(), L"w");
    if (g_logFile) { fprintf(g_logFile, "%s\n", buf); fflush(g_logFile); }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    g_logLines.push_back(buf);
    while (g_logLines.size() > 400) g_logLines.pop_front();
}
std::vector<std::string> logTail(int n) {
    std::lock_guard<std::mutex> lk(g_logMx);
    std::vector<std::string> r;
    int st = std::max(0, (int)g_logLines.size() - n);
    for (int i = st; i < (int)g_logLines.size(); i++) r.push_back(g_logLines[i]);
    return r;
}
double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static std::atomic<uint64_t> g_nextId{1000};
uint64_t newId() { return g_nextId++; }
void seedIds(uint64_t minNext) {
    uint64_t cur = g_nextId.load();
    while (cur < minNext && !g_nextId.compare_exchange_weak(cur, minNext)) {}
}
uint64_t fnv1a(const void* data, size_t n, uint64_t h) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
std::string fmtTimecode(double t, double fps) {
    if (t < 0) t = 0;
    int ifps = std::max(1, (int)std::lround(fps));
    int64_t fr = timeToFrame(t, fps);
    int64_t secs = (int64_t)std::floor(t + 1e-9);
    int f = (int)(fr - (int64_t)std::floor(secs * fps + 1e-6));
    if (f < 0) f = 0;
    if (f >= ifps) f = ifps - 1;
    return strf("%d:%02d:%02d:%02d", (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60), f);
}
std::string fmtTimeShort(double t) {
    if (t < 0) t = 0;
    int m = (int)(t / 60);
    double s = t - m * 60;
    return strf("%d:%05.2f", m, s);
}
double parseTimecode(const std::string& s, double fps, bool* ok) {
    auto parts = split(trim(s), ':');
    if (ok) *ok = false;
    if (parts.empty()) return 0;
    // a bare number is a frame count
    if (parts.size() == 1) {
        char* e = nullptr;
        double v = strtod(parts[0].c_str(), &e);
        if (e == parts[0].c_str()) return 0;
        if (ok) *ok = true;
        return v / fps;
    }
    double vals[4] = {0, 0, 0, 0};
    int n = (int)std::min<size_t>(4, parts.size());
    for (int i = 0; i < n; i++) vals[4 - n + i] = atof(parts[parts.size() - n + i].c_str());
    if (ok) *ok = true;
    return vals[0] * 3600 + vals[1] * 60 + vals[2] + vals[3] / fps;
}
