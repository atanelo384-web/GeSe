#include "ai_bg.h"
#include "ai_local.h"
#include "media.h"
#include "gfx.h"
#include <winhttp.h>

bool httpPostMultipart(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
                       const std::string& fileField, const std::string& fileName, const std::vector<uint8_t>& fileData,
                       const std::vector<std::pair<std::string, std::string>>& fields, int& status, std::vector<uint8_t>& response,
                       std::string& err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "invalid URL: " + url; return false; }
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET ses = WinHttpOpen(L"GeSe/2.4.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHTTP unavailable"; return false; }
    WinHttpSetTimeouts(ses, 10000, 15000, 60000, 120000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) { WinHttpCloseHandle(ses); err = "cannot connect to " + narrow(host); return false; }
    HINTERNET req = WinHttpOpenRequest(con, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); err = "request failed"; return false; }

    std::string boundary = strf("----GeSeBoundary%08x%08x", GetTickCount(), (unsigned)rand());
    std::string body;
    for (auto& f : fields) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + f.first + "\"\r\n\r\n" + f.second + "\r\n";
    }
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + fileField + "\"; filename=\"" + fileName +
            "\"\r\nContent-Type: image/png\r\n\r\n";
    body.append((const char*)fileData.data(), fileData.size());
    body += "\r\n--" + boundary + "--\r\n";

    std::wstring hdr = widen("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    for (auto& h : headers) hdr += widen(h.first + ": " + h.second + "\r\n");
    BOOL ok = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        err = strf("network error %lu", GetLastError());
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }
    DWORD code = 0, sz = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    status = (int)code;
    response.clear();
    while (true) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        size_t base = response.size();
        response.resize(base + avail);
        DWORD rd = 0;
        WinHttpReadData(req, response.data() + base, avail, &rd);
        response.resize(base + rd);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return true;
}

static void unpremultiply(std::vector<uint8_t>& p) {
    for (size_t i = 0; i + 3 < p.size(); i += 4) {
        unsigned a = p[i + 3];
        if (a == 0 || a == 255) continue;
        for (int k = 0; k < 3; k++) p[i + k] = (uint8_t)std::min(255u, (p[i + k] * 255u + a / 2) / a);
    }
}

// The built-in network runs in a child GeSe process ("--matte-job"): every job gets a clean GPU state
// (DirectML failed with "out of memory" after hardware-encoder probing in the editor process) and a
// driver problem can't take the editor down. Progress comes back as lines on a pipe.
static void sendLine(const std::string& s) {
    DWORD wr = 0;
    std::string l = s + "\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), l.data(), (DWORD)l.size(), &wr, nullptr);
}

int aiRunMatteJob(const std::string& jobPath) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string s;
    if (!readFile(jobPath, s)) { sendLine("E cannot read the job file"); return 1; }
    Json j = Json::parse(s);
    Footage f;
    if (auto* fj = j.get("footage")) f.load(*fj);
    f.hasVideo = true;  // not stored in the project json (found by probing); jobs are only made for footage with a picture
    f.media = MediaSource::open(f);
    if (!f.media) { sendLine("E cannot open " + f.path); return 1; }
    std::string outDir = j.str("outDir"), base = j.str("baseName");
    bool gpu = j.boolean("gpu", true);
    std::vector<int64_t> frames;
    if (auto* fa = j.get("frames")) for (auto& v : fa->a) frames.push_back((int64_t)v.n);
    for (size_t i = 0; i < frames.size(); i++) {
        FramePtr fr = f.media->decodeSync(frames[i], 0);
        if (!fr) { sendLine("E cannot decode source frame"); return 1; }
        std::vector<uint8_t> px = fr->rgba;
        unpremultiply(px);
        std::vector<uint8_t> alpha;
        std::string e;
        if (!localMatte(px.data(), fr->w, fr->h, alpha, gpu, e)) { sendLine("E " + e); return 1; }
        if (i == 0) sendLine("S " + localDeviceName());
        for (size_t k = 0; k < alpha.size(); k++) px[k * 4 + 3] = (uint8_t)(px[k * 4 + 3] * alpha[k] / 255);
        std::string out = frames.size() == 1 ? joinPath(outDir, base + "_cutout.png") : joinPath(outDir, strf("%s_cutout_%05d.png", base.c_str(), (int)i));
        if (!gfx::savePNG(out, px.data(), fr->w, fr->h)) { sendLine("E cannot write " + out); return 1; }
        sendLine("F " + out);
    }
    return 0;
}

