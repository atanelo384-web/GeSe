#include "gfx.h"
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <tuple>

namespace gfx {
namespace {
ComPtr<ID3D11Device> g_dev;
ComPtr<ID3D11DeviceContext> g_ctx;
ComPtr<IDXGISwapChain> g_swap;
ComPtr<ID3D11RenderTargetView> g_backRTV;
ComPtr<ID3D11VertexShader> g_vsFull;
ComPtr<ID3D11SamplerState> g_sampLinear, g_sampPoint, g_sampWrap;
ComPtr<ID3D11BlendState> g_blendNone, g_blendOver;
ComPtr<ID3D11RasterizerState> g_raster;
ComPtr<ID3D11Buffer> g_cb[2];
ComPtr<ID3D11Texture2D> g_staging;
int g_stagW = 0, g_stagH = 0;
ComPtr<ID3D11Texture2D> g_staging1;
std::string g_adapter;
int64_t g_vram = 0;
int g_bbW = 0, g_bbH = 0;
HWND g_hwnd = nullptr;

typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                      ID3DBlob**, ID3DBlob**);
PFN_Compile g_compile = nullptr;
std::mutex g_compileMx;

struct PoolKey {
    int w, h;
    DXGI_FORMAT f;
    bool operator<(const PoolKey& o) const { return std::tie(w, h, f) < std::tie(o.w, o.h, o.f); }
};
std::map<PoolKey, std::vector<Texture*>> g_pool;
size_t g_poolBytes = 0;

size_t bytesOf(int w, int h, DXGI_FORMAT f) { return (size_t)w * h * (f == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4); }

ComPtr<IWICImagingFactory> wic() {
    static ComPtr<IWICImagingFactory> f;
    if (!f) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}

void createBackbuffer() {
    ComPtr<ID3D11Texture2D> bb;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) g_dev->CreateRenderTargetView(bb.Get(), nullptr, &g_backRTV);
}
}  // namespace

ID3D11Device* dev() { return g_dev.Get(); }
ID3D11DeviceContext* ctx() { return g_ctx.Get(); }
std::string adapterName() { return g_adapter; }
int64_t adapterVramMB() { return g_vram; }

bool init(HWND hwnd) {
    g_hwnd = hwnd;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, want, 3, D3D11_SDK_VERSION, &sd,
                                               &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, want, 3, D3D11_SDK_VERSION, &sd, &g_swap,
                                           &g_dev, &fl, &g_ctx);
    }
    if (FAILED(hr)) { logf("D3D11 init failed 0x%08x", (unsigned)hr); return false; }
    {
        ComPtr<IDXGIDevice> dxd;
        ComPtr<IDXGIAdapter> ad;
        if (SUCCEEDED(g_dev.As(&dxd)) && SUCCEEDED(dxd->GetAdapter(&ad))) {
            DXGI_ADAPTER_DESC d;
            ad->GetDesc(&d);
            g_adapter = narrow(d.Description);
            g_vram = (int64_t)(d.DedicatedVideoMemory / (1024 * 1024));
        }
        ComPtr<IDXGIFactory> fac;
        if (ad && SUCCEEDED(ad->GetParent(IID_PPV_ARGS(&fac)))) fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    createBackbuffer();

    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    if (m) g_compile = (PFN_Compile)GetProcAddress(m, "D3DCompile");
    if (!g_compile) logf("d3dcompiler_47.dll not found - effects disabled");

    const char* vs = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID) {
    VSOut o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
})";
    std::vector<uint8_t> bc;
    std::string err;
    if (compile(vs, "main", "vs_5_0", bc, err)) g_dev->CreateVertexShader(bc.data(), bc.size(), nullptr, &g_vsFull);
    else logf("VS compile failed: %s", err.c_str());

    D3D11_SAMPLER_DESC s = {};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->CreateSamplerState(&s, &g_sampLinear);
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    g_dev->CreateSamplerState(&s, &g_sampPoint);
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    g_dev->CreateSamplerState(&s, &g_sampWrap);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&bd, &g_blendNone);
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    g_dev->CreateBlendState(&bd, &g_blendOver);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    g_dev->CreateRasterizerState(&rd, &g_raster);

    for (int i = 0; i < 2; i++) {
        D3D11_BUFFER_DESC b = {};
        b.ByteWidth = 4096;
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev->CreateBuffer(&b, nullptr, &g_cb[i]);
    }
    logf("GPU: %s (%lld MB)", g_adapter.c_str(), (long long)g_vram);
    return true;
}

