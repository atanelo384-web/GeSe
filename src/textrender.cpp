#include "textrender.h"
#include <d2d1.h>
#include <dwrite_3.h>

uint64_t TextStyle::hash() const {
    uint64_t h = fnv1a(text.data(), text.size());
    h = fnv1a(font.data(), font.size(), h);
    float nums[] = {size, tracking, leading, strokeW, fill[0], fill[1], fill[2], fill[3], stroke[0], stroke[1], stroke[2], stroke[3]};
    h = fnv1a(nums, sizeof(nums), h);
    int flags[] = {bold, italic, caps, align};
    return fnv1a(flags, sizeof(flags), h);
}

namespace textr {
namespace {
ComPtr<ID2D1Factory> g_d2d;
ComPtr<IDWriteFactory> g_dw;
struct Entry {
    TexPtr tex;
    uint64_t used;
};
std::unordered_map<uint64_t, Entry> g_cache;
uint64_t g_tick = 0;
// fonts shipped with the app (Main\Fonts) - usable even when not installed in Windows
ComPtr<IDWriteFontCollection1> g_appFonts;
std::vector<std::string> g_appFamilies;

std::string familyName(IDWriteFontFamily* fam) {
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(fam->GetFamilyNames(&names))) return {};
    UINT32 idx = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &idx, &exists);
    if (!exists) idx = 0;
    UINT32 len = 0;
    names->GetStringLength(idx, &len);
    std::wstring w(len + 1, 0);
    names->GetString(idx, w.data(), len + 1);
    w.resize(len);
    return narrow(w);
}

void loadAppFonts() {
    ComPtr<IDWriteFactory5> f5;
    if (FAILED(g_dw.As(&f5))) return;
    ComPtr<IDWriteFontSetBuilder1> b;
    if (FAILED(f5->CreateFontSetBuilder(&b))) return;
    std::string dir = resPath("Fonts");
    int added = 0;
    for (const char* ext : {".otf", ".ttf"})
        for (auto& f : listFiles(dir, ext, false)) {
            ComPtr<IDWriteFontFile> ff;
            if (SUCCEEDED(f5->CreateFontFileReference(widen(f).c_str(), nullptr, &ff)) && SUCCEEDED(b->AddFontFile(ff.Get()))) added++;
        }
    if (!added) return;
    ComPtr<IDWriteFontSet> set;
    if (FAILED(b->CreateFontSet(&set))) return;
    if (FAILED(f5->CreateFontCollectionFromFontSet(set.Get(), &g_appFonts))) return;
    for (UINT32 i = 0; i < g_appFonts->GetFontFamilyCount(); i++) {
        ComPtr<IDWriteFontFamily1> fam;
        if (SUCCEEDED(g_appFonts->GetFontFamily(i, &fam))) g_appFamilies.push_back(familyName(fam.Get()));
    }
}

IDWriteFontCollection* collectionFor(const std::string& family) {
    for (auto& f : g_appFamilies)
        if (toLower(f) == toLower(family)) return g_appFonts.Get();
    return nullptr;
}
}  // namespace

bool init() {
    if (g_d2d) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g_dw.GetAddressOf()))) return false;
    loadAppFonts();
    for (auto& f : g_appFamilies) logf("app font: %s", f.c_str());
    return true;
}

std::string appFamilyLike(const std::string& part, const std::string& fallback) {
    for (auto& f : g_appFamilies) if (toLower(f).find(toLower(part)) != std::string::npos) return f;
    for (auto& f : systemFonts()) if (toLower(f).find(toLower(part)) != std::string::npos) return f;
    return fallback;
}

bool hasFamily(const std::string& family) {
    if (collectionFor(family)) return true;
    for (auto& f : systemFonts()) if (toLower(f) == toLower(family)) return true;
    return false;
}
void shutdown() {
    g_cache.clear();
    g_dw.Reset();
    g_d2d.Reset();
}

std::vector<std::string> systemFonts() {
    static std::vector<std::string> fonts;
    if (!fonts.empty() || !g_dw) return fonts;
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(g_dw->GetSystemFontCollection(&coll))) return fonts;
    UINT32 n = coll->GetFontFamilyCount();
    for (UINT32 i = 0; i < n; i++) {
        ComPtr<IDWriteFontFamily> fam;
        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(coll->GetFontFamily(i, &fam)) || FAILED(fam->GetFamilyNames(&names))) continue;
        UINT32 idx = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &idx, &exists);
        if (!exists) idx = 0;
        UINT32 len = 0;
        names->GetStringLength(idx, &len);
        std::wstring w(len + 1, 0);
        names->GetString(idx, w.data(), len + 1);
        w.resize(len);
        fonts.push_back(narrow(w));
    }
    for (auto& f : g_appFamilies) fonts.push_back(f);
    std::sort(fonts.begin(), fonts.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
    fonts.erase(std::unique(fonts.begin(), fonts.end()), fonts.end());
    return fonts;
}