bool AiBackgroundRemover::runLocalProcess() {
    Json j = Json::object();
    j["footage"] = desc.footage->save();
    Json fa = Json::array();
    for (int64_t fi : desc.frames) fa.push(Json((double)fi));
    j["frames"] = fa;
    j["outDir"] = desc.outDir;
    j["baseName"] = desc.baseName;
    j["gpu"] = desc.gpu;
    std::string jobPath = joinPath(desc.outDir, "matte_job.json");
    if (!writeFile(jobPath, j.dump())) return false;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --matte-job \"" + widen(jobPath) + L"\"";
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = INVALID_HANDLE_VALUE;
    si.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    {
        std::lock_guard<std::mutex> lk(mx);
        msg = strf("AI: loading the network... (%d frame%s)", total, total > 1 ? "s" : "");
    }
    std::string pending, device;
    char buf[4096];
    while (true) {
        if (stopFlag) { TerminateProcess(pi.hProcess, 1); break; }
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;  // child closed the pipe
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            }
            continue;
        }
        DWORD n = 0;
        if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof(buf)), &n, nullptr) || n == 0) break;
        pending.append(buf, n);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (line.size() < 2) continue;
            std::lock_guard<std::mutex> lk(mx);
            if (line[0] == 'F') {
                outFiles.push_back(line.substr(2));
                done++;
                msg = strf("AI: frame %d of %d%s", done.load(), total, device.empty() ? "" : (" · " + device).c_str());
            } else if (line[0] == 'S') {
                device = line.substr(2);
            } else if (line[0] == 'E') {
                error = line.substr(2);
            }
        }
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    DeleteFileW(widen(jobPath).c_str());
    std::lock_guard<std::mutex> lk(mx);
    if (code != 0 && error.empty() && !stopFlag) error = strf("the AI process stopped (code %lu)", code);
    return true;
}

// built-in network in this process (used when the child process can't be started)
void AiBackgroundRemover::runLocal() {
    if (runLocalProcess()) return;
    struct Release {
        ~Release() { localRelease(); }
    } release;
    auto& f = *desc.footage;
    for (size_t i = 0; i < desc.frames.size() && !stopFlag; i++) {
        FramePtr fr = f.media ? f.media->decodeSync(desc.frames[i], 0) : nullptr;
        if (!fr) { std::lock_guard<std::mutex> lk(mx); error = "cannot decode source frame"; break; }
        std::vector<uint8_t> px = fr->rgba;
        unpremultiply(px);
        {
            std::lock_guard<std::mutex> lk(mx);
            std::string dev = localDeviceName();
            msg = strf("AI: frame %d of %d%s", (int)i + 1, total, dev.empty() ? " (loading the network...)" : (" · " + dev).c_str());
        }
        std::vector<uint8_t> alpha;
        std::string e;
        if (!localMatte(px.data(), fr->w, fr->h, alpha, desc.gpu, e)) {
            std::lock_guard<std::mutex> lk(mx);
            error = e;
            break;
        }
        for (size_t k = 0; k < alpha.size(); k++) px[k * 4 + 3] = (uint8_t)(px[k * 4 + 3] * alpha[k] / 255);
        std::string out = desc.frames.size() == 1 ? joinPath(desc.outDir, desc.baseName + "_cutout.png")
                                                  : joinPath(desc.outDir, strf("%s_cutout_%05d.png", desc.baseName.c_str(), (int)i));
        if (!gfx::savePNG(out, px.data(), fr->w, fr->h)) { std::lock_guard<std::mutex> lk(mx); error = "cannot write " + out; break; }
        {
            std::lock_guard<std::mutex> lk(mx);
            outFiles.push_back(out);
        }
        done++;
    }
}