void shutdown() {
    for (auto& kv : g_pool)
        for (auto* t : kv.second) delete t;
    g_pool.clear();
    g_backRTV.Reset();
    g_swap.Reset();
    g_ctx.Reset();
    g_dev.Reset();
}

void resize(int w, int h) {
    if (!g_swap || w <= 0 || h <= 0) return;
    if (w == g_bbW && h == g_bbH) return;
    g_bbW = w;
    g_bbH = h;
    g_backRTV.Reset();
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    createBackbuffer();
}

void beginBackbuffer(float r, float g, float b) {
    float c[4] = {r, g, b, 1};
    g_ctx->OMSetRenderTargets(1, g_backRTV.GetAddressOf(), nullptr);
    g_ctx->ClearRenderTargetView(g_backRTV.Get(), c);
}

void present(bool vsync) { g_swap->Present(vsync ? 1 : 0, 0); }

TexPtr createTexture(int w, int h, DXGI_FORMAT fmt, bool renderTarget, const void* data, int pitch) {
    auto t = std::make_shared<Texture>();
    w = std::max(1, w);
    h = std::max(1, h);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = (UINT)w;
    d.Height = (UINT)h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = data;
    sd.SysMemPitch = (UINT)pitch;
    if (FAILED(g_dev->CreateTexture2D(&d, data ? &sd : nullptr, &t->tex))) return nullptr;
    g_dev->CreateShaderResourceView(t->tex.Get(), nullptr, &t->srv);
    if (renderTarget) g_dev->CreateRenderTargetView(t->tex.Get(), nullptr, &t->rtv);
    t->w = w;
    t->h = h;
    t->fmt = fmt;
    return t;
}

void upload(Texture& t, const void* data, int pitch) {
    g_ctx->UpdateSubresource(t.tex.Get(), 0, nullptr, data, (UINT)pitch, 0);
}

TexPtr rt(int w, int h, DXGI_FORMAT fmt) {
    w = std::max(1, w);
    h = std::max(1, h);
    PoolKey k{w, h, fmt};
    auto& lst = g_pool[k];
    Texture* raw = nullptr;
    if (!lst.empty()) {
        raw = lst.back();
        lst.pop_back();
        g_poolBytes -= bytesOf(w, h, fmt);
    } else {
        auto t = createTexture(w, h, fmt, true);
        if (!t) return nullptr;
        raw = new Texture(*t);
        raw->pooled = true;
    }
    return TexPtr(raw, [](Texture* p) {
        if (!g_dev) { delete p; return; }
        g_pool[PoolKey{p->w, p->h, p->fmt}].push_back(p);
        g_poolBytes += bytesOf(p->w, p->h, p->fmt);
    });
}

void trimPool(size_t keepBytes) {
    if (g_poolBytes <= keepBytes) return;
    for (auto& kv : g_pool) {
        while (!kv.second.empty() && g_poolBytes > keepBytes) {
            Texture* t = kv.second.back();
            kv.second.pop_back();
            g_poolBytes -= bytesOf(t->w, t->h, t->fmt);
            delete t;
        }
    }
}
size_t poolBytes() { return g_poolBytes; }

void clear(Texture& t, float r, float g, float b, float a) {
    float c[4] = {r, g, b, a};
    if (t.rtv) g_ctx->ClearRenderTargetView(t.rtv.Get(), c);
}

