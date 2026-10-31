#include "ui.h"
#include "gfx.h"
#include "textrender.h"

static std::string prettyPath(const std::string& p) {
    std::string r = p;
    for (auto& c : r) if (c == '\\') c = '/';
    return r;
}

// one unit of real start-up work per frame so the splash keeps animating
static void loadStep(App& a) {
    std::string root = prettyPath(exeDir());
    switch (a.loadStage) {
    case 0:
        a.loadingLine = "loading " + root + "/resources/Core/compositor.hlsl";
        textr::init();
        a.renderer.init(&a.effects);
        a.loadStage++;
        break;
    case 1:
        a.effects.disabledPlugins.clear();
        for (auto& d : a.settings.disabledPlugins) a.effects.disabledPlugins.insert(toLower(d));
        a.effects.beginLoad();
        a.loadingLine = "loading " + root + "/resources/Effects/";
        a.loadStage++;
        break;
    case 2: {
        std::string f;
        double t0 = nowSec();
        bool more = true;
        while (more && nowSec() - t0 < 0.03) {
            more = a.effects.loadStep(f);
            if (!f.empty()) a.loadingLine = "loading " + prettyPath(f);
        }
        if (!more) a.loadStage++;
        break;
    }
    case 3:
        a.loadingLine = "loading " + root + "/resources/Presets/";
        a.presetFiles();
        a.loadStage++;
        break;
    case 4:
        a.loadingLine = "loading " + root + "/Plugins/";
        a.loadStage++;
        break;
    case 5:
        a.loadingLine = "initializing audio engine";
        if (!a.audio.init()) logf("audio device unavailable - silent playback");
        a.loadStage++;
        break;
    case 6:
        a.loadingLine = "scanning system fonts";
        textr::systemFonts();
        MediaSource::setCacheBudget((int64_t)a.settings.cacheMB * 1024 * 1024);
        a.loadStage++;
        break;
    case 7: {
        int defs = (int)a.effects.defs.size(), errs = 0;
        for (auto& d : a.effects.defs) if (!d->ok) errs++;
        a.loadingLine = strf("ready  -  %d effects in %d categories%s", defs, (int)a.effects.categories.size(),
                             errs ? strf(", %d failed", errs).c_str() : "");
        logf("%s", a.loadingLine.c_str());
        a.loadStage++;
        a.loadingDone = true;
        a.loadDoneAt = nowSec();
        break;
    }
    default: break;
    }
}

void uiSplash(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 p0 = vp->Pos, sz = vp->Size;
    float W = std::min(sz.x, sz.y);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p0 + sz, IM_COL32(0, 0, 0, 255));
    if (a.splashTex) {
        // cover-fit the artwork
        float iw = (float)a.splashTex->w, ih = (float)a.splashTex->h;
        float k = std::max(sz.x / iw, sz.y / ih);
        ImVec2 d(iw * k, ih * k);
        ImVec2 o = p0 + (sz - d) * 0.5f;
        dl->AddImage(a.splashTex->id(), o, o + d);
    }
    // The artwork (Main\Splash\splash.png) already contains the title and version;
    // the only thing drawn on top is the loading log, as in the reference.
    auto U = [&](float v) { return v / 1920.f * W; };
    // loading log (Arial, grey)
    {
        float fs = U(48);
        std::string line = a.loadingLine;
        ImVec2 ts = a.fArial->CalcTextSizeA(fs, FLT_MAX, 0, line.c_str());
        float x = p0.x + U(1160);
        if (x + ts.x > p0.x + sz.x - U(14)) x = p0.x + sz.x - U(14) - ts.x;
        x = std::max(x, p0.x + U(75));
        dl->PushClipRect(p0, p0 + sz, true);
        dl->AddText(a.fArial, fs, ImVec2(x, p0.y + U(1850)), IM_COL32(0x5C, 0x5C, 0x5C, 0xFF), line.c_str());
        dl->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!a.loadingDone) loadStep(a);
    if (a.loadingDone && nowSec() - a.splashStart > 3.2 && nowSec() - a.loadDoneAt > 0.45) a.screen = Screen::Home;
}
