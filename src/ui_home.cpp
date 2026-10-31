#include "ui.h"
#include "gfx.h"

static void presetTile(App& a, int idx, float w, float h) {
    const CompPreset& p = kCompPresets[idx];
    ImGui::PushID(idx);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("tile", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, pos + ImVec2(w, h), hov ? pal::Bg4 : pal::Bg3, S(3));
    if (hov) dl->AddRect(pos, pos + ImVec2(w, h), pal::Accent, S(3), 0, S(1));
    // aspect frame
    float box = h * 0.48f;
    float ar = (float)p.w / p.h;
    ImVec2 fs = ar >= 1 ? ImVec2(box * 1.3f, box * 1.3f / ar) : ImVec2(box * ar, box);
    ImVec2 c = pos + ImVec2(w * 0.5f, h * 0.38f);
    dl->AddRect(c - fs * 0.5f, c + fs * 0.5f, hov ? pal::Accent : pal::TextDim, S(1), 0, S(1.5f));
    std::string l1 = strf("%d × %d", p.w, p.h);
    std::string name = p.name;
    std::string l2 = name.substr(0, name.find("  ·"));
    std::string l3 = strf("%g fps", p.fps);
    ImVec2 t1 = ImGui::CalcTextSize(l2.c_str());
    dl->AddText(ImVec2(pos.x + (w - t1.x) * 0.5f, pos.y + h * 0.66f), pal::Text, l2.c_str());
    std::string sub = l1 + "  ·  " + l3;
    ImVec2 t2 = ImGui::CalcTextSize(sub.c_str());
    dl->AddText(ImVec2(pos.x + (w - t2.x) * 0.5f, pos.y + h * 0.66f + ImGui::GetTextLineHeight() + S(2)), pal::TextDim, sub.c_str());
    if (clicked) {
        a.newProject();
        Composition* c2 = a.newComp(a.project->uniqueCompName("Comp"), p.w, p.h, p.fps, 30);
        (void)c2;
        a.settings.lastCompPreset = idx;
        a.resetUndo();
        a.dirty = false;
    }
    tooltip(p.name);
    ImGui::PopID();
}

