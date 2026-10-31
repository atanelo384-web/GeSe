#include "ai_local.h"
#include "onnxruntime_c_api.h"

namespace {
typedef const OrtApiBase*(ORT_API_CALL* GetApiBaseFn)(void);
typedef OrtStatus*(ORT_API_CALL* AppendDmlFn)(OrtSessionOptions*, int);

HMODULE g_ort = nullptr;
const OrtApi* g_api = nullptr;
OrtEnv* g_env = nullptr;
OrtSession* g_sess = nullptr;
bool g_sessGpu = false;
std::string g_inName, g_outName, g_device;
int g_inW = 1024, g_inH = 1024;

bool check(OrtStatus* st, std::string& err) {
    if (!st) return true;
    err = g_api->GetErrorMessage(st);
    g_api->ReleaseStatus(st);
    return false;
}

bool loadRuntime(std::string& err) {
    if (g_api) return true;
    std::wstring dll = widen(joinPath(runtimeDir(), "onnxruntime.dll"));
    // altered search path: DirectML.dll and the VC runtime next to onnxruntime.dll are found first
    g_ort = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_ort) { err = strf("onnxruntime.dll could not be loaded (error %lu). Reinstall GeSe.", GetLastError()); return false; }
    auto getBase = (GetApiBaseFn)(void*)GetProcAddress(g_ort, "OrtGetApiBase");
    const OrtApiBase* base = getBase ? getBase() : nullptr;
    g_api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!g_api) { err = "unsupported onnxruntime.dll version"; return false; }
    if (!check(g_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "GeSe", &g_env), err)) { g_api = nullptr; return false; }
    return true;
}

bool createSession(bool gpu, std::string& err) {
    if (g_sess && g_sessGpu == gpu) return true;
    if (g_sess) { g_api->ReleaseSession(g_sess); g_sess = nullptr; }
    OrtSessionOptions* so = nullptr;
    if (!check(g_api->CreateSessionOptions(&so), err)) return false;
    g_api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
    bool dml = false;
    if (gpu) {
        auto appendDml = (AppendDmlFn)(void*)GetProcAddress(g_ort, "OrtSessionOptionsAppendExecutionProvider_DML");
        g_api->DisableMemPattern(so);
        g_api->SetSessionExecutionMode(so, ORT_SEQUENTIAL);
        // DirectML graph fusion compiles the whole network into one operator whose scratch memory is ~17 GB
        // of commit for BiRefNet at 1024 px; op by op the arena reuses buffers and it fits a normal PC
        g_api->AddSessionConfigEntry(so, "ep.dml.disable_graph_fusion", "1");
        std::string e;
        if (appendDml && check(appendDml(so, 0), e)) dml = true;
        else logf("ai: DirectML unavailable (%s), using CPU", e.c_str());
    }
    if (!dml) g_api->SetIntraOpNumThreads(so, (int)std::max(1u, std::thread::hardware_concurrency()));
    std::wstring model = widen(localModelPath());
    OrtStatus* st = g_api->CreateSession(g_env, model.c_str(), so, &g_sess);
    if (st && dml) {
        // a GPU without DX12 feature level support: retry on the CPU
        logf("ai: GPU session failed (%s), retrying on CPU", g_api->GetErrorMessage(st));
        g_api->ReleaseStatus(st);
        g_api->ReleaseSessionOptions(so);
        return createSession(false, err) && ((g_sessGpu = gpu), true);
    }
    g_api->ReleaseSessionOptions(so);
    if (!check(st, err)) { g_sess = nullptr; return false; }
    g_sessGpu = gpu;
    g_device = dml ? "GPU (DirectML)" : "CPU";
    OrtAllocator* al = nullptr;
    g_api->GetAllocatorWithDefaultOptions(&al);
    char* nm = nullptr;
    if (check(g_api->SessionGetInputName(g_sess, 0, al, &nm), err)) { g_inName = nm; g_api->AllocatorFree(al, nm); }
    if (check(g_api->SessionGetOutputName(g_sess, 0, al, &nm), err)) { g_outName = nm; g_api->AllocatorFree(al, nm); }
    OrtTypeInfo* ti = nullptr;
    if (check(g_api->SessionGetInputTypeInfo(g_sess, 0, &ti), err)) {
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        g_api->CastTypeInfoToTensorInfo(ti, &tsi);
        size_t n = 0;
        g_api->GetDimensionsCount(tsi, &n);
        if (n == 4) {
            int64_t d[4];
            g_api->GetDimensions(tsi, d, 4);
            if (d[2] > 0) g_inH = (int)d[2];
            if (d[3] > 0) g_inW = (int)d[3];
        }
        g_api->ReleaseTypeInfo(ti);
    }
    logf("ai: %s on %s, input %s %dx%d", fileName(localModelPath()).c_str(), g_device.c_str(), g_inName.c_str(), g_inW, g_inH);
    return !g_inName.empty() && !g_outName.empty();
}

