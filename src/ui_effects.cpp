#include "ui.h"
#include "i18n.h"
#include "audiofx.h"

static char g_fxSearch[96] = "";
static bool g_fxFocus = false;

static int defIndex(App& a, const EffectDef* d) {
    for (size_t i = 0; i < a.effects.defs.size(); i++) if (a.effects.defs[i].get() == d) return (int)i;
    return -1;
}

static void effectItem(App& a, const EffectDef* d, bool showCat) {
    ImGui::PushID(d);
    std::string label = d->name;
    if (!d->ok) ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::TextFaint));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Selectable("##fx", false, ImGuiSelectableFlags_AllowDoubleClick);
    bool dbl = clicked && ImGui::IsMouseDoubleClicked(0);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawIcon(dl, Icon::Fx, p + ImVec2(S(8), ImGui::GetTextLineHeight() * 0.5f), S(13), d->ok ? pal::Accent : pal::TextFaint);
    dl->AddText(p + ImVec2(S(20), 0), d->ok ? pal::Text : pal::TextFaint, label.c_str());
    float tx = S(26) + ImGui::CalcTextSize(label.c_str()).x;
    if (showCat) dl->AddText(p + ImVec2(tx, 0), pal::TextFaint, d->category.c_str()), tx += ImGui::CalcTextSize(d->category.c_str()).x + S(6);
    if (d->plugin) dl->AddText(p + ImVec2(tx, 0), pal::AccentA(200), "plugin");
    if (!d->ok) ImGui::PopStyleColor();
    // hold the cursor on an effect: the viewer shows it applied (selected layer, or the whole frame)
    if (d->ok && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsMouseDown(0)) a.hoverFx = d->id;
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(d->name.c_str());
        ImGui::TextDisabled("%s%s", d->category.c_str(), d->animated ? "  ·  animated (Speed)" : "");
        if (!d->description.empty()) {
            ImGui::PushTextWrapPos(S(320));
            ImGui::TextUnformatted(d->description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Intensity%s", d->animated ? ", Speed" : "");
        for (auto& pd : d->params) ImGui::TextDisabled("%s", pd.label.c_str());
        if (!d->ok) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%s", d->error.c_str());
        ImGui::TextDisabled("Double-click to apply, or drag onto a layer / empty timeline area.");
        ImGui::EndTooltip();
    }
    if (d->ok && ImGui::BeginDragDropSource()) {
        int idx = defIndex(a, d);
        ImGui::SetDragDropPayload(DND_EFFECT, &idx, sizeof(idx));
        ImGui::Text("fx  %s", d->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (dbl && d->ok) a.applyEffect(d);
    ImGui::PopID();
}

// search matches the English name, the translated name, the category and the description
static bool fxMatches(const EffectDef& d, const std::string& q) {
    auto has = [&](const std::string& s) { return !s.empty() && toLower(s).find(q) != std::string::npos; };
    return has(d.name) || has(T(d.name.c_str())) || has(d.category) || has(T(d.category.c_str())) || has(d.description) || has(d.id);
}

static void effectsPanelBody(App& a);

void panelEffects(App& a) {
    std::string prevHover = a.hoverFx;
    a.hoverFx.clear();
    if (ImGui::Begin("Effects & Presets", &a.pEffects)) effectsPanelBody(a);
    ImGui::End();
    if (a.hoverFx != prevHover) a.needRender = true;
}

static void effectsPanelBody(App& a) {
    // search: Ctrl+F focuses it; Esc clears
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) ImGui::SetKeyboardFocusHere();
    if (g_fxFocus) { ImGui::SetKeyboardFocusHere(); g_fxFocus = false; }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(26), ImGui::GetStyle().FramePadding.y));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InputTextWithHint("##fxs", T("Search effects and presets"), g_fxSearch, sizeof(g_fxSearch));
    ImGui::PopStyleVar();
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_fxSearch[0] = 0;
    drawIcon(ImGui::GetWindowDrawList(), Icon::Search, sp + ImVec2(S(13), ImGui::GetFrameHeight() * 0.5f), S(12), pal::TextDim);
    if (g_fxSearch[0]) {
        ImGui::SameLine(ImGui::GetWindowWidth() - S(30));
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, sp.y));
        if (iconButton("clr", Icon::Close, false, nullptr)) g_fxSearch[0] = 0;
    }
    (void)io;
    ImGui::BeginChild("fxlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    std::string q = toLower(trim(g_fxSearch));
    auto presets = a.presetFiles();
    if (!q.empty()) {
        for (size_t i = 0; i < presets.size(); i++) {
            if (toLower(fileStem(presets[i])).find(q) == std::string::npos) continue;
            ImGui::PushID((int)i);
            if (ImGui::Selectable(("*  " + fileStem(presets[i])).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
                a.applyPreset(presets[i]);
            if (ImGui::BeginDragDropSource()) {
                int idx = (int)i;
                ImGui::SetDragDropPayload(DND_PRESET, &idx, sizeof(idx));
                ImGui::Text("%s", fileStem(presets[i]).c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
        int found = 0;
        for (auto& d : a.effects.defs)
            if (fxMatches(*d, q)) effectItem(a, d.get(), true), found++;
        if (!found) ImGui::TextDisabled("%s", T("Nothing found"));
    } else {
        ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
        if (ImGui::TreeNodeEx("* Animation Presets", ImGuiTreeNodeFlags_SpanAvailWidth, "* %s  (%d)", T("Animation Presets"), (int)presets.size())) {
            for (size_t i = 0; i < presets.size(); i++) {
                ImGui::PushID((int)i);
                bool user = startsWith(toLower(presets[i]), toLower(a.presetDir()));
                if (ImGui::Selectable(fileStem(presets[i]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
                    a.applyPreset(presets[i]);
                if (ImGui::BeginDragDropSource()) {
                    int idx = (int)i;
                    ImGui::SetDragDropPayload(DND_PRESET, &idx, sizeof(idx));
                    ImGui::Text("%s", fileStem(presets[i]).c_str());
                    ImGui::EndDragDropSource();
                }
                if (user && ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete Preset")) DeleteFileW(widen(presets[i]).c_str());
                    if (ImGui::MenuItem("Show in Explorer")) shellOpen(parentDir(presets[i]));
                    ImGui::EndPopup();
                }
                tooltip(user ? "Your preset. Double-click to apply." : "Built-in preset. Double-click to apply.");
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        for (auto& cat : a.effects.categories) {
            auto list = a.effects.inCategory(cat);
            if (ImGui::TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%d)", T(cat.c_str()), (int)list.size())) {
                for (auto* d : list) effectItem(a, d, false);
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
    int plug = 0;
    for (auto& d : a.effects.defs) if (d->plugin) plug++;
    ImGui::TextDisabled("%d effects · %d plugin", (int)a.effects.defs.size(), plug);
    ImGui::SameLine(ImGui::GetWindowWidth() - S(34));
    if (iconButton("plg", Icon::Plug, false, "Plugin Manager")) a.dlgPlugins = true;
}

void focusEffectSearch(App& a) {
    a.pEffects = true;
    a.focusPanel = "Effects & Presets";
    g_fxFocus = true;
}

// ---------------------------------------------------------------------------
static void paramRow(App& a, Layer& l, Prop& p, const char* label, const PropUI& ui, double lt, float labelW) {
    ImGui::PushID(&p);
    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
    ImGui::TableNextColumn();
    ImGui::Indent(S(14));
    if (stopwatch("sw", p, lt)) a.markChanged();
    ImGui::SameLine(0, S(4));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (p.animated) {
        ImGui::SameLine(labelW - ImGui::GetFrameHeight() * 2.6f);
        Composition* c = a.comp();
        if (keyNav("kn", p, lt, l.startTime, c ? c->fps : 30)) a.markChanged();
    }
    ImGui::Unindent(S(14));
    ImGui::TableNextColumn();
    propValue("v", p, lt, ui, ImGui::GetContentRegionAvail().x);
    ImGui::PopID();
}

// Audio & Voice block: mute, volume, fades, speed with or without the pitch change,
// pitch on its own, voice effects, clean-up, extract / save
static void audioBlock(App& a, Layer& l, Composition& c) {
    Footage* f = l.type == LayerType::Footage ? a.project->findFootage(l.source) : nullptr;
    bool footageAudio = f && f->hasAudio;
    if (!footageAudio && l.type != LayerType::Precomp) return;
    double lt = l.localTime(c.time);
    ImVec2 hp = ImGui::GetCursorScreenPos();
    float avail0 = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(hp, hp + ImVec2(avail0, ImGui::GetFrameHeight() + S(2)), pal::Bg3);
    ImGui::SetCursorScreenPos(hp + ImVec2(S(4), S(1)));
    if (iconButton("autw", l.audioOpen ? Icon::ChevronDown : Icon::ChevronRight, false)) l.audioOpen = !l.audioOpen;
    ImGui::SameLine(0, S(4));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Audio & Voice");
    if (audioFxBusy()) {
        ImGui::SameLine();
        ImGui::TextColored(pal::v4(pal::Accent), "%s", T("processing..."));
    }
    ImGui::SameLine(avail0 - S(34));
    if (iconButton("aumute", Icon::Speaker, l.audioOn, l.audioOn ? "Mute this clip's sound" : "Turn this clip's sound back on")) {
        bool on = !l.audioOn;
        for (auto* sl : a.selectedLayers()) sl->audioOn = on;
        l.audioOn = on;
        a.markChanged();
    }
    if (!l.audioOpen) { ImGui::Dummy(ImVec2(0, S(2))); return; }
    if (!ImGui::BeginTable("au", 2, ImGuiTableFlags_SizingFixedFit)) return;
    float labelW = std::max(S(150), avail0 * 0.46f);
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    auto row = [&](const char* label) {
        ImGui::TableNextRow(0, ImGui::GetFrameHeight());
        ImGui::TableNextColumn();
        ImGui::Indent(S(14));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::Unindent(S(14));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-S(8));
    };
    row("Sound");
    if (ImGui::Checkbox("##auon", &l.audioOn)) a.markChanged();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T(l.audioOn ? "on" : "muted"));
    if (f && f->hasVideo) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Extract Audio")) a.extractAudio(l);
        tooltip("Moves the sound into its own audio layer under the clip and mutes the video");
    }
    if (f) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Save As...")) a.saveLayerAudio(l);
        tooltip("Save the clip's sound as WAV, MP3, M4A or FLAC (with speed and voice effects)");
    }
    PropUI db;
    db.kind = 9;
    db.speed = 0.1f;
    db.mn = -96;
    db.mx = 24;
    paramRow(a, l, l.volume, "Volume", db, lt, labelW);
    double dur = std::max(0.1, l.outPoint - l.inPoint);
    row("Sound Fade In");
    if (ImGui::SliderFloat("##fin", &l.audioFadeIn, 0, (float)std::min(10.0, dur * 0.5), "%.1f s")) a.markChanged();
    row("Sound Fade Out");
    if (ImGui::SliderFloat("##fout", &l.audioFadeOut, 0, (float)std::min(10.0, dur * 0.5), "%.1f s")) a.markChanged();
    if (footageAudio) {
        row("Loudness");
        if (ImGui::SmallButton("Normalize")) {
            // peak of the part the clip plays -> -1 dB
            float peak = 0;
            if (f->audio && f->audio->ready) {
                const auto& s = f->audio->samples;
                int64_t total = (int64_t)s.size() / 2;
                double a0 = (l.inPoint - l.startTime) * l.speed, a1 = (l.outPoint - l.startTime) * l.speed;
                int64_t i0 = clampv((int64_t)(std::min(a0, a1) * AudioData::kRate), (int64_t)0, total);
                int64_t i1 = clampv((int64_t)(std::max(a0, a1) * AudioData::kRate), i0, total);
                for (int64_t i = i0 * 2; i < i1 * 2; i++) peak = std::max(peak, fabsf(s[i]));
            }
            if (peak > 1e-5f) {
                float target = -1.f - 20.f * log10f(peak);
                if (l.volume.animated) {
                    float d = target - l.volume.f(lt);
                    for (auto& k : l.volume.keys) k.v[0] += d;
                } else {
                    l.volume.v[0] = target;
                }
                a.markChanged();
                a.notify(strf(T("Volume set to %+.1f dB (peak -1 dB)"), target));
            } else {
                a.notify("The clip is silent", true);
            }
        }
        tooltip("Raises or lowers the volume so the loudest moment of the clip reaches -1 dB");

        // ---- speed: slow down only, or together with the pitch ("capture" like a record)
        row("Speed");
        float pct = (float)(l.speed * 100);
        ImGui::SetNextItemWidth(S(110));
        if (ImGui::DragFloat("##auspd", &pct, 0.5f, 5, 1000, "%.0f %%")) {
            for (auto* sl : a.selectedLayers()) a.setLayerSpeed(*sl, pct / 100.0);
            a.markChanged();
        }
        for (float p : {50.f, 100.f, 200.f}) {
            ImGui::SameLine(0, S(3));
            if (ImGui::SmallButton(strf("%gx", p / 100).c_str())) {
                for (auto* sl : a.selectedLayers()) a.setLayerSpeed(*sl, p / 100.0);
                a.markChanged();
            }
        }
        row("Speed Mode");
        int mode = l.keepPitch ? 0 : 1;
        if (ImGui::RadioButton("Keep pitch", &mode, 0) | (ImGui::SameLine(), ImGui::RadioButton("Pitch follows speed", &mode, 1))) {
            for (auto* sl : a.selectedLayers()) sl->keepPitch = mode == 0;
            l.keepPitch = mode == 0;
            a.markChanged();
        }
        tooltip("Keep pitch: the sound only slows down / speeds up, the voice stays the same.\nPitch follows speed: like a record or tape, a slower clip sounds lower.");
        row("Pitch");
        if (ImGui::SliderFloat("##aupitch", &l.pitch, -12, 12, "%+.1f st")) a.markChanged();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { l.pitch = 0; a.markChanged(); }
        tooltip("Voice pitch in semitones, independent of speed (double-click resets)");
        // ---- voice effects
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Indent(S(14));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Voice Effect");
        ImGui::Unindent(S(14));
        ImGui::TableNextColumn();
        float bx0 = ImGui::GetCursorPosX(), colW = ImGui::GetContentRegionAvail().x - S(8), x = 0;
        for (int i = 0; i < kVoicePresetCount; i++) {
            const char* nm = T(kVoicePresets[i].name);
            float bw = ImGui::CalcTextSize(nm).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0 && x + bw <= colW) ImGui::SameLine(0, S(4));
            else if (i > 0) { x = 0; ImGui::SetCursorPosX(bx0); }
            bool on = l.voice == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
            if (ImGui::SmallButton(strf("%s##vp%d", nm, i).c_str())) {
                for (auto* sl : a.selectedLayers()) sl->voice = i;
                l.voice = i;
                a.markChanged();
            }
            if (on) ImGui::PopStyleColor();
            x += bw + S(4);
        }
        row("Clean Up");
        if (ImGui::Checkbox("Noise Reduction", &l.denoise)) a.markChanged();
        ImGui::SameLine();
        if (ImGui::Checkbox("Enhance Voice", &l.enhanceVoice)) a.markChanged();
        tooltip("Noise Reduction removes steady hiss / hum. Enhance Voice cuts rumble, adds clarity and evens out the level.");
    } else {
        row("");
        ImGui::TextDisabled("%s", T("Voice effects work on video and audio clips"));
    }
    ImGui::EndTable();
    ImGui::Dummy(ImVec2(0, S(4)));
}

// camera and light layers: their own controls instead of Transform / effects
static void cameraLightBlock(App& a, Layer& l, Composition& c) {
    double lt = l.localTime(c.time);
    bool cam = l.type == LayerType::Camera;
    ImGui::SeparatorText(cam ? T("Camera") : T("Light"));
    if (!ImGui::BeginTable("cl", 2, ImGuiTableFlags_SizingFixedFit)) return;
    float avail = ImGui::GetContentRegionAvail().x, labelW = std::max(S(150), avail * 0.46f);
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    auto plainRow = [&](const char* label) {
        ImGui::TableNextRow(0, ImGui::GetFrameHeight());
        ImGui::TableNextColumn();
        float ind = S(18) + ImGui::GetFrameHeight();
        ImGui::Indent(ind);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::Unindent(ind);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-S(8));
    };
    PropUI v2;
    v2.kind = 1;
    PropUI px;
    px.kind = 0;
    px.speed = 1;
    px.mn = -100000;
    px.mx = 100000;
    px.unit = "px";
    PropUI ang;
    ang.kind = 5;
    ang.speed = 0.5f;
    PropUI pct;
    pct.kind = 6;
    pct.speed = 0.5f;
    pct.mn = 0;
    pct.mx = 1000;
    if (!cam) {
        plainRow("Light Type");
        int k = l.lightKind;
        if (ImGui::Combo("##lk", &k, [](void*, int i) { return T(kLightKindNames[i]); }, nullptr, 4)) { l.lightKind = k; a.markChanged(); }
        paramRow(a, l, l.lightColor, "Color", [] { PropUI u; u.kind = 2; return u; }(), lt, labelW);
        paramRow(a, l, l.lightIntensity, "Intensity", pct, lt, labelW);
    }
    if (cam || l.lightKind != LK_Ambient) {
        paramRow(a, l, l.position, "Position", v2, lt, labelW);
        paramRow(a, l, l.posZ, "Position Z", px, lt, labelW);
        plainRow("Point of Interest");
        if (ImGui::Checkbox("##usetg", &l.useTarget)) a.markChanged();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", T(l.useTarget ? "looks at the point" : "uses its rotation"));
        if (l.useTarget) {
            paramRow(a, l, l.target, "Point of Interest", v2, lt, labelW);
            paramRow(a, l, l.targetZ, "Point of Interest Z", px, lt, labelW);
        }
        if (cam || !l.useTarget) {
            paramRow(a, l, l.rotX, "X Rotation", ang, lt, labelW);
            paramRow(a, l, l.rotY, "Y Rotation", ang, lt, labelW);
            if (cam) paramRow(a, l, l.rotation, "Z Rotation", ang, lt, labelW);
        }
    }
    if (cam) {
        PropUI zm = px;
        zm.mn = 1;
        paramRow(a, l, l.camZoom, "Zoom", zm, lt, labelW);
        plainRow("Lens");
        static const float kLens[] = {15, 20, 24, 28, 35, 50, 80, 135, 200};
        float mm = l.camZoom.f(lt) / std::max(1, c.w) * 36.f;
        if (ImGui::BeginCombo("##lens", strf("%.0f mm", mm).c_str())) {
            for (float f : kLens)
                if (ImGui::Selectable(strf("%.0f mm", f).c_str(), fabsf(mm - f) < 0.5f)) {
                    float z = f / 36.f * c.w;
                    l.camZoom.setF(lt, z);
                    a.markChanged();
                }
            ImGui::EndCombo();
        }
        plainRow("Depth of Field");
        if (ImGui::Checkbox("##dof", &l.dof)) a.markChanged();
        if (l.dof) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Focus on Point of Interest")) {
                ImVec2 p = l.position.v2(lt), tg = l.target.v2(lt);
                float d = (Vec3(tg.x, tg.y, l.targetZ.f(lt)) - Vec3(p.x, p.y, l.posZ.f(lt))).len();
                l.focusDist.setF(lt, d);
                a.markChanged();
            }
            PropUI fd = px;
            fd.mn = 1;
            paramRow(a, l, l.focusDist, "Focus Distance", fd, lt, labelW);
            PropUI ap = px;
            ap.mn = 0;
            ap.mx = 2000;
            paramRow(a, l, l.aperture, "Aperture", ap, lt, labelW);
            paramRow(a, l, l.blurLevel, "Blur Level", pct, lt, labelW);
        }
    } else {
        if (l.lightKind == LK_Spot) {
            PropUI ca = ang;
            ca.mn = 1;
            ca.mx = 179;
            paramRow(a, l, l.coneAngle, "Cone Angle", ca, lt, labelW);
            PropUI cf = pct;
            cf.mx = 100;
            paramRow(a, l, l.coneFeather, "Cone Feather", cf, lt, labelW);
        }
        if (l.lightKind == LK_Point || l.lightKind == LK_Spot) {
            PropUI rr = px;
            rr.mn = 0;
            paramRow(a, l, l.lightRadius, "Radius", rr, lt, labelW);
            paramRow(a, l, l.lightFalloff, "Falloff Distance", rr, lt, labelW);
        }
    }
    ImGui::EndTable();
    bool any3D = false;
    for (auto& o : c.layers) if (o->threeD) any3D = true;
    if (!any3D) {
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextWrapped("%s", T("Cameras and lights only affect 3D layers. Turn on the cube switch of a layer on the timeline."));
    }
}

void panelEffectControls(App& a) {
    Layer* l = a.selectedLayer();
    Composition* c = a.comp();
    std::string title = l ? std::string(T("Effect Controls")) + ": " + l->name : std::string(T("Effect Controls"));
    if (!ImGui::Begin((title + "###Effect Controls").c_str(), &a.pFxControls)) { ImGui::End(); return; }
    if (!l || !c) {
        if (c) {
            // nothing selected: canvas settings of the composition
            ImGui::TextDisabled("%s  ·  %d × %d  ·  %g fps", c->name.c_str(), c->w, c->h, c->fps);
            backgroundEditor(a, *c);
            ImGui::Dummy(ImVec2(0, S(8)));
        }
        ImGui::TextDisabled("Select a layer to see its effects.");
        ImGui::TextDisabled("Apply effects from Effects & Presets or the Effect menu.");
        ImGui::End();
        return;
    }
    double lt = l->localTime(c->time);
    ImVec2 ls = a.project->layerSize(*l, *c);
    ImGui::TextDisabled("%s  ·  %s%s", c->name.c_str(), T(kLayerTypeNames[(int)l->type]), l->threeD ? "  ·  3D" : "");
    ImGui::Separator();
    if (l->type == LayerType::Camera || l->type == LayerType::Light) {
        cameraLightBlock(a, *l, *c);
        ImGui::End();
        return;
    }
    // Transform block: pixel width/height on top of the scale %
    {
        static bool xfOpen = true, lockAspect = true;
        ImVec2 hp = ImGui::GetCursorScreenPos();
        float avail0 = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(hp, hp + ImVec2(avail0, ImGui::GetFrameHeight() + S(2)), pal::Bg3);
        ImGui::SetCursorScreenPos(hp + ImVec2(S(4), S(1)));
        if (iconButton("xftw", xfOpen ? Icon::ChevronDown : Icon::ChevronRight, false)) xfOpen = !xfOpen;
        ImGui::SameLine(0, S(4));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Transform");
        if (l->type != LayerType::Adjustment) {
            ImGui::SameLine(avail0 - S(64));
            if (iconButton("xf3d", Icon::Cube, l->threeD, "3D Layer: depth (Z), X / Y rotation, camera and lights (Ctrl+Alt+3)")) {
                l->threeD = !l->threeD;
                a.markChanged();
            }
        }
        ImGui::SameLine(avail0 - S(34));
        if (iconButton("xfreset", Icon::Reset, false, "Reset transform")) {
            l->anchor = Prop(2, ls.x * 0.5f, ls.y * 0.5f);
            l->position = Prop(2, c->w * 0.5f, c->h * 0.5f);
            l->scale = Prop(2, 100, 100);
            l->rotation = Prop(1, 0);
            l->opacity = Prop(1, 100);
            a.markChanged();
        }
        if (xfOpen && ls.x > 0 && ImGui::BeginTable("xf", 2, ImGuiTableFlags_SizingFixedFit)) {
            float labelW0 = std::max(S(150), avail0 * 0.46f);
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW0);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            PropUI v2;
            v2.kind = 1;
            PropUI zpx;
            zpx.kind = 0;
            zpx.speed = 1;
            zpx.mn = -100000;
            zpx.mx = 100000;
            zpx.unit = "px";
            paramRow(a, *l, l->position, "Position", v2, lt, labelW0);
            if (l->threeD) paramRow(a, *l, l->posZ, "Position Z", zpx, lt, labelW0);
            paramRow(a, *l, l->anchor, "Anchor Point", v2, lt, labelW0);
            // size in pixels <-> scale %
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            ImGui::Indent(S(14));
            if (stopwatch("swsz", l->scale, lt)) a.markChanged();
            ImGui::SameLine(0, S(4));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Size (W × H)");
            ImGui::Unindent(S(14));
            ImGui::TableNextColumn();
            ImVec2 sc = l->scale.v2(lt);
            float wpx = ls.x * sc.x / 100.f, hpx = ls.y * sc.y / 100.f;
            bool chW = scrubFloat("##wpx", &wpx, 1.f, -100000.f, 100000.f, "%.0f px");
            ImGui::SameLine(0, S(2));
            ImGui::TextDisabled("×");
            ImGui::SameLine(0, S(2));
            bool chH = scrubFloat("##hpx", &hpx, 1.f, -100000.f, 100000.f, "%.0f px");
            ImGui::SameLine(0, S(6));
            iconToggle("lockar", Icon::Link, &lockAspect, "Lock aspect ratio (turn off to stretch the object)");
            if (chW || chH) {
                float nx = wpx / ls.x * 100.f, ny = hpx / ls.y * 100.f;
                if (lockAspect) {
                    if (chW && fabsf(sc.x) > 1e-4f) ny = sc.y * nx / sc.x;
                    if (chH && fabsf(sc.y) > 1e-4f) nx = sc.x * ny / sc.y;
                }
                l->scale.set2(lt, nx, ny);
                a.markChanged();
            }
            PropUI s8;
            s8.kind = 8;
            s8.speed = 0.25f;
            s8.mn = -10000;
            s8.mx = 10000;
            paramRow(a, *l, l->scale, "Scale", s8, lt, labelW0);
            PropUI r5;
            r5.kind = 5;
            r5.speed = 0.5f;
            if (l->threeD) {
                paramRow(a, *l, l->rotX, "X Rotation", r5, lt, labelW0);
                paramRow(a, *l, l->rotY, "Y Rotation", r5, lt, labelW0);
            }
            paramRow(a, *l, l->rotation, l->threeD ? "Z Rotation" : "Rotation", r5, lt, labelW0);
            PropUI o6;
            o6.kind = 6;
            o6.speed = 0.5f;
            o6.mn = 0;
            o6.mx = 100;
            paramRow(a, *l, l->opacity, "Opacity", o6, lt, labelW0);
            if (l->threeD && l->type != LayerType::Null) {
                // material: how the layer takes the lights of the comp
                ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                ImGui::TableNextColumn();
                ImGui::Indent(S(14));
                if (ImGui::Checkbox("Accepts Lights", &l->acceptsLights)) a.markChanged();
                ImGui::Unindent(S(14));
                ImGui::TableNextColumn();
                bool anyLight = false;
                for (auto& o : c->layers) if (o->type == LayerType::Light) anyLight = true;
                if (!anyLight) {
                    ImGui::TextDisabled("%s", T("no lights in the comp"));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Add Light")) a.addLight(*c, LK_Point);
                }
                if (l->acceptsLights && anyLight) {
                    PropUI pc;
                    pc.kind = 6;
                    pc.speed = 0.5f;
                    pc.mn = 0;
                    pc.mx = 500;
                    paramRow(a, *l, l->matAmbient, "Ambient", pc, lt, labelW0);
                    paramRow(a, *l, l->matDiffuse, "Diffuse", pc, lt, labelW0);
                    paramRow(a, *l, l->matSpecular, "Specular", pc, lt, labelW0);
                    pc.mx = 100;
                    paramRow(a, *l, l->matShininess, "Shininess", pc, lt, labelW0);
                }
            }
            if (l->type != LayerType::Null && l->type != LayerType::Adjustment) {
                // one-click video fades (Fade transition underneath, its duration is editable below)
                ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                ImGui::TableNextColumn();
                float ind = S(18) + ImGui::GetFrameHeight();  // lines up with the labels that have a stopwatch
                ImGui::Indent(ind);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Fade In / Out");
                ImGui::Unindent(ind);
                ImGui::TableNextColumn();
                int fm = a.fadeMode(*l);
                bool fin = fm == 1 || fm == 3, fout = fm == 2 || fm == 3;
                if (fin) ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
                if (ImGui::SmallButton("Fade In##q")) a.toggleFade(*l, false);
                if (fin) ImGui::PopStyleColor();
                tooltip("The clip appears from transparent at its start");
                ImGui::SameLine(0, S(4));
                if (fout) ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
                if (ImGui::SmallButton("Fade Out##q")) a.toggleFade(*l, true);
                if (fout) ImGui::PopStyleColor();
                tooltip("The clip fades away at its end");
                EffectInst* fe = a.findFade(*l);
                Prop* fto = fe ? a.effectParam(*fe, "fadeTo") : nullptr;
                Prop* fcol = fe ? a.effectParam(*fe, "fadeColor") : nullptr;
                Prop* fdur = fe ? a.effectParam(*fe, "tdur") : nullptr;
                if (fto && fcol && fdur) {
                    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    ImGui::Indent(S(30) + ImGui::GetFrameHeight());
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Fade Color");
                    ImGui::Unindent(S(30) + ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    static const char* kFadeTo[] = {"Transparent", "Black", "White", "Custom Color"};
                    int ft = clampv((int)lrintf(fto->v[0]), 0, 3);
                    for (int i = 0; i < 4; i++) {
                        if (i) ImGui::SameLine(0, S(3));
                        bool on = ft == i;
                        if (on) ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
                        if (ImGui::SmallButton(strf("%s##ft%d", T(kFadeTo[i]), i).c_str())) { fto->v[0] = (float)i; a.markChanged(); }
                        if (on) ImGui::PopStyleColor();
                    }
                    if (ft == 3) {
                        ImGui::SameLine(0, S(6));
                        if (ImGui::ColorEdit3("##fcol", fcol->v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) a.markChanged();
                    }
                    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    ImGui::Indent(S(30) + ImGui::GetFrameHeight());
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Fade Duration");
                    ImGui::Unindent(S(30) + ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-S(8));
                    if (ImGui::SliderFloat("##fdur", &fdur->v[0], 0.04f, 5.f, "%.2f s")) a.markChanged();
                }
            }
            // motion blur
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            ImGui::Indent(S(14));
            if (ImGui::Checkbox("Motion Blur", &l->motionBlur)) a.markChanged();
            ImGui::Unindent(S(14));
            ImGui::TableNextColumn();
            if (!c->motionBlur) {
                ImGui::TextDisabled("master switch is off");
                ImGui::SameLine();
                if (ImGui::SmallButton("Turn on")) { c->motionBlur = true; a.markChanged(); }
            }
            if (l->motionBlur) {
                auto mbRow = [&](const char* label) {
                    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    ImGui::Indent(S(30) + ImGui::GetFrameHeight());
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::Unindent(S(30) + ImGui::GetFrameHeight());
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-S(8));
                };
                mbRow("Blur");
                if (ImGui::SliderFloat("##mbblur", &l->mbBlur, 0, 100, "%.0f %%")) a.markChanged();
                mbRow("Blend");
                if (ImGui::SliderFloat("##mbblend", &l->mbBlend, 0, 100, "%.0f %%")) a.markChanged();
                mbRow("Direction");
                static const char* dirs[] = {"Both Sides", "Left (behind)", "Right (ahead)"};
                if (ImGui::Combo("##mbdir", &l->mbDir, dirs, 3)) a.markChanged();
                mbRow("Times");
                if (ImGui::SliderInt("##mbtimes", &l->mbSamples, 2, 16, "%dx")) a.markChanged();
                // copy these settings to other clips
                ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                auto applyMb = [&](bool all) {
                    int n = 0;
                    for (auto& lp : c->layers) {
                        if (lp.get() == l || (!all && !a.selLayers.count(lp->id))) continue;
                        if (lp->type == LayerType::Null || lp->type == LayerType::Adjustment) continue;
                        lp->motionBlur = true;
                        lp->mbBlur = l->mbBlur;
                        lp->mbBlend = l->mbBlend;
                        lp->mbDir = l->mbDir;
                        lp->mbSamples = l->mbSamples;
                        n++;
                    }
                    c->motionBlur = true;
                    a.markChanged();
                    a.notify(strf(T("Motion blur applied to %d clip(s)"), n + 1));
                };
                int others = 0;
                for (auto id : a.selLayers) if (id != l->id) others++;
                ImGui::BeginDisabled(others == 0);
                if (ImGui::SmallButton(strf("%s (%d)##mbsel", T("Apply to Selected"), others + 1).c_str())) applyMb(false);
                ImGui::EndDisabled();
                tooltip("Select several clips on the timeline (Ctrl+click or drag a box), then apply these motion blur settings to all of them");
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply to All Clips")) applyMb(true);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Fit to Comp")) a.fitLayer(*l, false);
            ImGui::SameLine();
            if (ImGui::SmallButton("Fill Comp")) a.fitLayer(*l, true);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(T("source %d × %d px"), (int)ls.x, (int)ls.y);
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    audioBlock(a, *l, *c);
    int moveFrom = -1, moveTo = -1, removeIdx = -1, dupIdx = -1;
    float avail = ImGui::GetContentRegionAvail().x;
    float labelW = std::max(S(150), avail * 0.46f);
    for (int ei = 0; ei < (int)l->effects.size(); ei++) {
        EffectInst& e = l->effects[ei];
        const EffectDef* d = a.effects.find(e.defId);
        ImGui::PushID((int)(e.id & 0x7fffffff));
        // header
        ImVec2 hp = ImGui::GetCursorScreenPos();
        float hh = ImGui::GetFrameHeight() + S(2);
        bool selected = a.selEffect == e.id;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(hp, hp + ImVec2(avail, hh), selected ? pal::AccentDark : pal::Bg3);
        if (selected) dl->AddRectFilled(hp, hp + ImVec2(S(2), hh), pal::Accent);
        ImGui::SetCursorScreenPos(hp + ImVec2(S(4), S(1)));
        if (iconButton("en", Icon::Fx, e.enabled, "Effect on/off")) { e.enabled = !e.enabled; a.markChanged(); }
        ImGui::SameLine(0, S(2));
        if (iconButton("tw", e.expanded ? Icon::ChevronDown : Icon::ChevronRight, false)) e.expanded = !e.expanded;
        ImGui::SameLine(0, S(4));
        ImGui::AlignTextToFramePadding();
        ImVec2 np = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("hdr", ImVec2(std::max(S(40), avail - S(120)), ImGui::GetFrameHeight()));
        if (ImGui::IsItemClicked()) a.selEffect = e.id;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) e.expanded = !e.expanded;
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GESE_FXMOVE", &ei, sizeof(ei));
            ImGui::Text("Move %s", e.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("GESE_FXMOVE")) { moveFrom = *(const int*)pl->Data; moveTo = ei; }
            ImGui::EndDragDropTarget();
        }
        dl->AddText(np + ImVec2(0, ImGui::GetStyle().FramePadding.y), e.enabled ? pal::White : pal::TextDim, e.name.c_str());
        if (d && d->plugin) dl->AddText(np + ImVec2(ImGui::CalcTextSize(e.name.c_str()).x + S(8), ImGui::GetStyle().FramePadding.y), pal::AccentA(200), "plugin");
        if (!d) dl->AddText(np + ImVec2(ImGui::CalcTextSize(e.name.c_str()).x + S(8), ImGui::GetStyle().FramePadding.y), IM_COL32(220, 90, 90, 255), "missing");
        ImGui::SameLine(avail - S(58));
        if (iconButton("rst", Icon::Reset, false, "Reset") && d) {
            EffectInst fresh = a.effects.instantiate(*d);
            fresh.id = e.id;
            fresh.enabled = e.enabled;
            e = fresh;
            a.markChanged();
        }
        ImGui::SameLine(0, S(2));
        if (iconButton("del", Icon::Close, false, "Remove effect")) removeIdx = ei;
        if (ImGui::BeginPopupContextItem("ectx")) {
            if (ImGui::MenuItem("Duplicate")) dupIdx = ei;
            if (ImGui::MenuItem("Move Up", nullptr, false, ei > 0)) { moveFrom = ei; moveTo = ei - 1; }
            if (ImGui::MenuItem("Move Down", nullptr, false, ei + 1 < (int)l->effects.size())) { moveFrom = ei; moveTo = ei + 1; }
            if (ImGui::MenuItem("Copy")) { a.clipEffects = Json::array(); a.clipEffects.push(e.save()); }
            if (ImGui::MenuItem("Paste", nullptr, false, a.clipEffects.size() > 0)) {
                for (auto& ej : a.clipEffects.a) {
                    EffectInst ne;
                    ne.load(ej);
                    ne.id = newId();
                    a.effects.sync(ne);
                    l->effects.push_back(ne);
                }
                a.markChanged();
            }
            if (ImGui::MenuItem("Save as Animation Preset...")) a.dlgSavePreset = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Remove")) removeIdx = ei;
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(hp + ImVec2(0, hh + S(1)));
        if (e.expanded) {
            if (ImGui::BeginTable("p", 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                PropUI iu;
                iu.kind = 6;
                iu.speed = 0.5f;
                iu.mn = 0;
                iu.mx = d && !d->autoMix ? 200.f : 100.f;
                paramRow(a, *l, e.intensity, "Intensity", iu, lt, labelW);
                if (d && d->animated) {
                    PropUI su;
                    su.kind = 0;
                    su.speed = 0.01f;
                    su.mn = 0;
                    su.mx = 20;
                    su.unit = "x";
                    paramRow(a, *l, e.speed, "Speed", su, lt, labelW);
                }
                if (d) {
                    for (size_t i = 0; i < d->params.size() && i < e.params.size(); i++) {
                        PropUI u = uiForParam(d->params[i], ls);
                        paramRow(a, *l, e.params[i], d->params[i].label.c_str(), u, lt, labelW);
                    }
                    if (d->params.empty() && !d->timeop.empty()) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("  time operator");
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::Dummy(ImVec2(0, S(3)));
        ImGui::PopID();
    }
    if (l->effects.empty()) ImGui::TextDisabled("No effects on this layer.");
    ImGui::Dummy(ImVec2(0, S(6)));
    static char addSearch[64] = "";
    if (ImGui::Button("+ Add Effect", ImVec2(-1, 0))) { addSearch[0] = 0; ImGui::OpenPopup("addfx"); }
    if (ImGui::BeginPopup("addfx")) {
        // type to search (English or your language), or browse the categories
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(S(280));
        ImGui::InputTextWithHint("##addfxs", T("Search effects and presets"), addSearch, sizeof(addSearch));
        std::string q = toLower(trim(addSearch));
        ImGui::Separator();
        if (!q.empty()) {
            int shown = 0;
            ImGui::BeginChild("##addres", ImVec2(S(280), S(320)));
            for (auto& d : a.effects.defs) {
                if (!d->ok || !fxMatches(*d, q)) continue;
                std::string lbl = std::string(T(d->name.c_str())) + "  ";
                if (ImGui::Selectable(strf("%s##%s", lbl.c_str(), d->id.c_str()).c_str())) { a.applyEffect(d.get(), l); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", T(d->category.c_str()));
                shown++;
            }
            if (!shown) ImGui::TextDisabled("%s", T("Nothing found"));
            ImGui::EndChild();
            if (ImGui::IsKeyPressed(ImGuiKey_Enter))
                for (auto& d : a.effects.defs)
                    if (d->ok && fxMatches(*d, q)) { a.applyEffect(d.get(), l); ImGui::CloseCurrentPopup(); break; }
        } else {
            for (auto& cat : a.effects.categories) {
                if (ImGui::BeginMenu(T(cat.c_str()))) {
                    for (auto* d : a.effects.inCategory(cat))
                        if (d->ok && ImGui::MenuItem(d->name.c_str())) a.applyEffect(d, l);
                    ImGui::EndMenu();
                }
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_EFFECT)) {
            int idx = *(const int*)pl->Data;
            if (idx >= 0 && idx < (int)a.effects.defs.size()) a.applyEffect(a.effects.defs[idx].get(), l);
        }
        ImGui::EndDragDropTarget();
    }
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveTo < (int)l->effects.size()) {
        EffectInst e = l->effects[moveFrom];
        l->effects.erase(l->effects.begin() + moveFrom);
        l->effects.insert(l->effects.begin() + moveTo, e);
        a.markChanged();
    }
    if (dupIdx >= 0) {
        EffectInst e = l->effects[dupIdx];
        e.id = newId();
        l->effects.insert(l->effects.begin() + dupIdx + 1, e);
        a.markChanged();
    }
    // Delete in this panel removes the selected effect (not the whole layer)
    if (removeIdx < 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        for (int i = 0; i < (int)l->effects.size(); i++) if (l->effects[i].id == a.selEffect) removeIdx = i;
        if (removeIdx >= 0) a.deleteHandledFrame = ImGui::GetFrameCount();
    }
    if (removeIdx >= 0) {
        l->effects.erase(l->effects.begin() + removeIdx);
        a.markChanged();
        a.notify(T("Effect removed (Ctrl+Z brings it back)"));
    }
    ImGui::End();
}