void uiHome(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal::v4(pal::Bg1));
    ImGui::Begin("##home", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();

    // top bar
    float barH = S(52);
    dl->AddRectFilled(p0, p0 + ImVec2(sz.x, barH), pal::Bg2);
    dl->AddLine(p0 + ImVec2(0, barH), p0 + ImVec2(sz.x, barH), pal::Line);
    float logo = S(30);
    if (a.logoTex) dl->AddImage(a.logoTex->id(), p0 + ImVec2(S(18), (barH - logo) * 0.5f), p0 + ImVec2(S(18) + logo, (barH + logo) * 0.5f));
    ImGui::PushFont(a.fBold, a.settings.fontSize * 1.25f);
    dl->AddText(p0 + ImVec2(S(18) + logo + S(12), barH * 0.5f - ImGui::GetFontSize() * 0.55f), pal::White, "GeSe");
    float gw = ImGui::CalcTextSize("GeSe").x;
    ImGui::PopFont();
    dl->AddText(p0 + ImVec2(S(18) + logo + S(18) + gw, barH * 0.5f - ImGui::GetTextLineHeight() * 0.45f), pal::TextDim, GESE_VERSION);
    ImGui::SetCursorPos(ImVec2(sz.x - S(44), (barH - ImGui::GetFrameHeight()) * 0.5f));
    if (iconButton("prefs", Icon::Gear, false, "Preferences")) a.dlgPrefs = true;

    // left column
    float leftW = S(250);
    dl->AddRectFilled(p0 + ImVec2(0, barH + 1), p0 + ImVec2(leftW, sz.y), pal::Bg2);
    dl->AddLine(p0 + ImVec2(leftW, barH), p0 + ImVec2(leftW, sz.y), pal::Line);
    ImGui::SetCursorPos(ImVec2(S(20), barH + S(24)));
    ImGui::BeginGroup();
    float bw = leftW - S(40);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(8)));
    if (accentButton("New Project", ImVec2(bw, 0))) a.newProject();
    ImGui::Dummy(ImVec2(0, S(4)));
    if (ImGui::Button("New Composition...", ImVec2(bw, 0))) { a.newProject(); a.dlgNewComp = true; }
    ImGui::Dummy(ImVec2(0, S(4)));
    if (ImGui::Button("Open Project...", ImVec2(bw, 0))) a.doAction(2);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, S(26)));
    sectionLabel("RESOURCES");
    ImGui::Dummy(ImVec2(0, S(2)));
    if (ImGui::Selectable("Keyboard Shortcuts", false, 0, ImVec2(bw, 0))) a.dlgShortcuts = true;
    if (ImGui::Selectable("Plugin SDK (.gsfx)", false, 0, ImVec2(bw, 0))) a.dlgSdk = true;
    if (ImGui::Selectable("Plugin Manager", false, 0, ImVec2(bw, 0))) a.dlgPlugins = true;
    if (ImGui::Selectable("Open Plugins Folder", false, 0, ImVec2(bw, 0))) shellOpen(a.effects.userPluginDir());
    if (ImGui::Selectable("About GeSe", false, 0, ImVec2(bw, 0))) a.dlgAbout = true;
    ImGui::EndGroup();
    std::string gpu = gfx::adapterName();
    ImGui::SetCursorPos(ImVec2(S(20), sz.y - S(56)));
    ImGui::PushTextWrapPos(leftW - S(16));
    ImGui::TextDisabled("%s", gpu.c_str());
    ImGui::TextDisabled("%d effects  ·  %d categories", (int)a.effects.defs.size(), (int)a.effects.categories.size());
    ImGui::PopTextWrapPos();

    // main area
    float mx = leftW + S(36);
    float mw = sz.x - mx - S(36);
    ImGui::SetCursorPos(ImVec2(mx, barH + S(26)));
    ImGui::BeginGroup();
    ImGui::PushFont(a.fBold, a.settings.fontSize * 1.55f);
    ImGui::TextUnformatted("Start a new scene");
    ImGui::PopFont();
    ImGui::TextDisabled("Pick a format to create a project with a composition, or start empty with New Project.");
    ImGui::Dummy(ImVec2(0, S(8)));
    int cols = std::max(3, std::min(6, (int)(mw / S(170))));
    float tw = (mw - (cols - 1) * S(10)) / cols, th = S(118);
    const int featured[] = {0, 5, 10, 12, 13, 14, 3, 4, 7, 15, 11, 9};
    int shown = std::min<int>(cols * 2, (int)(sizeof(featured) / sizeof(featured[0])));
    for (int i = 0; i < shown; i++) {
        if (i % cols) ImGui::SameLine(0, S(10));
        presetTile(a, featured[i], tw, th);
    }
    ImGui::Dummy(ImVec2(0, S(22)));
    ImGui::PushFont(a.fBold, a.settings.fontSize * 1.55f);
    ImGui::TextUnformatted("Recent");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));
    float listH = std::max(S(120), sz.y - ImGui::GetCursorPosY() - S(30));
    if (a.settings.recent.empty()) {
        ImGui::TextDisabled("No recent projects yet. Create a new project or open an existing .gese file.");
    } else if (ImGui::BeginTable("recent", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable,
                                 ImVec2(mw, listH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn("Last modified", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.1f);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableHeadersRow();
        std::string toOpen, toRemove;
        for (auto& r : a.settings.recent) {
            bool exists = fileExists(r);
            ImGui::TableNextRow(0, S(26));
            ImGui::TableNextColumn();
            ImGui::PushID(r.c_str());
            if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::TextFaint));
            if (ImGui::Selectable(fileStem(r).c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, S(20)))) {
                toOpen = r;
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Open")) toOpen = r;
                if (ImGui::MenuItem("Show in Explorer")) shellOpen(parentDir(r));
                if (ImGui::MenuItem("Remove from list")) toRemove = r;
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", exists ? fmtDate(fileTime(r)).c_str() : "missing");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", exists ? fmtBytes(fileSize(r)).c_str() : "-");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", parentDir(r).c_str());
            if (!exists) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!toRemove.empty()) {
            auto& rv = a.settings.recent;
            rv.erase(std::remove(rv.begin(), rv.end(), toRemove), rv.end());
            a.settings.save();
        }
        if (!toOpen.empty()) {
            if (fileExists(toOpen)) a.openProject(toOpen);
            else a.notify("File not found: " + toOpen, true);
        }
    }
    ImGui::EndGroup();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    uiDialogs(a);
    uiToasts(a);
}