bool measure(const TextStyle& s, float& w, float& h, float& capHeight, float* baseline) {
    if (!g_dw) return false;
    std::wstring wt = widen(s.text);
    ComPtr<IDWriteTextFormat> tf;
    g_dw->CreateTextFormat(widen(s.font).c_str(), collectionFor(s.font), s.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                           s.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size, L"en-us", &tf);
    if (!tf) return false;
    ComPtr<IDWriteTextLayout> layout;
    g_dw->CreateTextLayout(wt.c_str(), (UINT32)wt.size(), tf.Get(), 100000.f, 100000.f, &layout);
    if (!layout) return false;
    DWRITE_TEXT_METRICS m;
    layout->GetMetrics(&m);
    w = m.width;
    h = m.height;
    capHeight = s.size * 0.7f;
    if (baseline) {
        DWRITE_LINE_METRICS lm[4];
        UINT32 n = 0;
        layout->GetLineMetrics(lm, 4, &n);
        *baseline = n ? lm[0].baseline : s.size * 0.8f;
    }
    // real cap height from the font face
    ComPtr<IDWriteFontCollection> coll;
    tf->GetFontCollection(&coll);
    UINT32 idx = 0;
    BOOL exists = FALSE;
    if (coll && SUCCEEDED(coll->FindFamilyName(widen(s.font).c_str(), &idx, &exists)) && exists) {
        ComPtr<IDWriteFontFamily> fam;
        ComPtr<IDWriteFont> font;
        if (SUCCEEDED(coll->GetFontFamily(idx, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS fm;
            font->GetMetrics(&fm);
            if (fm.designUnitsPerEm) capHeight = s.size * fm.capHeight / fm.designUnitsPerEm;
        }
    }
    return true;
}

TexPtr render(const TextStyle& s, int w, int h, float scale) {
    if (!g_d2d || w <= 0 || h <= 0) return nullptr;
    uint64_t key = s.hash();
    int dims[] = {w, h};
    key = fnv1a(dims, sizeof(dims), key);
    key = fnv1a(&scale, sizeof(scale), key);
    g_tick++;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) { it->second.used = g_tick; return it->second.tex; }

    auto tex = gfx::createTexture(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, true);
    if (!tex) return nullptr;
    ComPtr<IDXGISurface> surf;
    tex->tex.As(&surf);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(g_d2d->CreateDxgiSurfaceRenderTarget(surf.Get(), &props, &rt))) return nullptr;

    std::string txt = s.text;
    if (s.caps) for (auto& c : txt) c = (char)toupper((unsigned char)c);
    std::wstring wt = widen(txt);
    float fs = std::max(1.f, s.size * scale);
    ComPtr<IDWriteTextFormat> tf;
    g_dw->CreateTextFormat(widen(s.font).c_str(), collectionFor(s.font), s.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                           s.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fs, L"en-us", &tf);
    if (!tf) return nullptr;
    tf->SetTextAlignment(s.align == 0 ? DWRITE_TEXT_ALIGNMENT_LEADING : (s.align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_CENTER));
    tf->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, fs * s.leading, fs * s.leading * 0.8f);
    ComPtr<IDWriteTextLayout> layout;
    g_dw->CreateTextLayout(wt.c_str(), (UINT32)wt.size(), tf.Get(), (float)w * 4, (float)h * 4, &layout);
    if (!layout) return nullptr;
    if (fabsf(s.tracking) > 0.01f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) {
            DWRITE_TEXT_RANGE r = {0, (UINT32)wt.size()};
            l1->SetCharacterSpacing(0, s.tracking * fs / 1000.f, 0, r);
        }
    }
    DWRITE_TEXT_METRICS m;
    layout->GetMetrics(&m);
    layout->SetMaxWidth(std::max(1.f, m.widthIncludingTrailingWhitespace));
    layout->GetMetrics(&m);
    float x0 = (w - m.width) * 0.5f - m.left;
    float y0 = (h - m.height) * 0.5f - m.top;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (s.strokeW > 0.01f) {
        ComPtr<ID2D1SolidColorBrush> sb;
        rt->CreateSolidColorBrush(D2D1::ColorF(s.stroke[0], s.stroke[1], s.stroke[2], s.stroke[3]), &sb);
        float r = s.strokeW * scale;
        int steps = clampv((int)(r * 3), 12, 48);
        for (int i = 0; i < steps; i++) {
            float a = i * 2 * kPi / steps;
            rt->DrawTextLayout(D2D1::Point2F(x0 + cosf(a) * r, y0 + sinf(a) * r), layout.Get(), sb.Get());
        }
    }
    ComPtr<ID2D1SolidColorBrush> fb;
    rt->CreateSolidColorBrush(D2D1::ColorF(s.fill[0], s.fill[1], s.fill[2], s.fill[3]), &fb);
    rt->DrawTextLayout(D2D1::Point2F(x0, y0), layout.Get(), fb.Get());
    rt->EndDraw();

    if (g_cache.size() > 48) {
        uint64_t oldest = UINT64_MAX, ok = 0;
        for (auto& kv : g_cache) if (kv.second.used < oldest) { oldest = kv.second.used; ok = kv.first; }
        g_cache.erase(ok);
    }
    g_cache[key] = {tex, g_tick};
    return tex;
}
}  // namespace textr