bool compile(const std::string& src, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& err) {
    uint64_t h = fnv1a(src.data(), src.size());
    h = fnv1a(entry, strlen(entry), h);
    h = fnv1a(target, strlen(target), h);
    std::string cachePath = joinPath(joinPath(localDataDir(), "ShaderCache"), strf("%016llx.cso", (unsigned long long)h));
    std::string cached;
    if (readFile(cachePath, cached) && !cached.empty()) {
        out.assign(cached.begin(), cached.end());
        return true;
    }
    if (!g_compile) { err = "d3dcompiler_47.dll missing"; return false; }
    ComPtr<ID3DBlob> code, errs;
    HRESULT hr;
    {
        std::lock_guard<std::mutex> lk(g_compileMx);
        hr = g_compile(src.data(), src.size(), "gese", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL2, 0, &code, &errs);
    }
    if (FAILED(hr)) {
        err = errs ? std::string((const char*)errs->GetBufferPointer(), errs->GetBufferSize()) : "compile failed";
        return false;
    }
    out.assign((const uint8_t*)code->GetBufferPointer(), (const uint8_t*)code->GetBufferPointer() + code->GetBufferSize());
    writeFileBin(cachePath, out.data(), out.size());
    return true;
}

ComPtr<ID3D11PixelShader> compilePS(const std::string& src, std::string* err, const char* entry) {
    std::vector<uint8_t> bc;
    std::string e;
    ComPtr<ID3D11PixelShader> ps;
    if (!compile(src, entry, "ps_5_0", bc, e)) {
        if (err) *err = e;
        return ps;
    }
    g_dev->CreatePixelShader(bc.data(), bc.size(), nullptr, &ps);
    return ps;
}

static void setCB(int slot, const void* data, size_t size) {
    if (!data || !size) return;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(g_cb[slot].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, data, std::min<size_t>(size, 4096));
        g_ctx->Unmap(g_cb[slot].Get(), 0);
    }
}

void pass(Texture& target, ID3D11PixelShader* ps, std::initializer_list<ID3D11ShaderResourceView*> srvs, const void* cb0,
          size_t cb0Size, const void* cb1, size_t cb1Size, bool blendOver) {
    if (!ps || !target.rtv) return;
    setCB(0, cb0, cb0Size);
    setCB(1, cb1, cb1Size);
    D3D11_VIEWPORT vp = {0, 0, (float)target.w, (float)target.h, 0, 1};
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->RSSetState(g_raster.Get());
    g_ctx->OMSetRenderTargets(1, target.rtv.GetAddressOf(), nullptr);
    float bf[4] = {0, 0, 0, 0};
    g_ctx->OMSetBlendState(blendOver ? g_blendOver.Get() : g_blendNone.Get(), bf, 0xffffffff);
    g_ctx->OMSetDepthStencilState(nullptr, 0);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vsFull.Get(), nullptr, 0);
    g_ctx->PSSetShader(ps, nullptr, 0);
    ID3D11SamplerState* ss[3] = {g_sampLinear.Get(), g_sampPoint.Get(), g_sampWrap.Get()};
    g_ctx->PSSetSamplers(0, 3, ss);
    ID3D11ShaderResourceView* arr[8] = {};
    UINT n = 0;
    for (auto* s : srvs) if (n < 8) arr[n++] = s;
    g_ctx->PSSetShaderResources(0, 8, arr);
    ID3D11Buffer* cbs[2] = {g_cb[0].Get(), g_cb[1].Get()};
    g_ctx->PSSetConstantBuffers(0, 2, cbs);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[8] = {};
    g_ctx->PSSetShaderResources(0, 8, nulls);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

