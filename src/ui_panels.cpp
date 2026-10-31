#include "ui.h"
#include "i18n.h"
#include "textrender.h"

void panelInfo(App& a) {
    if (!ImGui::Begin("Info", &a.pInfo)) { ImGui::End(); return; }
    Composition* c = a.comp();
    if (ImGui::BeginTable("info", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        float* p = a.pickRGBA;
        float al = p[3] > 1e-5f ? p[3] : 1.f;
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "R: %d", (int)std::lround(clampv(p[0] / al, 0.f, 1.f) * 255));
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "G: %d", (int)std::lround(clampv(p[1] / al, 0.f, 1.f) * 255));
        ImGui::TextColored(ImVec4(0.45f, 0.6f, 0.95f, 1), "B: %d", (int)std::lround(clampv(p[2] / al, 0.f, 1.f) * 255));
        ImGui::Text("A: %d", (int)std::lround(clampv(p[3], 0.f, 1.f) * 255));
        ImGui::TableNextColumn();
        if (a.pickPos.x >= 0) {
            ImGui::Text("X: %d", (int)a.pickPos.x);
            ImGui::Text("Y: %d", (int)a.pickPos.y);
        } else {
            ImGui::TextDisabled("X: -");
            ImGui::TextDisabled("Y: -");
        }
        ImVec4 sw(clampv(p[0] / al, 0.f, 1.f), clampv(p[1] / al, 0.f, 1.f), clampv(p[2] / al, 0.f, 1.f), 1);
        ImGui::ColorButton("##pick", sw, ImGuiColorEditFlags_NoTooltip, ImVec2(S(40), S(18)));
        ImGui::EndTable();
    }
    ImGui::Separator();
    if (c) {
        ImGui::TextDisabled("%s", timeLabel(c->time, c->fps).c_str());
        if (Layer* l = a.selectedLayer()) {
            ImGui::Text("%s", l->name.c_str());
            ImGui::TextDisabled("In %s  Out %s", fmtTimecode(l->inPoint, c->fps).c_str(), fmtTimecode(l->outPoint, c->fps).c_str());
            ImGui::TextDisabled("Duration %s  ·  Speed %g%%", fmtTimecode(l->outPoint - l->inPoint, c->fps).c_str(), l->speed * 100);
        }
    }
    ImGui::End();
}

void panelPreview(App& a) {
    if (!ImGui::Begin("Preview", &a.pPreview)) { ImGui::End(); return; }
    Composition* c = a.comp();
    float bs = ImGui::GetFrameHeight() * 1.25f;
    float total = bs * 5 + S(8) * 4;
    ImGui::SetCursorPosX(std::max(S(4), (ImGui::GetWindowWidth() - total) * 0.5f));
    if (iconButton("start", Icon::ToStart, false, "First Frame (Home)", bs) && c) a.setTime(c->workStart);
    ImGui::SameLine(0, S(8));
    if (iconButton("prev", Icon::StepBack, false, "Previous Frame (Page Up)", bs)) a.stepFrames(-1);
    ImGui::SameLine(0, S(8));
    if (iconButton("play", a.playing ? Icon::Pause : Icon::Play, a.playing, "Play / Stop (Space)", bs)) a.togglePlay();
    ImGui::SameLine(0, S(8));
    if (iconButton("next", Icon::StepFwd, false, "Next Frame (Page Down)", bs)) a.stepFrames(1);
    ImGui::SameLine(0, S(8));
    if (iconButton("end", Icon::ToEnd, false, "Last Frame (End)", bs) && c) a.setTime(c->workEnd - 1.0 / c->fps);
    ImGui::Dummy(ImVec2(0, S(4)));
    iconToggle("loop", Icon::Loop, &a.settings.loop, "Loop playback");
    ImGui::SameLine();
    bool mute = a.audio.mute.load();
    if (iconToggle("mute", Icon::Speaker, &mute, "Mute audio")) a.audio.mute = mute;
    ImGui::SameLine();
    static const char* res[] = {"Auto", "Full", "Half", "Third", "Quarter"};
    ImGui::SetNextItemWidth(S(90));
    if (ImGui::Combo("Resolution", &a.settings.previewRes, res, 5)) a.needRender = true;
    if (c) {
        ImGui::TextDisabled("Work area %s – %s", fmtTimecode(c->workStart, c->fps).c_str(), fmtTimecode(c->workEnd, c->fps).c_str());
        ImGui::TextDisabled("B / N set work area start / end");
    }
    if (a.playing) ImGui::TextColored(pal::v4(pal::Accent), "%.1f fps (target %g)", a.measuredFps, c ? c->fps : 0.0);
    ImGui::End();
}

