// GeSe v2.4.0 - entry point, window and main loop
#include "ui.h"
#include "gfx.h"
#include "textrender.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "i18n.h"
#include "audiofx.h"
#include "ai_local.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <ctime>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static App g_appInstance;
static bool g_inSizeMove = false;
static void frame(App& a);

// Windows DPI scale, plus a boost on large monitors running at 100% (1440p and up),
// where a 1:1 UI looks tiny.
static float effectiveScale(HWND h, float dpi) {
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    float logicalH = (mi.rcMonitor.bottom - mi.rcMonitor.top) / std::max(dpi, 0.5f);
    float boost = logicalH >= 2000 ? 1.5f : (logicalH >= 1350 ? 1.25f : 1.0f);
    return dpi * boost;
}

void App::setDpi(float d) {
    dpi = effectiveScale(hwnd, d);
    uiApplyTheme(*this);
}

static void enableDarkTitle(HWND h) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = RGB(0x23, 0x23, 0x23);
    DwmSetWindowAttribute(h, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
}

static void openMediaFiles(App& a, const std::vector<std::string>& files);

static void onDrop(App& a, HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::string> files;
    for (UINT i = 0; i < n; i++) {
        wchar_t buf[MAX_PATH * 2];
        DragQueryFileW(drop, i, buf, MAX_PATH * 2);
        files.push_back(narrow(buf));
    }
    DragFinish(drop);
    if (files.empty() || a.screen == Screen::Splash) return;
    openMediaFiles(a, files);
}

