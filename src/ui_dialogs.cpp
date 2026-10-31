#include "ui.h"
#include "gfx.h"
#include "i18n.h"
#include "ai_local.h"
#include "imgui_internal.h"

static void openIf(bool& flag, const char* name) {
    if (flag) { ImGui::OpenPopup(name); flag = false; }
}
static void centerNext(float w, float h = 0) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
}
static bool footerButtons(const char* ok, bool okEnabled = true) {
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, S(2)));
    float bw = S(96);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - S(22));
    bool r = false;
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(!okEnabled);
    if (accentButton(ok, ImVec2(bw, 0)) || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsAnyItemActive())) { r = true; ImGui::CloseCurrentPopup(); }
    ImGui::EndDisabled();
    return r;
}
static void labelCol(const char* l, float w = 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(l);
    ImGui::SameLine(w > 0 ? w : S(150));
}
static std::string aspectText(int w, int h) {
    int a = w, b = h;
    while (b) { int t = a % b; a = b; b = t; }
    if (a <= 0) a = 1;
    int rw = w / a, rh = h / a;
    if (rw > 64) return strf("%.2f:1", (double)w / h);
    return strf("%d:%d (%.2f)", rw, rh, (double)w / h);
}

// ---------------------------------------------------------------------------
// composition background ("canvas"): colour with ready-made swatches, blurred clip, or an image
void backgroundEditor(App& a, Composition& c) {
    ImGui::PushID("bged");
    ImGui::SeparatorText("Background");
    int mode = c.bgMode;
    ImGui::RadioButton("Color", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Blur", &mode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Image", &mode, 2);
    if (mode != c.bgMode) { c.bgMode = mode; a.markChanged(); }
    if (c.bgMode == 0) {
        if (ImGui::ColorEdit3("##bgc", c.bg, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) { c.bg[3] = 1; a.markChanged(); }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", T("your colour, or pick one:"));
        static const uint32_t kSwatches[] = {0x000000, 0xFFFFFF, 0x1E1E1E, 0x5C5C5C, 0xB0B0B0, 0xB85500, 0x271200, 0xF2D7B6, 0xFFB3BA,
                                             0xFFDFBA, 0xFFFFBA, 0xBAFFC9, 0xBAE1FF, 0xD7BAFF, 0x2E3A59, 0x3B5249, 0x6B2737, 0x0F4C5C,
                                             0xE63946, 0xF4A261, 0x2A9D8F, 0x264653, 0x8338EC, 0x3A86FF};
        float sw = S(22), avail = ImGui::GetContentRegionAvail().x, x = 0;
        for (int i = 0; i < (int)(sizeof(kSwatches) / sizeof(kSwatches[0])); i++) {
            ImVec4 col(((kSwatches[i] >> 16) & 255) / 255.f, ((kSwatches[i] >> 8) & 255) / 255.f, (kSwatches[i] & 255) / 255.f, 1);
            if (i > 0 && x + sw <= avail) ImGui::SameLine(0, S(4));
            else if (i > 0) x = 0;
            if (ImGui::ColorButton(strf("##sw%d", i).c_str(), col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(sw, sw))) {
                c.bg[0] = col.x, c.bg[1] = col.y, c.bg[2] = col.z, c.bg[3] = 1;
                a.markChanged();
            }
            x += sw + S(4);
        }
    } else {
        if (c.bgMode == 2) {
            std::string shown = c.bgImage.empty() ? std::string(T("no image")) : fileName(c.bgImage);
            ImGui::TextDisabled("%s", shown.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Choose Image...")) {
                std::string p = dlgOpenFile(a.hwnd, L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.tif;*.tiff\0", "Background Image");
                if (!p.empty()) { c.bgImage = p; a.markChanged(); }
            }
        } else {
            ImGui::TextWrapped("%s", T("A blurred copy of the bottom clip fills the empty parts of the frame (vertical video in a 16:9 frame and the other way round)."));
        }
        ImGui::SetNextItemWidth(S(180));
        if (ImGui::SliderFloat("##bgblur", &c.bgBlur, 0, 100, "%.0f %%")) a.markChanged();
        ImGui::SameLine();
        ImGui::TextUnformatted(T("Blur"));
        for (float v : {15.f, 40.f, 70.f, 100.f}) {
            ImGui::SameLine(0, S(3));
            if (ImGui::SmallButton(strf("%g##bb", v).c_str())) { c.bgBlur = v; a.markChanged(); }
        }
    }
    if (a.project && a.project->comps.size() > 1 && ImGui::SmallButton("Apply to All Compositions")) {
        for (auto& oc : a.project->comps) {
            oc->bgMode = c.bgMode;
            oc->bgBlur = c.bgBlur;
            oc->bgImage = c.bgImage;
            memcpy(oc->bg, c.bg, sizeof(c.bg));
        }
        a.markChanged();
    }
    ImGui::PopID();
}

static void compDialog(App& a, bool isNew) {
    const char* name = isNew ? "Composition Settings##new" : "Composition Settings##edit";
    openIf(isNew ? a.dlgNewComp : a.dlgCompSettings, name);
    centerNext(S(520));
    // separate state for "New Composition" and "Composition Settings": both run every frame,
    // and a shared `init` flag made the closed one reset the open one (fields snapped back)
    struct CompDlg {
        char nm[128] = {};
        int w = 1920, h = 1080, preset = 0;
        float fps = 30, dur = 10, bg[4] = {0, 0, 0, 1}, shutter = 180;
        int mbs = 8;
        bool lockAR = true, init = false;
        char durBuf[32] = {};
    };
    static CompDlg dlgs[2];
    CompDlg& D = dlgs[isNew ? 0 : 1];
    char* nm = D.nm;
    int &w = D.w, &h = D.h, &preset = D.preset, &mbs = D.mbs;
    float &fps = D.fps, &dur = D.dur, &shutter = D.shutter;
    float* bg = D.bg;
    bool &lockAR = D.lockAR, &init = D.init;
    if (!ImGui::IsPopupOpen(name)) { init = false; return; }
    if (!init) {
        init = true;
        Composition* c = isNew ? nullptr : a.comp();
        if (c) {
            snprintf(nm, sizeof(D.nm), "%s", c->name.c_str());
            w = c->w; h = c->h; fps = (float)c->fps; dur = (float)c->duration; memcpy(bg, c->bg, sizeof(D.bg));
            shutter = c->shutterAngle; mbs = c->mbSamples; preset = -1;
        } else {
            snprintf(nm, sizeof(D.nm), "%s", a.project ? a.project->uniqueCompName("Comp").c_str() : "Comp 1");
            preset = clampv(a.settings.lastCompPreset, 0, kCompPresetCount - 1);
            w = kCompPresets[preset].w; h = kCompPresets[preset].h; fps = (float)kCompPresets[preset].fps; dur = 10;
            bg[0] = bg[1] = bg[2] = 0; bg[3] = 1;
        }
    }
    if (ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        labelCol("Composition Name");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##nm", nm, sizeof(D.nm));
        ImGui::SeparatorText("Basic");
        labelCol("Preset");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##preset", preset >= 0 ? kCompPresets[preset].name : "Custom", ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < kCompPresetCount; i++)
                if (ImGui::Selectable(kCompPresets[i].name, preset == i)) { preset = i; w = kCompPresets[i].w; h = kCompPresets[i].h; fps = (float)kCompPresets[i].fps; }
            ImGui::EndCombo();
        }
        labelCol("Width");
        ImGui::SetNextItemWidth(S(90));
        int ow = w, oh = h;
        if (ImGui::InputInt("##w", &w, 0)) { w = clampv(w, 16, 16384); if (lockAR && ow > 0) h = std::max(16, (int)std::lround((double)w * oh / ow)); preset = -1; }
        ImGui::SameLine();
        ImGui::Text("px");
        ImGui::SameLine(S(290));
        ImGui::Checkbox("Lock Aspect Ratio", &lockAR);
        labelCol("Height");
        ImGui::SetNextItemWidth(S(90));
        if (ImGui::InputInt("##h", &h, 0)) { h = clampv(h, 16, 16384); if (lockAR && oh > 0) w = std::max(16, (int)std::lround((double)h * ow / oh)); preset = -1; }
        ImGui::SameLine();
        ImGui::Text("px");
        ImGui::SameLine(S(290));
        ImGui::TextDisabled("Aspect %s", aspectText(w, h).c_str());
        labelCol("Pixel Aspect Ratio");
        ImGui::TextDisabled("Square Pixels (1.00)");
        labelCol("Frame Rate");
        ImGui::SetNextItemWidth(S(90));
        if (ImGui::BeginCombo("##fpsc", strf("%g", fps).c_str())) {
            for (float f : {8.f, 10.f, 12.f, 12.5f, 15.f, 23.976f, 24.f, 25.f, 29.97f, 30.f, 48.f, 50.f, 59.94f, 60.f, 90.f, 120.f})
                if (ImGui::Selectable(strf("%g", f).c_str(), fabsf(f - fps) < 1e-3f)) { fps = f; preset = -1; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(80));
        if (ImGui::InputFloat("fps##fpsi", &fps, 0, 0, "%.3f")) preset = -1;
        if (ImGui::IsItemDeactivatedAfterEdit()) fps = clampv(fps, 1.f, 240.f);
        labelCol("Duration");
        ImGui::SetNextItemWidth(S(90));
        // keep the typed text while the field is being edited; refresh it otherwise
        ImGuiID durId = ImGui::GetID("##dur");
        if (ImGui::GetActiveID() != durId) snprintf(D.durBuf, sizeof(D.durBuf), "%s", fmtTimecode(dur, std::max(1.f, fps)).c_str());
        ImGui::InputText("##dur", D.durBuf, sizeof(D.durBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            bool ok = false;
            double v = parseTimecode(D.durBuf, std::max(1.f, fps), &ok);
            if (ok && v > 0) dur = (float)v;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(80));
        ImGui::DragFloat("sec##durs", &dur, 0.1f, 0.1f, 36000, "%.2f");
        ImGui::SeparatorText("Advanced");
        labelCol("Background Color");
        ImGui::ColorEdit4("##bg", bg, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", T("blur / image / ready colours: Composition > Background"));
        ImGui::TextDisabled("Motion blur is set per layer: Effect Controls > Transform > Motion Blur.");
        if (footerButtons("OK", nm[0] != 0)) {
            if (isNew) {
                if (!a.project) a.newProject();
                Composition* c = a.newComp(nm, w, h, fps, std::max(0.1f, dur));
                memcpy(c->bg, bg, sizeof(D.bg));
                c->shutterAngle = shutter;
                c->mbSamples = mbs;
                if (preset >= 0) a.settings.lastCompPreset = preset;
            } else if (Composition* c = a.comp()) {
                c->name = nm;
                c->w = w;
                c->h = h;
                c->fps = fps;
                c->duration = std::max(0.1f, dur);
                c->workEnd = std::min(c->workEnd, c->duration);
                if (c->workEnd <= c->workStart) { c->workStart = 0; c->workEnd = c->duration; }
                memcpy(c->bg, bg, sizeof(D.bg));
                c->shutterAngle = shutter;
                c->mbSamples = mbs;
                c->tlZoom = 0;
                for (auto& oc : a.project->comps)
                    for (auto& l : oc->layers) if (l->type == LayerType::Precomp && l->source == c->id) l->name = c->name;
            }
            a.markChanged();
        }
        ImGui::EndPopup();
    }
}

static void solidDialog(App& a) {
    openIf(a.dlgSolid, "Solid Settings");
    centerNext(S(420));
    static char nm[96] = "Solid";
    static int w = 1920, h = 1080;
    static float col[4] = {0.72f, 0.33f, 0, 1};
    static bool init = false;
    if (!ImGui::IsPopupOpen("Solid Settings")) { init = false; return; }
    Composition* c = a.comp();
    if (!init && c) { init = true; w = c->w; h = c->h; snprintf(nm, sizeof(nm), "Solid %d", (int)c->layers.size() + 1); }
    if (ImGui::BeginPopupModal("Solid Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        labelCol("Name", S(80));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##n", nm, sizeof(nm));
        labelCol("Width", S(80));
        ImGui::SetNextItemWidth(S(90));
        ImGui::InputInt("##w", &w, 0);
        labelCol("Height", S(80));
        ImGui::SetNextItemWidth(S(90));
        ImGui::InputInt("##h", &h, 0);
        if (c && ImGui::Button("Make Comp Size")) { w = c->w; h = c->h; }
        labelCol("Color", S(80));
        ImGui::ColorEdit4("##c", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        if (footerButtons("OK") && c) a.addSolid(*c, nm, clampv(w, 1, 16384), clampv(h, 1, 16384), col);
        ImGui::EndPopup();
    }
}

static void speedDialog(App& a) {
    openIf(a.dlgSpeed, "Speed / Duration");
    centerNext(S(380));
    static float pct = 100;
    static bool rev = false;
    static bool init = false;
    if (!ImGui::IsPopupOpen("Speed / Duration")) { init = false; return; }
    Layer* l = a.selectedLayer();
    Composition* c = a.comp();
    if (!init && l) { init = true; pct = (float)(l->speed * 100); rev = l->reversed; }
    if (ImGui::BeginPopupModal("Speed / Duration", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        labelCol("Speed", S(90));
        ImGui::SetNextItemWidth(S(120));
        ImGui::DragFloat("##sp", &pct, 1, 1, 10000, "%.1f %%");
        ImGui::SameLine();
        for (float p : {25.f, 50.f, 200.f, 400.f}) { if (ImGui::SmallButton(strf("%g%%", p).c_str())) pct = p; ImGui::SameLine(); }
        ImGui::NewLine();
        ImGui::Checkbox("Reverse Speed", &rev);
        static bool keepP = false;
        if (ImGui::IsWindowAppearing() && l) keepP = l->keepPitch;
        ImGui::Checkbox("Keep audio pitch (slow down only)", &keepP);
        tooltip("On: the sound only slows down / speeds up, voices keep their pitch.\nOff: the pitch follows the speed, like a record.");
        if (l && c) {
            double newDur = (l->outPoint - l->inPoint) * l->speed / std::max(0.01, pct / 100.0);
            ImGui::TextDisabled("New duration: %s", fmtTimecode(newDur, c->fps).c_str());
        }
        if (footerButtons("OK") && c) {
            double ns = std::max(0.01, pct / 100.0);
            for (Layer* sl : a.selectedLayers()) {
                double srcIn = (sl->inPoint - sl->startTime) * sl->speed;
                double dur = (sl->outPoint - sl->inPoint) * sl->speed / ns;
                sl->speed = ns;
                sl->startTime = sl->inPoint - srcIn / ns;
                sl->outPoint = sl->inPoint + dur;
                sl->reversed = rev;
                sl->keepPitch = keepP;
            }
            a.markChanged();
        }
        ImGui::EndPopup();
    }
}

static void precomposeDialog(App& a) {
    openIf(a.dlgPrecompose, "Pre-compose");
    centerNext(S(420));
    static char nm[128];
    static bool init = false;
    if (!ImGui::IsPopupOpen("Pre-compose")) { init = false; return; }
    if (!init && a.project) { init = true; snprintf(nm, sizeof(nm), "%s", a.project->uniqueCompName("Compound Clip").c_str()); }
    if (ImGui::BeginPopupModal("Pre-compose", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Moves the selected layers into a new composition");
        ImGui::TextDisabled("(compound clip) and replaces them with one layer.");
        labelCol("New composition name", S(170));
        ImGui::SetNextItemWidth(S(220));
        ImGui::InputText("##n", nm, sizeof(nm));
        if (footerButtons("OK", nm[0] != 0)) a.precomposeSelected(nm);
        ImGui::EndPopup();
    }
}

static void savePresetDialog(App& a) {
    openIf(a.dlgSavePreset, "Save Animation Preset");
    centerNext(S(400));
    static char nm[128] = "My Preset";
    if (ImGui::BeginPopupModal("Save Animation Preset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        Layer* l = a.selectedLayer();
        ImGui::TextDisabled("Saves all effects of the selected layer with their keyframes.");
        labelCol("Preset name", S(110));
        ImGui::SetNextItemWidth(S(240));
        ImGui::InputText("##n", nm, sizeof(nm));
        if (footerButtons("Save", nm[0] != 0 && l && !l->effects.empty()) && l) a.savePreset(nm, *l);
        ImGui::EndPopup();
    }
}

static void outputDialog(App& a) {
    openIf(a.dlgOutput, "Output Module Settings");
    centerNext(S(560));
    if (!ImGui::IsPopupOpen("Output Module Settings")) return;
    RenderItem* it = nullptr;
    if (a.project) for (auto& r : a.project->queue) if (r.id == a.editRenderItem) it = &r;
    if (ImGui::BeginPopupModal("Output Module Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!it) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        OutputSettings& o = it->out;
        labelCol("Preset");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##pr", "Apply a preset...", ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < kExportPresetCount; i++)
                if (ImGui::Selectable(kExportPresets[i].name)) {
                    const ExportPreset& p = kExportPresets[i];
                    o.format = p.format; o.encoder = p.encoder; o.quality = p.quality; o.bitrate = p.bitrate; o.scalePct = p.scalePct;
                    o.audio = p.audio; o.audioKbps = p.audioKbps;
                    o.path = joinPath(parentDir(o.path), fileStem(o.path) + formatExtension(o.format));
                    a.settings.exportPreset = i;
                }
            ImGui::EndCombo();
        }
        ImGui::SeparatorText("Video");
        labelCol("Format");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##fmt", &o.format, kFormatNames, kFormatCount)) o.path = joinPath(parentDir(o.path), fileStem(o.path) + formatExtension(o.format));
        if (o.format <= 1) {
            labelCol("Encoder");
            ImGui::RadioButton("CPU (x264 / x265)", &o.encoder, 0);
            ImGui::SameLine();
            ImGui::RadioButton("NVIDIA NVENC (GPU)", &o.encoder, 1);
        }
        if (o.format <= 1 || o.format == 5) {
            labelCol("Quality (CRF / CQ)");
            ImGui::SetNextItemWidth(S(220));
            ImGui::SliderInt("##q", &o.quality, 0, 51, o.quality <= 16 ? "%d  (visually lossless)" : (o.quality <= 23 ? "%d  (high)" : "%d  (small file)"));
            labelCol("Target Bitrate");
            ImGui::SetNextItemWidth(S(120));
            ImGui::InputInt("Mbps (0 = quality mode)##br", &o.bitrate, 1, 5);
            o.bitrate = clampv(o.bitrate, 0, 400);
        }
        labelCol("Resize");
        ImGui::SetNextItemWidth(S(220));
        ImGui::SliderInt("##sc", &o.scalePct, 10, 200, "%d%%");
        Composition* c = a.project->findComp(it->comp);
        if (c) { ImGui::SameLine(); ImGui::TextDisabled("%d × %d", (int)(c->w * o.scalePct / 100) & ~1, (int)(c->h * o.scalePct / 100) & ~1); }
        labelCol("Time Span");
        ImGui::RadioButton("Work Area", &o.range, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Length of Comp", &o.range, 1);
        if (o.format != 4) {
            ImGui::SeparatorText("Audio");
            ImGui::Checkbox("Audio Output", &o.audio);
            if (o.audio) {
                ImGui::SameLine(S(150));
                ImGui::SetNextItemWidth(S(120));
                ImGui::SliderInt("kbps##ak", &o.audioKbps, 96, 512);
            }
        }
        ImGui::SeparatorText("Output");
        static char pathBuf[1024];
        snprintf(pathBuf, sizeof(pathBuf), "%s", o.path.c_str());
        ImGui::SetNextItemWidth(-S(90));
        if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) o.path = pathBuf;
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
            std::string ext = formatExtension(o.format);
            std::wstring filt = widen("Output (*" + ext + ")") + std::wstring(1, L'\0') + widen("*" + ext) + std::wstring(1, L'\0');
            std::string p = dlgSaveFile(a.hwnd, filt.c_str(), ext.c_str() + 1, fileStem(o.path), "Output To");
            if (!p.empty()) o.path = p;
        }
        ImGui::Dummy(ImVec2(0, S(6)));
        if (accentButton("Done", ImVec2(-1, 0))) { a.markChanged(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static void aiDialog(App& a) {
    openIf(a.dlgAi, "Remove Background (AI)");
    centerNext(S(560));
    static int provider = -1;
    static int mode = 0;
    if (provider < 0) provider = a.settings.aiProvider;
    if (ImGui::BeginPopupModal("Remove Background (AI)", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        Layer* l = a.selectedLayer();
        Footage* f = l && l->type == LayerType::Footage ? a.project->findFootage(l->source) : nullptr;
        ImGui::TextWrapped("Cuts out the subject of the selected layer with an AI segmentation service. The result is imported as a new layer with transparency, placed above the original.");
        ImGui::Dummy(ImVec2(0, S(4)));
        labelCol("Service");
        ImGui::SetNextItemWidth(-1);
        provider = clampv(provider, 0, kAiProviderCount - 1);
        if (ImGui::Combo("##prov", &provider, kAiProviders, kAiProviderCount)) a.settings.aiProvider = provider;
        bool keyOk = true;
        if (provider == 0) {
            keyOk = localModelAvailable();
            ImGui::TextWrapped("%s", T("BiRefNet neural network built into GeSe: free, no internet, no limits. Hair and fine edges are kept. Runs on the video card (DirectML), or on the processor."));
            if (ImGui::Checkbox("Use the video card (GPU)", &a.settings.aiGpu)) a.settings.save();
        }
        if (provider == 1) { keyOk = !a.settings.removeBgKey.empty(); ImGui::TextDisabled("API key from remove.bg (Settings > API Keys). Uses credits per image."); }
        if (provider == 2) { keyOk = !a.settings.photoroomKey.empty(); ImGui::TextDisabled("API key from photoroom.com/api. Uses credits per image."); }
        if (provider == 3) {
            ImGui::TextDisabled("Free & offline. Install once:  pip install \"rembg[cli]\"   then run:  rembg s");
            ImGui::TextDisabled("Endpoint: %s", a.settings.rembgUrl.c_str());
        }
        if (provider == 4) { keyOk = !a.settings.customUrl.empty(); ImGui::TextDisabled("POST multipart field 'image_file' -> PNG. URL: %s", a.settings.customUrl.c_str()); }
        if (!keyOk) {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1), "Not configured.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Open Preferences")) { a.prefsPage = 5; a.dlgPrefs = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        bool video = f && f->kind != FootageKind::Image;
        labelCol("Process");
        ImGui::RadioButton("Current frame", &mode, 0);
        ImGui::BeginDisabled(!video);
        ImGui::SameLine();
        ImGui::RadioButton("All frames in work area", &mode, 1);
        ImGui::EndDisabled();
        if (video && mode == 1 && a.comp()) {
            Composition* c = a.comp();
            double s = std::max(l->inPoint, c->workStart), e = std::min(l->outPoint, c->workEnd);
            int n = std::max(0, (int)((e - s) * c->fps));
            if (provider == 0) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1), T("%d frames will be processed on this PC."), n);
            else ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1), "%d frames will be uploaded%s.", n, provider <= 2 ? " (one API credit each)" : "");
        }
        if (a.ai.running()) {
            ImGui::ProgressBar(a.ai.progress(), ImVec2(-1, 0), a.ai.status().c_str());
            if (ImGui::Button("Cancel Job", ImVec2(-1, 0))) a.ai.cancel();
        }
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::Separator();
        float bw = S(110);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - S(22));
        if (ImGui::Button("Close", ImVec2(bw, 0))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!f || !keyOk || a.ai.running());
        if (accentButton("Start", ImVec2(bw, 0))) { a.startAi(provider, mode == 1); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

static void prefsDialog(App& a) {
    openIf(a.dlgPrefs, "Preferences");
    centerNext(S(760), S(520));
    if (ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        static const char* pages[] = {"General", "Appearance", "Previews", "Media & Cache", "Import / Output", "AI / API Keys", "Plugins"};
        ImGui::BeginChild("pages", ImVec2(S(170), -ImGui::GetFrameHeightWithSpacing() - S(10)), ImGuiChildFlags_Borders);
        for (int i = 0; i < 7; i++) if (ImGui::Selectable(pages[i], a.prefsPage == i)) a.prefsPage = i;
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(10)));
        Settings& s = a.settings;
        ImGui::PushItemWidth(S(220));
        switch (a.prefsPage) {
        case 0:
            ImGui::SeparatorText("General");
            ImGui::SliderInt("Auto-save every (minutes, 0 = off)", &s.autosaveMin, 0, 60);
            ImGui::SliderInt("Undo levels", &s.undoLevels, 10, 500);
            {
                float fd = (float)s.freezeDuration;
                if (ImGui::DragFloat("Freeze frame duration (sec)", &fd, 0.05f, 0.1f, 600.f, "%.2f")) s.freezeDuration = fd;
            }
            ImGui::SeparatorText("Timeline");
            ImGui::Checkbox("Snapping", &s.snapping);
            ImGui::Combo("Time display", &s.timeDisplay, "Timecode\0Frames\0");
            if (ImGui::Button("Open auto-save folder")) shellOpen(joinPath(appDataDir(), "Autosave"));
            break;
        case 1: {
            ImGui::SeparatorText("Appearance");
            {
                int lang = i18nLanguage();
                if (ImGui::BeginCombo("Language", kLanguageNames[lang])) {
                    for (int i = 0; i < kLanguageCount; i++)
                        if (ImGui::Selectable(kLanguageNames[i], i == lang)) { s.language = i; i18nSetLanguage(i); s.save(); }
                    ImGui::EndCombo();
                }
            }
            ImGui::TextDisabled("Two brand colors on neutral greys. UI font: %s", a.uiFontName.c_str());
            static float sc = 0, fs = 0;
            if (sc == 0) { sc = s.uiScale; fs = s.fontSize; }
            ImGui::SliderFloat("Interface scale", &sc, 0.75f, 2.0f, "%.2fx");
            ImGui::SliderFloat("Font size", &fs, 11, 22, "%.0f px");
            if (ImGui::Button("Apply")) { s.uiScale = sc; s.fontSize = fs; s.save(); a.setDpi(a.dpi); }
            ImGui::SameLine();
            ImGui::TextDisabled("(applied immediately)");
            ImGui::TextDisabled("Tip: install Source Sans 3 for a cleaner interface font;");
            ImGui::TextDisabled("GeSe picks it up automatically from Windows\\Fonts or resources\\Fonts.");
            break;
        }
        case 2:
            ImGui::SeparatorText("Previews");
            ImGui::Combo("Default preview resolution", &s.previewRes, "Auto\0Full\0Half\0Third\0Quarter\0");
            ImGui::Checkbox("Loop playback", &s.loop);
            ImGui::Checkbox("Drop frames to keep real-time", &s.skipFrames);
            ImGui::TextDisabled("GPU: %s (%lld MB)", gfx::adapterName().c_str(), (long long)gfx::adapterVramMB());
            ImGui::TextDisabled("Audio device: %s", a.audio.deviceOk() ? a.audio.deviceName.c_str() : "none");
            break;
        case 3:
            ImGui::SeparatorText("Media & Disk Cache");
            if (ImGui::SliderInt("Frame cache (MB)", &s.cacheMB, 256, 16384)) MediaSource::setCacheBudget((int64_t)s.cacheMB * 1024 * 1024);
            ImGui::TextDisabled("Shader cache: %s", joinPath(localDataDir(), "ShaderCache").c_str());
            if (ImGui::Button("Clear Shader Cache")) {
                for (auto& f : listFiles(joinPath(localDataDir(), "ShaderCache"), ".cso", false)) DeleteFileW(widen(f).c_str());
                a.notify("Shader cache cleared");
            }
            ImGui::SameLine();
            if (ImGui::Button("Purge Memory")) { a.renderer.clearCaches(); gfx::trimPool(0); }
            break;
        case 4: {
            ImGui::SeparatorText("Import");
            float sd = (float)s.stillDuration, sf = (float)s.seqFps;
            if (ImGui::DragFloat("Still footage duration (sec)", &sd, 0.1f, 0.1f, 3600)) s.stillDuration = sd;
            ImGui::Checkbox("Import numbered images as sequences", &s.importSequences);
            if (ImGui::DragFloat("Sequence frame rate", &sf, 0.1f, 1, 240)) s.seqFps = sf;
            ImGui::SeparatorText("Output");
            static char dir[512];
            snprintf(dir, sizeof(dir), "%s", s.exportDir.c_str());
            if (ImGui::InputTextWithHint("Default export folder", T("next to the project"), dir, sizeof(dir))) s.exportDir = dir;
            ImGui::SameLine();
            if (ImGui::Button("...")) { std::string d = dlgPickFolder(a.hwnd, "Export Folder"); if (!d.empty()) s.exportDir = d; }
            ImGui::Combo("Default render preset", &s.exportPreset, [](void*, int i) { return kExportPresets[i].name; }, nullptr, kExportPresetCount);
            break;
        }
        case 5: {
            ImGui::SeparatorText("AI background removal");
            ImGui::Combo("Default service", &s.aiProvider, kAiProviders, kAiProviderCount);
            ImGui::Checkbox("Built-in AI: use the video card (GPU)", &s.aiGpu);
            ImGui::TextDisabled("Built-in model: %s", localModelAvailable() ? fileName(localModelPath()).c_str() : "missing");
            static char k1[256], k2[256], k3[256], u1[512], u2[512];
            snprintf(k1, sizeof(k1), "%s", s.removeBgKey.c_str());
            snprintf(k2, sizeof(k2), "%s", s.photoroomKey.c_str());
            snprintf(k3, sizeof(k3), "%s", s.customKey.c_str());
            snprintf(u1, sizeof(u1), "%s", s.rembgUrl.c_str());
            snprintf(u2, sizeof(u2), "%s", s.customUrl.c_str());
            ImGui::PushItemWidth(S(300));
            if (ImGui::InputText("remove.bg API key", k1, sizeof(k1), ImGuiInputTextFlags_Password)) s.removeBgKey = k1;
            if (ImGui::InputText("Photoroom API key", k2, sizeof(k2), ImGuiInputTextFlags_Password)) s.photoroomKey = k2;
            if (ImGui::InputText("rembg server URL", u1, sizeof(u1))) s.rembgUrl = u1;
            if (ImGui::InputText("Custom endpoint URL", u2, sizeof(u2))) s.customUrl = u2;
            if (ImGui::InputText("Custom endpoint key", k3, sizeof(k3), ImGuiInputTextFlags_Password)) s.customKey = k3;
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Keys are stored in %%APPDATA%%\\GeSe\\settings.json on this PC only.");
            ImGui::TextDisabled("rembg runs locally for free: pip install \"rembg[cli]\"  then  rembg s");
            break;
        }
        case 6:
            ImGui::SeparatorText("Plugins");
            ImGui::TextDisabled("Built-in effects: %s", a.effects.builtinDir().c_str());
            ImGui::TextDisabled("Plugins: %s", a.effects.pluginDir().c_str());
            ImGui::TextDisabled("User plugins: %s", a.effects.userPluginDir().c_str());
            if (ImGui::Button("Open Plugin Manager")) { ImGui::CloseCurrentPopup(); a.dlgPlugins = true; }
            break;
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(110));
        if (accentButton("OK", ImVec2(S(96), 0))) { s.save(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static const char* kSdkText =
    "GeSe effects are plain text files with the .gsfx extension.\n"
    "Drop them into the Plugins folder (or File > Import > Plugin Effect) and they\n"
    "appear in Effects & Presets. Every effect automatically gets an Intensity\n"
    "parameter and, with @speed, a Speed parameter. All parameters can be keyframed.\n\n"
    "@effect My Wobble                 name shown in the UI\n"
    "@category Distort                 existing or new category\n"
    "@desc Wobbles the picture.        tooltip text\n"
    "@mix manual                       'auto' = cross-fade result with input by Intensity\n"
    "@speed                            animated effect: PHASE = integrated time * Speed\n"
    "@passes 1                         multi-pass: PASS index, gSrc = previous pass\n"
    "@param float  amount \"Amount\" 20 0 200 px\n"
    "@param angle  dir    \"Direction\" 0\n"
    "@param point  center \"Center\" 0.5 0.5\n"
    "@param color  tint   \"Tint\" 1 0.5 0 1\n"
    "@param bool   flip   \"Flip\" 0\n"
    "@param choice edge   \"Edge Mode\" 2 \"Crop|Repeat|Mirror|Stretch\"\n"
    "@code\n"
    "float4 effect(float2 uv) {\n"
    "    float2 o = PXUV(float2(sin(PHASE * TAU + uv.y * 10.0) * amount * INTENSITY, 0));\n"
    "    return SRCE(uv + o, edge);\n"
    "}\n"
    "@end\n\n"
    "Helpers: SRC(uv) SRCP(uv) SRCE(uv,mode) ORIG(uv) PX(px) PXUV(px2) rot2 rotUV luma\n"
    "hash12 vnoise fbm wave(x,type) rgb2hsv hsv2rgb blendScreen blendOverlay blur1D\n"
    "Globals: RES TEXEL TIME PHASE SPEED INTENSITY RSCALE PASS SEED FPS COMPRES FRAME\n"
    "Return straight (non-premultiplied) RGBA. Shaders compile with HLSL ps_5_0.";

static void pluginsDialog(App& a) {
    openIf(a.dlgPlugins, "Plugin Manager");
    centerNext(S(760), S(500));
    if (ImGui::BeginPopupModal("Plugin Manager", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        if (accentButton("Install Plugin...")) {
            std::string f = dlgOpenFile(a.hwnd, L"GeSe Effect (*.gsfx)\0*.gsfx\0", "Install Plugin");
            if (!f.empty()) a.importFile(f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Create New Effect...")) {
            std::string p = joinPath(a.effects.userPluginDir(), strf("MyEffect_%llu.gsfx", (unsigned long long)(newId() % 10000)));
            std::string tpl = "// GeSe plugin effect - edit and press Reload All in the Plugin Manager\n@effect My Effect\n@category Plugins\n@desc Describe what it does.\n@mix auto\n@param float amount \"Amount\" 50 0 100 %\n@param color tint \"Tint\" 1 0.53 0 1\n@code\nfloat4 effect(float2 uv) {\n    float4 c = SRC(uv);\n    c.rgb = lerp(c.rgb, c.rgb * tint.rgb, amount / 100.0);\n    return c;\n}\n@end\n";
            writeFile(p, tpl);
            shellOpen(p);
            a.effects.reload();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload All")) {
            a.effects.reload();
            if (a.project)
                for (auto& c : a.project->comps) for (auto& l : c->layers) for (auto& e : l->effects) a.effects.sync(e);
            a.needRender = true;
            a.notify(strf("Reloaded %d effects", (int)a.effects.defs.size()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Plugins Folder")) shellOpen(a.effects.userPluginDir());
        ImGui::BeginChild("files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(8)), ImGuiChildFlags_Borders);
        if (ImGui::BeginTable("pf", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, S(34));
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Effects", ImGuiTableColumnFlags_WidthFixed, S(60));
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableHeadersRow();
            bool changed = false;
            for (auto& f : a.effects.files) {
                ImGui::PushID(f.path.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (f.plugin) {
                    bool en = f.enabled;
                    if (ImGui::Checkbox("##en", &en)) {
                        auto& dp = a.settings.disabledPlugins;
                        std::string key = toLower(f.path);
                        dp.erase(std::remove(dp.begin(), dp.end(), key), dp.end());
                        if (!en) dp.push_back(key);
                        changed = true;
                    }
                } else ImGui::TextDisabled("core");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fileName(f.path).c_str());
                tooltip(f.path.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", f.effects);
                ImGui::TableNextColumn();
                if (!f.enabled) ImGui::TextDisabled("disabled");
                else if (f.errors) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%d error(s): %s", f.errors, f.firstError.c_str());
                else ImGui::TextColored(pal::v4(pal::Accent), "OK");
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (changed) {
                a.settings.save();
                a.effects.disabledPlugins.clear();
                for (auto& d : a.settings.disabledPlugins) a.effects.disabledPlugins.insert(d);
                a.effects.reload();
                a.needRender = true;
            }
        }
        ImGui::EndChild();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(110));
        if (ImGui::Button("Close", ImVec2(S(96), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void sdkDialog(App& a) {
    openIf(a.dlgSdk, "Plugin SDK");
    centerNext(S(720), S(560));
    if (ImGui::BeginPopupModal("Plugin SDK", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::BeginChild("txt", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(8)), ImGuiChildFlags_Borders);
        ImGui::PushFont(a.fMono, 0);
        ImGui::TextUnformatted(kSdkText);
        ImGui::PopFont();
        ImGui::EndChild();
        if (ImGui::Button("Open Example Plugin")) shellOpen(joinPath(a.effects.pluginDir(), "Example_Wobble.gsfx"));
        ImGui::SameLine(ImGui::GetWindowWidth() - S(110));
        if (ImGui::Button("Close", ImVec2(S(96), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void aboutDialog(App& a) {
    openIf(a.dlgAbout, "About GeSe");
    centerNext(S(520));
    if (ImGui::BeginPopupModal("About GeSe", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (a.logoTex) ImGui::Image(a.logoTex->id(), ImVec2(S(72), S(72)));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushFont(a.fAkira, S(26) / a.dpi / a.settings.uiScale * 1.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.533f, 0.004f, 1), "GESE");
        ImGui::PopFont();
        ImGui::Text("%s  ·  motion graphics & video editor", GESE_FULLNAME);
        ImGui::TextDisabled("GPU: %s", gfx::adapterName().c_str());
        ImGui::TextDisabled("%d effects loaded", (int)a.effects.defs.size());
        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::TextDisabled("Uses FFmpeg (libavcodec, libavformat, libswscale, libswresample),");
        ImGui::TextDisabled("Dear ImGui (MIT), Direct3D 11, Direct2D/DirectWrite, WASAPI, WIC.");
        ImGui::TextDisabled("Fonts: Akira Expanded (splash), Arial, Segoe UI.");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(110));
        if (ImGui::Button("Close", ImVec2(S(96), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void shortcutsDialog(App& a) {
    openIf(a.dlgShortcuts, "Keyboard Shortcuts");
    centerNext(S(640), S(560));
    if (ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        static const char* rows[][2] = {
            {"Play / Stop", "Space"}, {"Previous / next frame", "Page Up / Page Down"}, {"10 frames back / forward", "Shift+Page Up / Down"},
            {"Go to start / end", "Home / End"}, {"Work area start / end", "B / N"}, {"Add marker", "* (numpad)"},
            {"Selection / Hand / Zoom", "V / H / Z"}, {"Rotation / Anchor point", "W / Y"}, {"Mask tools", "Q"}, {"Type tool", "Ctrl+T"},
            {"Blade (split) tool", "C"}, {"Split layers at playhead", "Ctrl+Shift+D"}, {"Duplicate", "Ctrl+D"}, {"Delete / Ripple delete", "Delete / Shift+Delete"},
            {"Trim in / out to playhead", "Alt+[ / Alt+]"}, {"Move layer in / out to playhead", "[ / ]"}, {"Bring forward / send backward", "Ctrl+] / Ctrl+["},
            {"Pre-compose (compound clip)", "Ctrl+Shift+C"}, {"Speed / duration", "Ctrl+R"}, {"Reverse layer", "Ctrl+Alt+R"},
            {"Fit layer to comp", "Ctrl+Alt+F"}, {"New composition", "Ctrl+N"}, {"Composition settings", "Ctrl+K"}, {"New solid", "Ctrl+Y"},
            {"New effect (adjustment) layer", "Ctrl+Alt+Y"}, {"New null", "Ctrl+Alt+Shift+Y"}, {"New text layer", "Ctrl+Alt+Shift+T"},
            {"Effect Controls", "F3"}, {"Remove all effects", "Ctrl+Shift+E"}, {"Apply last effect", "Ctrl+Alt+Shift+E"},
            {"Easy ease / in / out", "F9 / Shift+F9 / Ctrl+Shift+F9"}, {"Toggle hold keyframe", "Ctrl+Alt+H"}, {"Deselect all", "F2"},
            {"Reveal properties", "P S R T A / E / U"}, {"Zoom viewer in / out / fit", ". / , / Shift+/"}, {"Render queue", "Ctrl+M"},
            {"Import", "Ctrl+I"}, {"Save / Save As", "Ctrl+S / Ctrl+Shift+S"}, {"Undo / Redo", "Ctrl+Z / Ctrl+Shift+Z"}, {"Preferences", "Ctrl+,"}};
        ImGui::BeginChild("sc", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(8)));
        if (ImGui::BeginTable("t", 2, ImGuiTableFlags_RowBg)) {
            for (auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r[0]);
                ImGui::TableNextColumn();
                ImGui::TextColored(pal::v4(pal::Accent), "%s", r[1]);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(110));
        if (ImGui::Button("Close", ImVec2(S(96), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void uiDialogs(App& a) {
    compDialog(a, true);
    compDialog(a, false);
    solidDialog(a);
    speedDialog(a);
    precomposeDialog(a);
    savePresetDialog(a);
    outputDialog(a);
    aiDialog(a);
    prefsDialog(a);
    pluginsDialog(a);
    sdkDialog(a);
    aboutDialog(a);
    shortcutsDialog(a);
}