// RGBA8 -> planar float, ImageNet normalisation; box filter when shrinking, bilinear when enlarging
void toInput(const uint8_t* src, int w, int h, int ow, int oh, std::vector<float>& out) {
    static const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    out.assign((size_t)3 * ow * oh, 0.f);
    size_t plane = (size_t)ow * oh;
    for (int oy = 0; oy < oh; oy++) {
        int y0 = (int)((int64_t)oy * h / oh), y1 = std::max(y0 + 1, (int)((int64_t)(oy + 1) * h / oh));
        for (int ox = 0; ox < ow; ox++) {
            int x0 = (int)((int64_t)ox * w / ow), x1 = std::max(x0 + 1, (int)((int64_t)(ox + 1) * w / ow));
            float acc[3] = {0, 0, 0};
            if (y1 - y0 <= 1 && x1 - x0 <= 1) {
                float fx = clampv((ox + 0.5f) * w / ow - 0.5f, 0.f, (float)(w - 1)), fy = clampv((oy + 0.5f) * h / oh - 0.5f, 0.f, (float)(h - 1));
                int ix = (int)fx, iy = (int)fy, jx = std::min(ix + 1, w - 1), jy = std::min(iy + 1, h - 1);
                float ax = fx - ix, ay = fy - iy;
                for (int c = 0; c < 3; c++) {
                    float a = src[((size_t)iy * w + ix) * 4 + c] * (1 - ax) + src[((size_t)iy * w + jx) * 4 + c] * ax;
                    float b = src[((size_t)jy * w + ix) * 4 + c] * (1 - ax) + src[((size_t)jy * w + jx) * 4 + c] * ax;
                    acc[c] = a * (1 - ay) + b * ay;
                }
            } else {
                int sy = std::max(1, (y1 - y0) / 4), sx = std::max(1, (x1 - x0) / 4), cnt = 0;
                for (int y = y0; y < y1; y += sy)
                    for (int x = x0; x < x1; x += sx, cnt++)
                        for (int c = 0; c < 3; c++) acc[c] += src[((size_t)y * w + x) * 4 + c];
                for (int c = 0; c < 3; c++) acc[c] /= std::max(1, cnt);
            }
            for (int c = 0; c < 3; c++) out[c * plane + (size_t)oy * ow + ox] = (acc[c] / 255.f - mean[c]) / sd[c];
        }
    }
}
}  // namespace

std::string localModelPath() {
    std::string dir = resPath("Models");
    std::string p = joinPath(dir, "birefnet_lite.onnx");
    if (fileExists(p)) return p;
    // any other .onnx matte model dropped into Main\Models (e.g. the full BiRefNet) is used too
    auto files = listFiles(dir, ".onnx", false);
    return files.empty() ? p : files.front();
}
bool localModelAvailable() { return fileExists(localModelPath()) && fileExists(joinPath(runtimeDir(), "onnxruntime.dll")); }
std::string localDeviceName() { return g_device; }

static bool g_gpuFailed = false;  // the video card ran out of memory in this job: the rest runs on the CPU

void localNewJob() { g_gpuFailed = false; }

void localRelease() {
    if (g_api && g_sess) g_api->ReleaseSession(g_sess);
    g_sess = nullptr;
    g_device.clear();
}