bool AiBackgroundRemover::start(const AiJobDesc& d, std::string& err) {
    if (active) { err = "a background removal job is already running"; return false; }
    if (!d.footage || d.frames.empty()) { err = "nothing to process"; return false; }
    if (d.provider == 0 && !localModelAvailable()) { err = "The built-in AI model is missing (resources\\Models). Reinstall GeSe."; return false; }
    if ((d.provider == 1 || d.provider == 2) && d.apiKey.empty()) {
        err = "API key is missing. Enter it in Edit > Preferences > AI Tools.";
        return false;
    }
    if (th.joinable()) th.join();
    desc = d;
    total = (int)d.frames.size();
    done = 0;
    stopFlag = false;
    finished = false;
    active = true;
    outFiles.clear();
    error.clear();
    makeDirs(d.outDir);
    th = std::thread([this] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string url, field = "image_file";
        std::vector<std::pair<std::string, std::string>> headers, fields;
        switch (desc.provider) {
        case 0: break;
        case 1:
            url = "https://api.remove.bg/v1.0/removebg";
            headers.push_back({"X-Api-Key", desc.apiKey});
            fields = {{"size", "auto"}, {"format", "png"}};
            break;
        case 2:
            url = "https://sdk.photoroom.com/v1/segment";
            headers.push_back({"x-api-key", desc.apiKey});
            break;
        case 3:
            url = desc.endpoint.empty() ? "http://127.0.0.1:7000/api/remove" : desc.endpoint;
            field = "file";
            break;
        default:
            url = desc.endpoint;
            if (!desc.apiKey.empty()) headers.push_back({"X-Api-Key", desc.apiKey});
        }
        auto& f = *desc.footage;
        if (desc.provider == 0) {
            localNewJob();
            runLocal();
        }
        for (size_t i = 0; i < desc.frames.size() && !stopFlag && desc.provider != 0; i++) {
            std::vector<uint8_t> png;
            if (f.kind == FootageKind::Image && (fileExt(f.path) == ".png" || fileExt(f.path) == ".jpg" || fileExt(f.path) == ".jpeg")) {
                std::string raw;
                readFile(f.path, raw);
                png.assign(raw.begin(), raw.end());
            } else {
                FramePtr fr = f.media ? f.media->decodeSync(desc.frames[i], 0) : nullptr;
                if (!fr) { std::lock_guard<std::mutex> lk(mx); error = "cannot decode source frame"; break; }
                std::vector<uint8_t> px = fr->rgba;
                unpremultiply(px);
                gfx::encodePNG(px.data(), fr->w, fr->h, png);
            }
            {
                std::lock_guard<std::mutex> lk(mx);
                msg = strf("Uploading frame %d of %d...", (int)i + 1, total);
            }
            int status = 0;
            std::vector<uint8_t> resp;
            std::string e;
            if (!httpPostMultipart(url, headers, field, "frame.png", png, fields, status, resp, e)) {
                std::lock_guard<std::mutex> lk(mx);
                error = e;
                break;
            }
            if (status != 200) {
                std::lock_guard<std::mutex> lk(mx);
                std::string body(resp.begin(), resp.end());
                if (body.size() > 300) body = body.substr(0, 300);
                error = strf("server answered HTTP %d: %s", status, body.c_str());
                break;
            }
            std::string out = desc.frames.size() == 1 ? joinPath(desc.outDir, desc.baseName + "_cutout.png")
                                                      : joinPath(desc.outDir, strf("%s_cutout_%05d.png", desc.baseName.c_str(), (int)i));
            // re-encode to guarantee PNG (some services can answer JPEG/WebP)
            std::vector<uint8_t> px;
            int w, h;
            if (gfx::decodeImageMemory(resp.data(), resp.size(), px, w, h)) gfx::savePNG(out, px.data(), w, h);
            else writeFileBin(out, resp.data(), resp.size());
            {
                std::lock_guard<std::mutex> lk(mx);
                outFiles.push_back(out);
            }
            done++;
        }
        if (stopFlag) { std::lock_guard<std::mutex> lk(mx); if (error.empty()) error = "cancelled"; }
        CoUninitialize();
        finished = true;
        active = false;
    });
    return true;
}

std::string AiBackgroundRemover::status() {
    std::lock_guard<std::mutex> lk(mx);
    return msg;
}

bool AiBackgroundRemover::takeResult(AiJobDesc& d, std::vector<std::string>& files, std::string& err) {
    if (!finished) return false;
    if (th.joinable()) th.join();
    finished = false;
    std::lock_guard<std::mutex> lk(mx);
    d = desc;
    files = outFiles;
    err = error;
    return true;
}

AiBackgroundRemover::~AiBackgroundRemover() {
    stopFlag = true;
    if (th.joinable()) th.join();
}