void panelAudio(App& a) {
    if (!ImGui::Begin("Audio", &a.pAudio)) { ImGui::End(); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 av = ImGui::GetContentRegionAvail();
    float h = std::max(S(60), av.y - ImGui::GetFrameHeight() - S(10));
    float bw = S(14);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto meter = [&](float x, float v) {
        float db = v > 1e-5f ? 20 * log10f(v) : -96.f;
        float k = clampv((db + 48.f) / 48.f, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + bw, p.y + h), pal::Bg0);
        dl->AddRectFilled(ImVec2(x, p.y + h * (1 - k)), ImVec2(x + bw, p.y + h), db > -3 ? IM_COL32(0xFF, 0x88, 0x01, 0xFF) : pal::Accent);
    };
    meter(p.x + S(4), a.audio.peakL.load());
    meter(p.x + S(6) + bw, a.audio.peakR.load());
    for (int db = 0; db >= -48; db -= 6) {
        float y = p.y + h * (-db / 48.f);
        dl->AddLine(ImVec2(p.x + S(8) + bw * 2, y), ImVec2(p.x + S(12) + bw * 2, y), pal::TextFaint);
        dl->AddText(ImVec2(p.x + S(15) + bw * 2, y - ImGui::GetTextLineHeight() * 0.5f), pal::TextDim, strf("%d", db).c_str());
    }
    ImGui::Dummy(ImVec2(av.x, h));
    float vol = a.audio.master.load();
    float vdb = vol > 1e-4f ? 20 * log10f(vol) : -60.f;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##master", &vdb, -60, 12, "Master %.1f dB")) a.audio.master = vdb <= -59.9f ? 0.f : powf(10, vdb / 20);
    if (!a.audio.deviceOk()) ImGui::TextDisabled("No audio device");
    ImGui::End();
}

void panelCharacter(App& a) {
    if (!ImGui::Begin("Character", &a.pCharacter)) { ImGui::End(); return; }
    Layer* l = a.selectedLayer();
    Composition* c = a.comp();
    if (!l || !c || l->type != LayerType::Text) {
        ImGui::TextDisabled("Select a text layer.");
        if (c && ImGui::Button("New Text Layer")) a.addText(*c, "Text", ImVec2(-1, -1));
        ImGui::End();
        return;
    }
    TextData& t = l->text;
    double lt = l->localTime(c->time);
    static char buf[4096];
    static uint64_t bufFor = 0;
    if (bufFor != l->id || !ImGui::IsAnyItemActive()) { snprintf(buf, sizeof(buf), "%s", t.text.c_str()); bufFor = l->id; }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextMultiline("##txt", buf, sizeof(buf), ImVec2(-1, S(70)))) {
        t.text = buf;
        std::string nm = t.text.substr(0, t.text.find('\n'));
        if (!nm.empty()) l->name = nm.substr(0, 40);
        a.markChanged();
    }
    auto fonts = textr::systemFonts();
    static char ff[64] = "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##font", t.font.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ff", T("Filter fonts"), ff, sizeof(ff));
        for (auto& f : fonts) {
            if (ff[0] && toLower(f).find(toLower(ff)) == std::string::npos) continue;
            if (ImGui::Selectable(f.c_str(), f == t.font)) { t.font = f; a.markChanged(); }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Bold", &t.bold)) a.markChanged();
    ImGui::SameLine();
    if (ImGui::Checkbox("Italic", &t.italic)) a.markChanged();
    ImGui::SameLine();
    if (ImGui::Checkbox("All Caps", &t.allCaps)) a.markChanged();
    static const char* al[] = {"Left", "Center", "Right"};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine(0, S(2));
        bool on = t.align == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::AccentDark));
        if (ImGui::Button(al[i], ImVec2(S(60), 0))) { t.align = i; a.markChanged(); }
        if (on) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    auto row = [&](const char* label, Prop& p, PropUI u) {
        ImGui::PushID(label);
        if (stopwatch("sw", p, lt)) a.markChanged();
        ImGui::SameLine(0, S(4));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(S(130));
        propValue("v", p, lt, u, S(120));
        ImGui::PopID();
    };
    PropUI fs; fs.kind = 0; fs.speed = 0.5f; fs.mn = 1; fs.mx = 2000; fs.unit = "px";
    row("Size", t.size, fs);
    PropUI col; col.kind = 2;
    row("Fill", t.fill, col);
    row("Stroke", t.stroke, col);
    PropUI sw; sw.kind = 0; sw.speed = 0.1f; sw.mn = 0; sw.mx = 200; sw.unit = "px";
    row("Stroke Width", t.strokeWidth, sw);
    PropUI tr; tr.kind = 0; tr.speed = 1; tr.mn = -500; tr.mx = 2000;
    row("Tracking", t.tracking, tr);
    PropUI ld; ld.kind = 0; ld.speed = 0.01f; ld.mn = 0.3f; ld.mx = 5; ld.unit = "x";
    row("Line Spacing", t.leading, ld);
    ImGui::End();
}