static bool runMatte(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& alpha, bool gpu, std::string& err);

bool localMatte(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& alpha, bool gpu, std::string& err) {
    if (gpu && !g_gpuFailed && runMatte(rgba, w, h, alpha, true, err)) return true;
    if (gpu && !g_gpuFailed) {
        // e.g. DirectML out of video memory (other programs use the card): finish the job on the processor
        logf("ai: GPU run failed (%s), switching to CPU", err.c_str());
        g_gpuFailed = true;
        err.clear();
    }
    return runMatte(rgba, w, h, alpha, false, err);
}

static bool runMatte(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& alpha, bool gpu, std::string& err) {
    if (!loadRuntime(err) || !createSession(gpu, err)) return false;
    std::vector<float> in;
    toInput(rgba, w, h, g_inW, g_inH, in);
    OrtMemoryInfo* mi = nullptr;
    if (!check(g_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi), err)) return false;
    int64_t shape[4] = {1, 3, g_inH, g_inW};
    OrtValue* iv = nullptr;
    OrtValue* ov = nullptr;
    bool ok = check(g_api->CreateTensorWithDataAsOrtValue(mi, in.data(), in.size() * sizeof(float), shape, 4,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &iv), err);
    const char* inNames[1] = {g_inName.c_str()};
    const char* outNames[1] = {g_outName.c_str()};
    if (ok) ok = check(g_api->Run(g_sess, nullptr, inNames, (const OrtValue* const*)&iv, 1, outNames, 1, &ov), err);
    int mw = 0, mh = 0;
    std::vector<float> m;
    if (ok) {
        OrtTensorTypeAndShapeInfo* si = nullptr;
        g_api->GetTensorTypeAndShape(ov, &si);
        size_t n = 0;
        g_api->GetDimensionsCount(si, &n);
        std::vector<int64_t> d(n);
        g_api->GetDimensions(si, d.data(), n);
        g_api->ReleaseTensorTypeAndShapeInfo(si);
        if (n >= 2) { mh = (int)d[n - 2]; mw = (int)d[n - 1]; }
        float* p = nullptr;
        g_api->GetTensorMutableData(ov, (void**)&p);
        if (p && mw > 0 && mh > 0) m.assign(p, p + (size_t)mw * mh);
        else { ok = false; err = "unexpected model output"; }
    }
    if (iv) g_api->ReleaseValue(iv);
    if (ov) g_api->ReleaseValue(ov);
    g_api->ReleaseMemoryInfo(mi);
    if (!ok) return false;
    // logits -> probability, then stretch to the full 0..1 range (as rembg does)
    float lo = 1e9f, hi = -1e9f;
    for (float v : m) lo = std::min(lo, v), hi = std::max(hi, v);
    if (lo < -0.01f || hi > 1.01f) {
        for (float& v : m) v = 1.f / (1.f + expf(-v));
        lo = 1e9f, hi = -1e9f;
        for (float v : m) lo = std::min(lo, v), hi = std::max(hi, v);
    }
    if (hi - lo > 0.05f)
        for (float& v : m) v = (v - lo) / (hi - lo);
    // matte back to the frame size (bilinear)
    alpha.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        float fy = clampv((y + 0.5f) * mh / h - 0.5f, 0.f, (float)(mh - 1));
        int iy = (int)fy, jy = std::min(iy + 1, mh - 1);
        float ay = fy - iy;
        for (int x = 0; x < w; x++) {
            float fx = clampv((x + 0.5f) * mw / w - 0.5f, 0.f, (float)(mw - 1));
            int ix = (int)fx, jx = std::min(ix + 1, mw - 1);
            float ax = fx - ix;
            float a = m[(size_t)iy * mw + ix] * (1 - ax) + m[(size_t)iy * mw + jx] * ax;
            float b = m[(size_t)jy * mw + ix] * (1 - ax) + m[(size_t)jy * mw + jx] * ax;
            alpha[(size_t)y * w + x] = (uint8_t)clampv((int)lrintf((a * (1 - ay) + b * ay) * 255.f), 0, 255);
        }
    }
    return true;
}