// Opens .gese projects, or imports media and (in an empty project) builds a matching composition.
static void openMediaFiles(App& a, const std::vector<std::string>& files) {
    if (files.size() == 1 && fileExt(files[0]) == ".gese") {
        a.pendingOpenPath = files[0];
        a.requestAction(2);
        return;
    }
    bool fresh = !a.project;
    if (fresh) a.newProject();
    auto imported = a.importFiles(files);
    if (imported.empty()) return;
    // folders and archives only fill the Media panel (dragging 200 files onto the timeline is never wanted)
    for (auto& f : files) {
        DWORD at = GetFileAttributesW(widen(f).c_str());
        if ((at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) || toLower(fileExt(f)) == ".zip") {
            a.pProject = true;
            if (fresh) { a.resetUndo(); a.dirty = true; }
            return;
        }
    }
    Composition* c = a.comp();
    if (!c) {
        // first drop into an empty project: build a composition that matches the first clip
        Footage* f = imported.front().get();
        double dur = f->kind == FootageKind::Image ? a.settings.stillDuration : std::max(1.0, f->duration);
        c = a.newComp(fileStem(f->name), f->hasVideo ? f->w : 1920, f->hasVideo ? f->h : 1080,
                      f->hasVideo && f->kind != FootageKind::Image ? f->fps : 30, dur);
    }
    double at = c->time;
    for (auto it = imported.rbegin(); it != imported.rend(); ++it) a.addFootageLayer(*c, **it, at, 0);
    if (fresh) { a.resetUndo(); a.dirty = true; }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return 1;
    App& a = *g_app;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) gfx::resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_ENTERSIZEMOVE: g_inSizeMove = true; return 0;
    case WM_EXITSIZEMOVE: g_inSizeMove = false; return 0;
    case WM_PAINT:
        if (g_inSizeMove && gfx::dev()) { frame(a); ValidateRect(h, nullptr); return 0; }
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        if (a.screen != Screen::Splash) { mm->ptMinTrackSize.x = (LONG)(960 * a.dpi); mm->ptMinTrackSize.y = (LONG)(600 * a.dpi); }
        return 0;
    }
    case WM_DROPFILES: onDrop(a, (HDROP)wp); return 0;
    case WM_DPICHANGED: {
        a.setDpi(HIWORD(wp) / 96.0f);
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu beep
        break;
    case WM_CLOSE:
        if (a.screen == Screen::Splash) return 0;
        if (a.exporter.active()) { a.notify("A render is running - stop it in the Render Queue first", true); return 0; }
        a.requestAction(3);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int g_showPending = 0;

// The splash window becomes the main window. It is hidden while it changes style and size and
// shown again only after a frame of the new screen was presented, so no stretched splash flashes.
static void switchToMainWindow(App& a) {
    ShowWindow(a.hwnd, SW_HIDE);
    SetWindowLongPtrW(a.hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    SetWindowRgn(a.hwnd, nullptr, FALSE);
    int pref = 0;  // DWMWCP_DEFAULT
    DwmSetWindowAttribute(a.hwnd, 33, &pref, sizeof(pref));
    enableDarkTitle(a.hwnd);
    HMONITOR mon = MonitorFromWindow(a.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    RECT w = mi.rcWork;
    int ww = (int)((w.right - w.left) * 0.86), wh = (int)((w.bottom - w.top) * 0.86);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    wp.showCmd = SW_HIDE;
    wp.rcNormalPosition = {w.left + (w.right - w.left - ww) / 2, w.top + (w.bottom - w.top - wh) / 2, 0, 0};
    wp.rcNormalPosition.right = wp.rcNormalPosition.left + ww;
    wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + wh;
    SetWindowPlacement(a.hwnd, &wp);
    // occupy the work area while hidden so the first frame already has the final size
    SetWindowPos(a.hwnd, nullptr, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(a.hwnd, L"GeSe v2.4.0");
    g_showPending = 2;
}

static void frame(App& a) {
    static Screen lastScreen = Screen::Splash;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    a.updatePlayback();
    if (a.screen == Screen::Editor) renderViewerIfNeeded(a);
    switch (a.screen) {
    case Screen::Splash: uiSplash(a); break;
    case Screen::Home: uiHome(a); break;
    case Screen::Editor:
        if (!a.project) a.screen = Screen::Home;
        else uiEditor(a);
        break;
    }
    a.tickRender();
    a.tickAi();
    a.pollAudioFx();
    a.autosave();
    a.commitIfIdle();
    // window title
    if (a.screen == Screen::Editor && a.project) {
        static std::string lastTitle;
        std::string t = "GeSe v2.4.0 - " + a.project->displayName() + (a.dirty ? " *" : "");
        if (t != lastTitle) { SetWindowTextW(a.hwnd, widen(t).c_str()); lastTitle = t; }
    }
    ImGui::Render();
    gfx::beginBackbuffer(0.137f, 0.137f, 0.137f);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    gfx::present(true);
    if (lastScreen == Screen::Splash && a.screen != Screen::Splash) switchToMainWindow(a);
    else if (g_showPending > 0 && --g_showPending == 0) {
        ShowWindow(a.hwnd, SW_SHOWMAXIMIZED);
        SetForegroundWindow(a.hwnd);
    }
    lastScreen = a.screen;
    gfx::trimPool((size_t)768 * 1024 * 1024);
    static double lastCompTime = -1;
    Composition* pc = a.comp();
    if (a.playing && pc) {
        if (pc->time != lastCompTime) { a.framesShown++; lastCompTime = pc->time; }
        double now = nowSec();
        if (now - a.fpsTimer > 0.5) {
            a.measuredFps = (float)(a.framesShown / (now - a.fpsTimer));
            a.framesShown = 0;
            a.fpsTimer = now;
        }
    } else {
        a.fpsTimer = nowSec();
        a.framesShown = 0;
    }
}

// --check-effects: compile every effect and write a report (used by the build)
static int checkEffects() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {sizeof(wc), 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"GeSeCheck", nullptr};
    RegisterClassExW(&wc);
    HWND h = CreateWindowW(L"GeSeCheck", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!gfx::init(h)) return 2;
    EffectRegistry reg;
    reg.reload();
    std::string rep;
    int bad = 0;
    std::map<std::string, int> cats;
    for (auto& d : reg.defs) {
        cats[d->category]++;
        if (!d->ok) { bad++; rep += "FAIL  " + d->category + " / " + d->name + " : " + d->error + "\n"; }
    }
    rep += strf("\n%d effects, %d failed\n", (int)reg.defs.size(), bad);
    for (auto& kv : cats) rep += strf("  %-20s %d\n", kv.first.c_str(), kv.second);
    Renderer r;
    rep += r.init(&reg) ? "core shaders OK\n" : "core shaders FAILED\n";
    writeFile(joinPath(exeDir(), "effects_report.txt"), rep);
    gfx::shutdown();
    return bad ? 1 : 0;
}

// --selftest <media> <outdir>: builds a project with effects, renders a frame and exports videos
static int selfTest(const std::string& media, const std::string& outDir) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {sizeof(wc), 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"GeSeTest", nullptr};
    RegisterClassExW(&wc);
    HWND h = CreateWindowW(L"GeSeTest", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!gfx::init(h)) return 2;
    textr::init();
    App& a = g_appInstance;
    g_app = &a;
    a.hwnd = h;
    std::string rep;
    auto ok = [&](bool c, const std::string& what) { rep += (c ? "OK    " : "FAIL  ") + what + "\n"; return c; };
    a.effects.reload();
    ok(a.renderer.init(&a.effects), "renderer init");
    a.newProject();
    FootagePtr f = a.importFile(media);
    if (!ok(f != nullptr, "import " + media)) { writeFile(joinPath(outDir, "selftest_report.txt"), rep); return 1; }
    ok(f->hasVideo && f->w > 0, strf("video %dx%d %.3f fps %.2fs codec %s", f->w, f->h, f->fps, f->duration, f->codec.c_str()));
    ok(f->hasAudio, "audio track " + f->audioCodec);
    for (int i = 0; i < 300 && f->audio && !f->audio->ready; i++) Sleep(20);
    Composition* c = a.newComp("Selftest", 1920, 1080, 30, 3);
    Layer* v = a.addFootageLayer(*c, *f, 0, 0);
    auto add = [&](Layer* l, const char* id) -> EffectInst* {
        const EffectDef* d = a.effects.find(id);
        if (!ok(d && d->ok, std::string("effect ") + id)) return nullptr;
        l->effects.push_back(a.effects.instantiate(*d));
        return &l->effects.back();
    };
    if (EffectInst* e = add(v, "gese.motion.oscillation")) {
        e->intensity.setAnimated(true, 0);
        float z[4] = {0, 0, 0, 0}, full[4] = {100, 0, 0, 0};
        e->intensity.addKey(0, z);
        e->intensity.addKey(1.5, full);
        e->intensity.keys[0].out = Interp::Ease;
    }
    add(v, "gese.motion.tile");
    add(v, "gese.sty.glow");
    if (EffectInst* e = add(v, "gese.filter.mono")) {
        e->intensity.setAnimated(true, 0);
        float a0[4] = {100, 0, 0, 0}, a1[4] = {0, 0, 0, 0};
        e->intensity.addKey(0, a0);
        e->intensity.addKey(2, a1);
    }
    v->motionBlur = true;
    Layer* t = a.addText(*c, "GESE SELFTEST", ImVec2(960, 540));
    t->text.font = textr::appFamilyLike("akira", "Arial");
    t->text.bold = false;
    add(t, "gese.persp.dropshadow");
    Layer* adj = a.addEffectLayer(*c, a.effects.find("gese.nz.grain"), 0, 0);
    adj->outPoint = 3;
    add(adj, "gese.tr.zoom");
    a.selLayers = {v->id};
    a.splitSelectedLayers(2.0);
    ok(c->layers.size() == 4, strf("layers after split: %d", (int)c->layers.size()));
    // project round trip
    std::string saved = a.project->save().dump();
    Project p2;
    std::string err;
    ok(p2.load(Json::parse(saved), err) && p2.comps.size() == 1 && p2.comps[0]->layers.size() == 4, "save / load round trip");
    writeFile(joinPath(outDir, "selftest.gese"), saved);
    // render a frame
    RenderParams rp;
    rp.sync = true;
    double t0 = nowSec();
    TexPtr img = a.renderer.renderComp(*a.project, *c, 1.2, rp);
    TexPtr o = img ? a.renderer.finalizeRGBA8(*img, c->bg, false) : nullptr;
    std::vector<uint8_t> px;
    bool frameOk = o && gfx::readback(*o, px) && gfx::savePNG(joinPath(outDir, "selftest_frame.png"), px.data(), o->w, o->h);
    ok(frameOk, strf("render frame 1.2s (%.1f ms, %d passes)", (nowSec() - t0) * 1000, a.renderer.lastPasses));
    // exports
    struct Fmt { int format, encoder; const char* name; };
    for (Fmt fm : {Fmt{0, 0, "h264.mp4"}, Fmt{0, 1, "nvenc.mp4"}, Fmt{3, 0, "prores4444.mov"}, Fmt{5, 0, "vp9.webm"}}) {
        RenderItem it;
        it.id = newId();
        it.comp = c->id;
        it.out.format = fm.format;
        it.out.encoder = fm.encoder;
        it.out.quality = fm.format == 5 ? 32 : 20;
        it.out.range = 1;
        it.out.path = joinPath(outDir, std::string("selftest_") + fm.name);
        a.project->queue.push_back(it);
        double s0 = nowSec();
        bool started = a.exporter.start(*a.project, a.project->queue.back(), a.renderer, err);
        if (!ok(started, std::string("export start ") + fm.name + (started ? "" : ": " + err))) continue;
        while (a.exporter.step(1.0)) {}
        int64_t sz = fileSize(it.out.path);
        ok(a.exporter.lastOk && sz > 1000, strf("export %s: %s, %s, %.1fs", fm.name, a.exporter.lastMessage.c_str(), fmtBytes(sz).c_str(), nowSec() - s0));
    }
    // freeze frame: split + held frame + rest shifted
    {
        a.selLayers.clear();
        size_t before = c->layers.size();
        double durBefore = c->duration;
        a.freezeFrame(1.0);
        Layer* fz = nullptr;
        for (auto& l : c->layers) if (l->name.find("(Freeze)") != std::string::npos) fz = l.get();
        ok(fz && c->layers.size() == before + 2 && c->duration > durBefore + 2.9, strf("freeze frame: %d -> %d layers, %.1fs -> %.1fs", (int)before, (int)c->layers.size(), durBefore, c->duration));
        if (fz) {
            TexPtr f1 = a.renderer.renderComp(*a.project, *c, 1.5, rp);
            ok(f1 != nullptr, "render frozen frame");
        }
    }
    // graph editor curves
    {
        float ez[4] = {0.42f, 0, 0.58f, 1}, back[4] = {0.34f, 1.56f, 0.64f, 1};
        float mid = curveValue(Curve::Bezier, ez, 0.5f), over = 0;
        for (int i = 0; i <= 50; i++) over = std::max(over, curveValue(Curve::Bezier, back, i / 50.f));
        ok(fabsf(mid - 0.5f) < 0.01f && over > 1.05f && fabsf(curveValue(Curve::Bounce, ez, 1) - 1) < 1e-3f,
           strf("curves: ease-in-out(0.5)=%.3f, back-out peak %.3f", mid, over));
        Prop pp(1, 0);
        pp.setAnimated(true, 0);
        float v0[4] = {0}, v1[4] = {100};
        pp.addKey(0, v0);
        pp.addKey(1, v1);
        pp.keys[0].curve = Curve::Bezier;
        memcpy(pp.keys[0].bz, ez, sizeof(ez));
        Prop pl;
        pl.load(pp.save());
        ok(pl.keys.size() == 2 && pl.keys[0].curve == Curve::Bezier && fabsf(pl.f(0.5) - 50) < 1, strf("curve keyframe save/load, value at 0.5s = %.2f", pl.f(0.5)));
    }
    // voice & speed: slow down keeping the pitch, pitch on its own, presets, clean-up
    {
        const int n = 96000;
        std::vector<float> sine((size_t)n * 2);
        for (int i = 0; i < n; i++) sine[i * 2] = sine[i * 2 + 1] = 0.5f * sinf(2 * kPi * 440.f * i / 48000.f);
        auto freq = [](const std::vector<float>& s) {
            int64_t frames = (int64_t)s.size() / 2, a0 = frames / 4, a1 = frames * 3 / 4, zc = 0;
            for (int64_t i = a0 + 1; i < a1; i++) if ((s[i * 2 - 2] < 0) != (s[i * 2] < 0)) zc++;
            return zc / 2.0 / ((a1 - a0) / 48000.0);
        };
        AudioFxParams p;
        std::vector<float> out;
        std::string e;
        p.tempo = 0.5;
        bool r1 = audioFxRender(sine.data(), n, p, out, e);
        ok(r1 && out.size() / 2 == (size_t)n * 2 && fabs(freq(out) - 440) < 25, strf("slow down 50%% keeping pitch: %zu frames, %.0f Hz %s", out.size() / 2, r1 ? freq(out) : 0.0, e.c_str()));
        p.tempo = 1;
        p.semitones = 12;
        bool r2 = audioFxRender(sine.data(), n, p, out, e);
        ok(r2 && out.size() / 2 == (size_t)n && fabs(freq(out) - 880) < 45, strf("pitch +12 st at the same speed: %.0f Hz %s", r2 ? freq(out) : 0.0, e.c_str()));
        // sync: a click must stay where it was after stretching
        std::vector<float> click((size_t)n * 2, 0.f);
        for (int k = 0; k < 4; k++) click[(24000 + k) * 2] = click[(24000 + k) * 2 + 1] = 0.9f;
        p = AudioFxParams();
        p.tempo = 0.5;
        int64_t pk = -1;
        if (audioFxRender(click.data(), n, p, out, e)) pk = (int64_t)llround(energyCenter(out));
        ok(pk >= 0 && llabs(pk - 48000) < 960, strf("stretched audio stays in sync: click at %lld (expected 48000, %.1f ms off)", (long long)pk, (pk - 48000) / 48.0));
        int bad = 0;
        std::string badNames;
        for (int v = 1; v < kVoicePresetCount; v++) {
            p = AudioFxParams();
            p.voice = v;
            if (!audioFxRender(sine.data(), n / 2, p, out, e) || out.size() / 2 != (size_t)(n / 2 * pow(1, 1))) { bad++; badNames += std::string(" ") + kVoicePresets[v].name + "(" + e + ")"; }
        }
        ok(bad == 0, strf("%d voice presets render%s", kVoicePresetCount - 1, badNames.c_str()));
        p = AudioFxParams();
        p.denoise = p.enhance = true;
        ok(audioFxRender(sine.data(), n, p, out, e) && out.size() / 2 == (size_t)n, "noise reduction + voice enhance " + e);
        // the playback mixer: "pitch follows speed" (like a record) vs "keep pitch" (time-stretched)
        {
            auto sd = std::make_shared<AudioData>();
            sd->samples.resize((size_t)48000 * 4 * 2);
            for (int i = 0; i < 48000 * 4; i++) sd->samples[i * 2] = sd->samples[i * 2 + 1] = 0.5f * sinf(2 * kPi * 440.f * i / 48000.f);
            sd->ready = true;
            auto mixFreq = [&](std::shared_ptr<AudioData> d, double speed, double offset) {
                AudioClip ac;
                ac.data = d;
                ac.start = 0;
                ac.in = 0;
                ac.out = 8;
                ac.speed = speed;
                ac.dataOffset = offset;
                std::vector<float> buf((size_t)48000 * 2);
                mixAudio({ac}, 0.5, 48000, 48000, buf.data(), 1.f);
                return freq(buf);
            };
            double slow = mixFreq(sd, 0.5, 0), fast = mixFreq(sd, 2.0, 0);
            ok(fabs(slow - 220) < 12 && fabs(fast - 880) < 45, strf("pitch follows speed: 0.5x -> %.0f Hz, 2x -> %.0f Hz (from 440 Hz)", slow, fast));
            AudioFxParams kp;
            kp.tempo = 0.5;
            auto st = audioFxGet(sd, 0, 4, kp, true);
            double kept = st ? mixFreq(st, 1.0, 0) : 0;
            ok(st && fabs(kept - 440) < 22, strf("keep pitch: 0.5x speed still %.0f Hz", kept));
        }
        // audio files
        for (const char* ext : {"wav", "mp3", "m4a", "flac"}) {
            std::string fp = joinPath(outDir, std::string("selftest_audio.") + ext);
            bool s = saveAudioFile(fp, sine.data(), n, e);
            ok(s && fileSize(fp) > 1000, strf("save audio .%s: %s %s", ext, fmtBytes(fileSize(fp)).c_str(), e.c_str()));
        }
    }
    // clip audio: voice preset through the mixer, extract to its own layer
    {
        c = a.comp();
        a.project->path = joinPath(outDir, "selftest.gese");
        Layer* vl = nullptr;
        for (auto& l : c->layers) if (l->type == LayerType::Footage && l->source == f->id) vl = l.get();
        if (ok(vl != nullptr, "footage layer with audio")) {
            vl->voice = 1;
            vl->keepPitch = true;
            vl->speed = 0.75;
            auto clips = collectAudioClips(*a.project, *c, true, vl->id);
            ok(!clips.empty() && clips[0].data != f->audio && clips[0].speed == 1, strf("voice + keep-pitch speed used for playback/export (%d clip)", (int)clips.size()));
            vl->speed = 1;
            size_t before = c->layers.size();
            a.extractAudio(*vl);
            ok(c->layers.size() == before + 1 && !vl->audioOn, strf("extract audio: %d -> %d layers, video muted", (int)before, (int)c->layers.size()));
        }
    }
    // built-in AI background removal (BiRefNet, GPU via DirectML)
    {
        FramePtr fr = f->media ? f->media->decodeSync(0, 0) : nullptr;
        std::vector<uint8_t> alpha;
        std::string e;
        a.renderer.clearCaches();  // same as App::startAi: free cached frames / render targets first
        gfx::trimPool(0);
        localNewJob();
        double s0 = nowSec();
        bool r = fr && localMatte(fr->rgba.data(), fr->w, fr->h, alpha, true, e);
        double first = nowSec() - s0;
        s0 = nowSec();
        if (r) r = localMatte(fr->rgba.data(), fr->w, fr->h, alpha, true, e);
        double second = nowSec() - s0;
        double mean = 0;
        for (uint8_t v : alpha) mean += v;
        if (!alpha.empty()) mean /= alpha.size();
        ok(r && alpha.size() == (size_t)fr->w * fr->h, strf("AI matte %dx%d on %s: first %.2fs, next %.2fs, mean alpha %.0f %s", fr ? fr->w : 0, fr ? fr->h : 0,
                                                             localDeviceName().c_str(), first, second, mean, e.c_str()));
        if (r) {
            std::vector<uint8_t> px = fr->rgba;
            for (size_t k = 0; k < alpha.size(); k++) px[k * 4 + 3] = alpha[k];
            gfx::savePNG(joinPath(outDir, "selftest_cutout.png"), px.data(), fr->w, fr->h);
        }
        // the way the editor runs it: a job in a child GeSe process
        localRelease();
        AiJobDesc d;
        d.provider = 0;
        d.gpu = true;
        d.footage = f;
        d.frames = {0, 15, 30};
        d.outDir = joinPath(outDir, "ai_job");
        d.baseName = "selftest";
        makeDirs(d.outDir);
        double j0 = nowSec();
        bool started = a.ai.start(d, e);
        while (started && a.ai.running()) Sleep(50);
        std::vector<std::string> files;
        AiJobDesc back;
        std::string je;
        a.ai.takeResult(back, files, je);
        ok(started && files.size() == 3 && je.empty(), strf("AI job in a child process: %d of 3 frames, %.1fs, %s %s", (int)files.size(), nowSec() - j0,
                                                             a.ai.status().c_str(), je.c_str()));
    }
    // 3D: a 3D layer at z = 0 with the default camera looks exactly like the 2D one; camera, lights, depth sort
    {
        c = a.comp();
        const float cardCol[4] = {0.8f, 0.3f, 0.1f, 1};
        Layer* s3 = a.addSolid(*c, "3D Card", 800, 600, cardCol);
        Mat3 m2 = a.project->layerMatrix(*c, *s3, 1.0);
        s3->threeD = true;
        Mat3 m3 = a.project->layerMatrix(*c, *s3, 1.0);
        ImVec2 p2 = m2.apply(ImVec2(0, 0)), p3 = m3.apply(ImVec2(0, 0)), q2 = m2.apply(ImVec2(800, 600)), q3 = m3.apply(ImVec2(800, 600));
        ok(len2(p2 - p3) < 0.05f && len2(q2 - q3) < 0.05f, strf("3D layer at z=0 = 2D layer: corner (%.2f, %.2f) vs (%.2f, %.2f)", p3.x, p3.y, p2.x, p2.y));
        s3->rotY.v[0] = 50;
        s3->posZ.v[0] = 300;
        Mat3 mr = a.project->layerMatrix(*c, *s3, 1.0);
        ImVec2 l0 = mr.apply(ImVec2(0, 300)), r0 = mr.apply(ImVec2(800, 300));
        ok(mr.projective() && fabsf(r0.x - l0.x) < 800 * 0.9f, strf("Y rotation + depth gives perspective: width %.0f px", r0.x - l0.x));
        Layer* cam = a.addCamera(*c);
        cam->posZ.v[0] -= 600;  // dolly back: everything smaller
        Mat3 mc = a.project->layerMatrix(*c, *s3, 1.0);
        ImVec2 l1 = mc.apply(ImVec2(0, 300)), r1 = mc.apply(ImVec2(800, 300));
        ok(fabsf(r1.x - l1.x) < fabsf(r0.x - l0.x), strf("camera dolly back: %.0f -> %.0f px", r0.x - l0.x, r1.x - l1.x));
        a.addLight(*c, LK_Spot);
        a.addLight(*c, LK_Ambient)->lightIntensity.v[0] = 25;
        cam->dof = true;
        cam->aperture.v[0] = 60;
        TexPtr img3 = a.renderer.renderComp(*a.project, *c, 1.0, rp);
        TexPtr o3 = img3 ? a.renderer.finalizeRGBA8(*img3, c->bg, false) : nullptr;
        std::vector<uint8_t> px3;
        bool r3 = o3 && gfx::readback(*o3, px3) && gfx::savePNG(joinPath(outDir, "selftest_3d.png"), px3.data(), o3->w, o3->h);
        ok(r3, strf("render 3D: camera + spot + ambient light + depth of field (%d passes)", a.renderer.lastPasses));
        Layer* fl = nullptr;
        for (auto& l : c->layers) if (l->type == LayerType::Footage && l->audioOn == false) fl = l.get();
        if (fl) {
            fl->motionBlur = true;
            fl->mbBlur = 60;
            TexPtr imb = a.renderer.renderComp(*a.project, *c, 1.0, rp);
            ok(imb != nullptr, "frame-blend motion blur of the video");
        }
    }
    // compound clip: pre-compose and unpack again
    {
        c = a.comp();
        size_t before = c->layers.size();
        a.selLayers.clear();
        for (auto& l : c->layers) if (l->type == LayerType::Solid) a.selLayers.insert(l->id);
        a.precomposeSelected("Selftest Compound");
        Layer* pc = nullptr;
        for (auto& l : c->layers) if (l->type == LayerType::Precomp) pc = l.get();
        if (ok(pc != nullptr, "pre-compose")) {
            a.unpackPrecomp(*pc);
            ok(c->layers.size() == before, strf("unpack compound clip: %d -> %d layers", (int)before, (int)c->layers.size()));
        }
    }
    // undo brings back a removed effect
    {
        c = a.comp();
        a.commitNow();
        Layer* L = nullptr;
        for (auto& l : c->layers) if (!l->effects.empty()) L = l.get();
        if (L) {
            uint64_t lid = L->id;
            size_t n0 = L->effects.size();
            L->effects.pop_back();
            a.markChanged();
            a.undo();
            Layer* L2 = a.comp() ? a.comp()->find(lid) : nullptr;
            ok(L2 && L2->effects.size() == n0, strf("undo restores a removed effect (%d -> %d)", (int)n0, L2 ? (int)L2->effects.size() : -1));
        }
    }
    writeFile(joinPath(outDir, "selftest_report.txt"), rep);
    a.closeProject();
    gfx::shutdown();
    return rep.find("FAIL") == std::string::npos ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    // all DLLs live in runtime\ (FFmpeg is delay-loaded): tell Windows before the first call needs them
    SetDllDirectoryW(widen(runtimeDir()).c_str());
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string openPath;
    for (int i = 1; i < argc; i++) {
        std::string s = narrow(argv[i]);
        if (s == "--check-effects") return checkEffects();
        if (s == "--selftest" && i + 2 < argc) return selfTest(narrow(argv[i + 1]), narrow(argv[i + 2]));
        if (s == "--matte-job" && i + 1 < argc) return aiRunMatteJob(narrow(argv[i + 1]));
        if (s == "--matte" && i + 2 < argc) {
            // built-in background removal on the first frame of a clip, GPU first (diagnostics)
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            WNDCLASSEXW wc = {sizeof(wc), 0, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"GeSeMatte", nullptr};
            RegisterClassExW(&wc);
            HWND h = CreateWindowW(L"GeSeMatte", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
            if (!gfx::init(h)) return 2;
            App& a = g_appInstance;
            g_app = &a;
            a.newProject();
            FootagePtr f = a.importFile(narrow(argv[i + 1]));
            FramePtr fr = f && f->media ? f->media->decodeSync(0, 0) : nullptr;
            std::string out = narrow(argv[i + 2]), e, rep;
            for (int run = 0; run < 3 && fr; run++) {
                std::vector<uint8_t> alpha;
                double s0 = nowSec();
                bool r = localMatte(fr->rgba.data(), fr->w, fr->h, alpha, true, e);
                rep += strf("run %d: %s on %s, %.2fs %s\n", run, r ? "OK" : "FAIL", localDeviceName().c_str(), nowSec() - s0, e.c_str());
                if (r && run == 0) {
                    std::vector<uint8_t> px = fr->rgba;
                    for (size_t k = 0; k < alpha.size(); k++) px[k * 4 + 3] = alpha[k];
                    gfx::savePNG(out, px.data(), fr->w, fr->h);
                }
            }
            writeFile(out + ".txt", rep);
            return rep.find("FAIL") == std::string::npos ? 0 : 1;
        }
        if (fileExists(s)) openPath = s;
    }
    LocalFree(argv);
    (void)cmd;

    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    srand((unsigned)::time(nullptr));
    App& a = g_appInstance;
    g_app = &a;
    a.settings.load();
    i18nSetLanguage(a.settings.language < 0 ? i18nSystemDefault() : a.settings.language);
    logf("%s starting", GESE_FULLNAME);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GeSeMainWindow";
    RegisterClassExW(&wc);

    POINT pt = {0, 0};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    a.dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int sw = mi.rcWork.right - mi.rcWork.left, sh = mi.rcWork.bottom - mi.rcWork.top;
    int size = std::min((int)(std::min(sw, sh) * 0.58), (int)(900 * a.dpi));
    a.hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"GeSe v2.4.0", WS_POPUP, mi.rcWork.left + (sw - size) / 2,
                             mi.rcWork.top + (sh - size) / 2, size, size, nullptr, nullptr, inst, nullptr);
    if (!gfx::init(a.hwnd)) {
        MessageBoxW(nullptr, L"Direct3D 11 is not available on this system.", L"GeSe", MB_ICONERROR);
        return 1;
    }
    DragAcceptFiles(a.hwnd, TRUE);
    {
        // rounded corners for the start-up window. DWM does not round caption-less popups,
        // so the window is clipped to a rounded region (the splash also paints a matching rim).
        int d = (int)(size * 0.06f);
        SetWindowRgn(a.hwnd, CreateRoundRectRgn(0, 0, size + 1, size + 1, d, d), TRUE);
    }
    ShowWindow(a.hwnd, SW_SHOW);
    UpdateWindow(a.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    static std::string ini = joinPath(appDataDir(), "layout_v2.ini");  // bump when default workspaces change
    io.IniFilename = ini.c_str();
    a.resetLayout = !fileExists(ini);
    ImGui_ImplWin32_Init(a.hwnd);
    ImGui_ImplDX11_Init(gfx::dev(), gfx::ctx());
    textr::init();
    a.dpi = effectiveScale(a.hwnd, a.dpi);
    uiLoadFonts(a);
    uiApplyTheme(a);

    // the start-up artwork can be dropped in as png, jpg or webp; the newest file wins
    {
        std::string best;
        int64_t bestTime = -1;
        for (const char* ext : {".png", ".webp", ".jpg", ".jpeg"}) {
            std::string sp = resPath(std::string("Splash\\splash") + ext);
            if (fileExists(sp) && fileTime(sp) > bestTime) { best = sp; bestTime = fileTime(sp); }
        }
        if (!best.empty()) a.splashTex = gfx::loadTextureFile(best);
    }
    a.logoTex = gfx::loadTextureFile(resPath("Splash\\logo.png"));
    a.factIndex = rand() % kFactCount;
    a.splashStart = nowSec();
    if (!openPath.empty()) a.pendingOpenPath = openPath;

    bool done = false;
    while (!done) {
        MSG msg;
        bool hadMsg = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            hadMsg = true;
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (a.quitConfirmed) { DestroyWindow(a.hwnd); continue; }
        if (IsIconic(a.hwnd) && g_showPending == 0) { Sleep(20); continue; }
        // open a project passed on the command line once the UI is ready
        if (a.screen == Screen::Home && !a.pendingOpenPath.empty() && !a.project) {
            std::string p = a.pendingOpenPath;
            a.pendingOpenPath.clear();
            openMediaFiles(a, {p});
        }
        bool busy = a.playing || a.exporter.active() || a.ai.running() || a.renderer.pending || a.screen == Screen::Splash || a.needRender ||
                    !a.toasts.empty() || ImGui::IsAnyMouseDown();
        if (!busy && !hadMsg) MsgWaitForMultipleObjects(0, nullptr, FALSE, 40, QS_ALLINPUT);
        frame(a);
    }
    a.settings.save();
    a.audio.shutdown();
    a.closeProject();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    textr::shutdown();
    gfx::shutdown();
    CoUninitialize();
    return 0;
}