void panelRenderQueue(App& a) {
    if (!ImGui::Begin("Render Queue", &a.pRender)) { ImGui::End(); return; }
    if (!a.project) { ImGui::End(); return; }
    auto& q = a.project->queue;
    if (a.exporter.active()) {
        if (ImGui::Button("Stop", ImVec2(S(90), 0))) a.exporter.cancel();
        ImGui::SameLine();
        ImGui::ProgressBar(a.exporter.progress(), ImVec2(-1, 0),
                           strf("%s  ·  %d%%  ·  ETA %s", a.exporter.status().c_str(), (int)(a.exporter.progress() * 100),
                                a.exporter.eta() < 0 ? "--" : fmtTimeShort(a.exporter.eta()).c_str())
                               .c_str());
    } else {
        bool any = false;
        for (auto& it : q) if (it.enabled && it.status != 2) any = true;
        ImGui::BeginDisabled(!any);
        if (accentButton("Render", ImVec2(S(90), 0))) a.startRenderQueue();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Add Active Comp") && a.comp()) a.queueRender(a.comp()->id);
        ImGui::SameLine();
        if (ImGui::Button("Clear Finished")) {
            q.erase(std::remove_if(q.begin(), q.end(), [](const RenderItem& r) { return r.status == 2; }), q.end());
            a.markChanged();
        }
    }
    ImGui::Separator();
    if (q.empty()) ImGui::TextDisabled("Queue is empty. Composition > Add to Render Queue (Ctrl+M).");
    uint64_t removeId = 0;
    if (ImGui::BeginTable("rq", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Render", ImGuiTableColumnFlags_WidthFixed, S(54));
        ImGui::TableSetupColumn("Comp Name", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Output Module", ImGuiTableColumnFlags_WidthStretch, 0.27f);
        ImGui::TableSetupColumn("Output To", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.2f);
        ImGui::TableHeadersRow();
        for (auto& it : q) {
            ImGui::PushID((int)(it.id & 0x7fffffff));
            ImGui::TableNextRow(0, S(26));
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##en", &it.enabled)) a.markChanged();
            ImGui::TableNextColumn();
            Composition* c = a.project->findComp(it.comp);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(c ? c->name.c_str() : "(deleted)");
            ImGui::TableNextColumn();
            std::string om = strf("%s%s · %d%%", kFormatNames[clampv(it.out.format, 0, kFormatCount - 1)], it.out.encoder == 1 && it.out.format <= 1 ? " NVENC" : "",
                                  it.out.scalePct);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::Accent));
            if (ImGui::Selectable(om.c_str(), false)) { a.editRenderItem = it.id; a.dlgOutput = true; }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::Accent));
            if (ImGui::Selectable(fileName(it.out.path).c_str(), false)) {
                std::string ext = formatExtension(it.out.format);
                std::wstring filt = widen(std::string("Output (*") + ext + ")") + std::wstring(1, L'\0') + widen("*" + ext) + std::wstring(1, L'\0');
                std::string p = dlgSaveFile(a.hwnd, filt.c_str(), ext.c_str() + 1, fileStem(it.out.path), "Output To");
                if (!p.empty()) { it.out.path = p; a.markChanged(); }
            }
            ImGui::PopStyleColor();
            tooltip(it.out.path.c_str());
            ImGui::TableNextColumn();
            static const char* st[] = {"Queued", "Rendering", "Done", "Failed", "Stopped"};
            if (it.status == 1) ImGui::ProgressBar(it.progress, ImVec2(-1, 0));
            else {
                ImGui::AlignTextToFramePadding();
                if (it.status == 3) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%s", st[it.status]);
                else if (it.status == 2) ImGui::TextColored(pal::v4(pal::Accent), "%s", st[it.status]);
                else ImGui::TextDisabled("%s", st[clampv(it.status, 0, 4)]);
                if (!it.message.empty()) tooltip(it.message.c_str());
            }
            if (ImGui::BeginPopupContextItem("rqctx")) {
                if (ImGui::MenuItem("Output Settings...")) { a.editRenderItem = it.id; a.dlgOutput = true; }
                if (ImGui::MenuItem("Open Output Folder")) shellOpen(parentDir(it.out.path));
                if (ImGui::MenuItem("Play Result", nullptr, false, fileExists(it.out.path))) shellOpen(it.out.path);
                if (ImGui::MenuItem("Reset Status")) { it.status = 0; it.progress = 0; it.message.clear(); }
                if (ImGui::MenuItem("Remove", nullptr, false, it.status != 1)) removeId = it.id;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeId) {
        q.erase(std::remove_if(q.begin(), q.end(), [&](const RenderItem& r) { return r.id == removeId; }), q.end());
        a.markChanged();
    }
    ImGui::End();
}