bool readback(Texture& src, std::vector<uint8_t>& out) {
    if (src.fmt != DXGI_FORMAT_R8G8B8A8_UNORM) return false;
    if (!g_staging || g_stagW != src.w || g_stagH != src.h) {
        g_staging.Reset();
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = (UINT)src.w;
        d.Height = (UINT)src.h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_staging))) return false;
        g_stagW = src.w;
        g_stagH = src.h;
    }
    g_ctx->CopyResource(g_staging.Get(), src.tex.Get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(g_staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    out.resize((size_t)src.w * src.h * 4);
    for (int y = 0; y < src.h; y++) memcpy(out.data() + (size_t)y * src.w * 4, (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)src.w * 4);
    g_ctx->Unmap(g_staging.Get(), 0);
    return true;
}

bool readPixel(Texture& src, int x, int y, float rgba[4]) {
    if (x < 0 || y < 0 || x >= src.w || y >= src.h) return false;
    if (!g_staging1) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = 1;
        d.Height = 1;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = kWorkFormat;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_staging1))) return false;
    }
    if (src.fmt != kWorkFormat) return false;
    D3D11_BOX box = {(UINT)x, (UINT)y, 0, (UINT)x + 1, (UINT)y + 1, 1};
    g_ctx->CopySubresourceRegion(g_staging1.Get(), 0, 0, 0, 0, src.tex.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(g_staging1.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const uint16_t* h = (const uint16_t*)m.pData;
    for (int i = 0; i < 4; i++) {
        uint16_t v = h[i];
        uint32_t sign = (v >> 15) & 1, ex = (v >> 10) & 31, man = v & 1023;
        float f = ex == 0 ? ldexpf((float)man, -24) : ldexpf((float)(man | 1024), (int)ex - 25);
        rgba[i] = sign ? -f : f;
    }
    g_ctx->Unmap(g_staging1.Get(), 0);
    return true;
}

void premultiply(std::vector<uint8_t>& p) {
    for (size_t i = 0; i + 3 < p.size(); i += 4) {
        unsigned a = p[i + 3];
        if (a == 255) continue;
        p[i] = (uint8_t)((p[i] * a + 127) / 255);
        p[i + 1] = (uint8_t)((p[i + 1] * a + 127) / 255);
        p[i + 2] = (uint8_t)((p[i + 2] * a + 127) / 255);
    }
}

static bool decodeFrame(IWICBitmapFrameDecode* frame, std::vector<uint8_t>& rgba, int& w, int& h) {
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic()->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw, uh;
    conv->GetSize(&uw, &uh);
    w = (int)uw;
    h = (int)uh;
    rgba.resize((size_t)w * h * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, (UINT)w * 4, (UINT)rgba.size(), rgba.data()));
}

bool loadImageFile(const std::string& path, std::vector<uint8_t>& rgba, int& w, int& h) {
    auto f = wic();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(widen(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    return decodeFrame(frame.Get(), rgba, w, h);
}

bool decodeImageMemory(const void* data, size_t n, std::vector<uint8_t>& rgba, int& w, int& h) {
    auto f = wic();
    if (!f) return false;
    ComPtr<IWICStream> st;
    if (FAILED(f->CreateStream(&st))) return false;
    if (FAILED(st->InitializeFromMemory((BYTE*)data, (DWORD)n))) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    return decodeFrame(frame.Get(), rgba, w, h);
}

static bool encodePngTo(IStream* stream, const uint8_t* rgba, int w, int h) {
    auto f = wic();
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return false;
    if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&fr, &props))) return false;
    fr->Initialize(props.Get());
    fr->SetSize((UINT)w, (UINT)h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    if (fmt != GUID_WICPixelFormat32bppRGBA) {
        // encoder wants BGRA: swizzle
        std::vector<uint8_t> tmp(rgba, rgba + (size_t)w * h * 4);
        for (size_t i = 0; i < tmp.size(); i += 4) std::swap(tmp[i], tmp[i + 2]);
        fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)tmp.size(), tmp.data());
    } else {
        fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)((size_t)w * h * 4), (BYTE*)rgba);
    }
    fr->Commit();
    return SUCCEEDED(enc->Commit());
}

bool savePNG(const std::string& path, const uint8_t* rgba, int w, int h) {
    auto f = wic();
    if (!f) return false;
    makeDirs(parentDir(path));
    ComPtr<IWICStream> st;
    if (FAILED(f->CreateStream(&st))) return false;
    if (FAILED(st->InitializeFromFilename(widen(path).c_str(), GENERIC_WRITE))) return false;
    return encodePngTo(st.Get(), rgba, w, h);
}

bool encodePNG(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out) {
    ComPtr<IStream> ms;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &ms))) return false;
    if (!encodePngTo(ms.Get(), rgba, w, h)) return false;
    STATSTG stg;
    ms->Stat(&stg, STATFLAG_NONAME);
    out.resize((size_t)stg.cbSize.QuadPart);
    LARGE_INTEGER z = {};
    ms->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG rd = 0;
    ms->Read(out.data(), (ULONG)out.size(), &rd);
    return rd == out.size();
}

TexPtr loadTextureFile(const std::string& path, bool premul) {
    std::vector<uint8_t> px;
    int w, h;
    if (!loadImageFile(path, px, w, h)) return nullptr;
    if (premul) premultiply(px);
    return createTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false, px.data(), w * 4);
}
}  // namespace gfx
