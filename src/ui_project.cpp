#include "ui.h"
#include "i18n.h"

static char g_search[128] = "";
static uint64_t g_renameId = 0;
static char g_renameBuf[256] = "";

static Icon footageIcon(const Footage& f) {
    switch (f.kind) {
    case FootageKind::Image: return Icon::Image;
    case FootageKind::Sequence: return Icon::Sequence;
    case FootageKind::Audio: return Icon::Audio;
    default: return Icon::Video;
    }
}

static bool matches(const std::string& name) {
    if (!g_search[0]) return true;
    return toLower(name).find(toLower(g_search)) != std::string::npos;
}

static void deleteItems(App& a) {
    Project& p = *a.project;
    for (uint64_t id : a.selItems) {
        for (auto& c : p.comps)
            c->layers.erase(std::remove_if(c->layers.begin(), c->layers.end(),
                                           [&](const LayerPtr& l) { return (l->type == LayerType::Footage || l->type == LayerType::Precomp) && l->source == id; }),
                            c->layers.end());
        p.footage.erase(std::remove_if(p.footage.begin(), p.footage.end(), [&](const FootagePtr& f) { return f->id == id; }), p.footage.end());
        if (std::find_if(p.comps.begin(), p.comps.end(), [&](const CompPtr& c) { return c->id == id; }) != p.comps.end()) {
            a.closeComp(id);
            p.comps.erase(std::remove_if(p.comps.begin(), p.comps.end(), [&](const CompPtr& c) { return c->id == id; }), p.comps.end());
            p.queue.erase(std::remove_if(p.queue.begin(), p.queue.end(), [&](const RenderItem& r) { return r.comp == id; }), p.queue.end());
        }
        for (auto& f : p.folders) if (f.parent == id) f.parent = 0;
        for (auto& f : p.footage) if (f->folder == id) f->folder = 0;
        for (auto& c : p.comps) if (c->folder == id) c->folder = 0;
        p.folders.erase(std::remove_if(p.folders.begin(), p.folders.end(), [&](const Folder& f) { return f.id == id; }), p.folders.end());
    }
    a.selItems.clear();
    a.selLayers.clear();
    a.markChanged();
}

static void newCompFromFootage(App& a, Footage& f) {
    double dur = f.kind == FootageKind::Image ? a.settings.stillDuration : std::max(1.0, f.duration);
    int w = f.hasVideo ? f.w : 1920, h = f.hasVideo ? f.h : 1080;
    Composition* c = a.newComp(fileStem(f.name), std::max(16, w), std::max(16, h), f.hasVideo && f.kind != FootageKind::Image ? f.fps : 30, dur);
    a.addFootageLayer(*c, f, 0, 0);
}

static void itemContext(App& a, uint64_t id) {
    Project& p = *a.project;
    Footage* f = p.findFootage(id);
    Composition* c = p.findComp(id);
    if (ImGui::MenuItem("Rename")) {
        g_renameId = id;
        snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", f ? f->name.c_str() : (c ? c->name.c_str() : ""));
        for (auto& fo : p.folders) if (fo.id == id) snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", fo.name.c_str());
    }
    if (f) {
        if (ImGui::MenuItem("New Composition from Footage")) newCompFromFootage(a, *f);
        if (a.comp() && f->hasVideo | f->hasAudio)
            if (ImGui::MenuItem("Add to Active Composition")) a.addFootageLayer(*a.comp(), *f, a.comp()->time, 0);
        if (ImGui::MenuItem("Reveal in Explorer")) shellOpen(parentDir(f->path));
        if (ImGui::MenuItem("Replace Footage...")) {
            std::string np = dlgOpenFile(a.hwnd, L"All files\0*.*\0", "Replace Footage");
            if (!np.empty()) {
                MediaInfo mi;
                if (probeMedia(np, mi)) {
                    f->path = np;
                    f->name = fileName(np);
                    f->w = mi.w;
                    f->h = mi.h;
                    f->fps = mi.fps;
                    f->duration = mi.still ? a.settings.stillDuration : mi.duration;
                    f->hasVideo = mi.hasVideo;
                    f->hasAudio = mi.hasAudio;
                    f->kind = mi.still ? FootageKind::Image : (mi.hasVideo ? FootageKind::Video : FootageKind::Audio);
                    f->missing = false;
                    f->media = f->hasVideo ? MediaSource::open(*f) : nullptr;
                    f->audio = f->hasAudio ? AudioData::decodeAsync(np) : nullptr;
                    a.renderer.clearCaches();
                    a.markChanged();
                }
            }
        }
        if (f->kind == FootageKind::Sequence && ImGui::BeginMenu("Interpret Frame Rate")) {
            for (double fps : {12.0, 15.0, 23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 60.0})
                if (ImGui::MenuItem(strf("%g fps", fps).c_str(), nullptr, fabs(f->fps - fps) < 0.001)) {
                    f->fps = fps;
                    f->duration = f->seqCount / fps;
                    f->media = MediaSource::open(*f);
                    a.renderer.clearCaches();
                    a.markChanged();
                }
            ImGui::EndMenu();
        }
    }
    if (c) {
        if (ImGui::MenuItem("Open in Timeline")) a.openComp(id);
        if (ImGui::MenuItem("Composition Settings...")) { a.openComp(id); a.dlgCompSettings = true; }
        if (ImGui::MenuItem("Duplicate")) {
            auto d = std::make_shared<Composition>();
            d->load(c->save());
            d->id = newId();
            d->name = c->name + " copy";
            for (auto& l : d->layers) { l->id = newId(); for (auto& e : l->effects) e.id = newId(); }
            p.comps.push_back(d);
            a.markChanged();
        }
        if (a.comp() && a.comp()->id != id && ImGui::MenuItem("Add to Active Composition")) a.addCompLayer(*a.comp(), *c, a.comp()->time, 0);
        if (ImGui::MenuItem("Add to Render Queue")) a.queueRender(id);
    }
    if (!p.folders.empty() && (f || c) && ImGui::BeginMenu("Move to Folder")) {
        if (ImGui::MenuItem("(Root)")) { if (f) f->folder = 0; if (c) c->folder = 0; a.markChanged(); }
        for (auto& fo : p.folders)
            if (ImGui::MenuItem(fo.name.c_str())) { if (f) f->folder = fo.id; if (c) c->folder = fo.id; a.markChanged(); }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete")) { if (!a.selItems.count(id)) a.selItems = {id}; deleteItems(a); }
}

static void itemRow(App& a, uint64_t id, Icon ic, const std::string& name, const std::string& type, const std::string& size,
                    const std::string& fps, const std::string& dur, int depth, bool missing) {
    ImGui::TableNextRow(0, S(22));
    ImGui::TableNextColumn();
    ImGui::PushID((int)(id & 0x7fffffff));
    bool sel = a.selItems.count(id) > 0;
    ImVec2 rp = ImGui::GetCursorScreenPos();
    if (g_renameId == id) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * S(14) + S(22));
        ImGui::SetNextItemWidth(-1);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rn", g_renameBuf, sizeof(g_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (Footage* f = a.project->findFootage(id)) f->name = g_renameBuf;
            if (Composition* c = a.project->findComp(id)) {
                c->name = g_renameBuf;
                for (auto& oc : a.project->comps)
                    for (auto& l : oc->layers) if (l->type == LayerType::Precomp && l->source == id) l->name = g_renameBuf;
            }
            for (auto& fo : a.project->folders) if (fo.id == id) fo.name = g_renameBuf;
            g_renameId = 0;
            a.markChanged();
        } else if (ImGui::IsItemDeactivated()) g_renameId = 0;
        ImGui::PopID();
        return;
    }
    if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, S(18)))) {
        if (ImGui::GetIO().KeyCtrl) { if (sel) a.selItems.erase(id); else a.selItems.insert(id); }
        else a.selItems = {id};
        if (ImGui::IsMouseDoubleClicked(0)) {
            if (a.project->findComp(id)) a.openComp(id);
            else if (Footage* f = a.project->findFootage(id)) {
                if (a.comp()) a.addFootageLayer(*a.comp(), *f, a.comp()->time, 0);
                else newCompFromFootage(a, *f);
            }
        }
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(DND_ITEM, &id, sizeof(id));
        ImGui::Text("%s", name.c_str());
        ImGui::EndDragDropSource();
    }
    bool isFolder = false;
    for (auto& fo : a.project->folders) if (fo.id == id) isFolder = true;
    if (isFolder && ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
            uint64_t src = *(const uint64_t*)pl->Data;
            if (Footage* f = a.project->findFootage(src)) f->folder = id;
            if (Composition* c = a.project->findComp(src)) c->folder = id;
            a.markChanged();
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (!a.selItems.count(id)) a.selItems = {id};
        itemContext(a, id);
        ImGui::EndPopup();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = rp.x + depth * S(14);
    float h = S(18);
    drawIcon(dl, ic, ImVec2(x + S(9), rp.y + h * 0.5f + S(1)), S(13), sel ? pal::Accent : pal::TextDim);
    dl->AddText(ImVec2(x + S(22), rp.y + (h - ImGui::GetTextLineHeight()) * 0.5f + S(1)), missing ? IM_COL32(0xC8, 0x50, 0x50, 0xFF) : pal::Text, name.c_str());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", type.c_str());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", size.c_str());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", fps.c_str());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", dur.c_str());
    ImGui::PopID();
}

static void listFolder(App& a, uint64_t folder, int depth) {
    Project& p = *a.project;
    for (auto& fo : p.folders) {
        if (fo.parent != folder) continue;
        itemRow(a, fo.id, Icon::Folder, fo.name, "Folder", "", "", "", depth, false);
        // folder open toggle via small arrow overlay
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) fo.open = !fo.open;
        if (fo.open) listFolder(a, fo.id, depth + 1);
    }
    for (auto& c : p.comps) {
        if (c->folder != folder || !matches(c->name)) continue;
        itemRow(a, c->id, Icon::Comp, c->name, "Composition", strf("%d×%d", c->w, c->h), strf("%g", c->fps),
                fmtTimecode(c->duration, c->fps), depth, false);
    }
    for (auto& f : p.footage) {
        if (f->folder != folder || !matches(f->name)) continue;
        const char* type = f->kind == FootageKind::Video ? "Video" : f->kind == FootageKind::Image ? "Image" : f->kind == FootageKind::Sequence ? "Sequence" : "Audio";
        std::string size = f->hasVideo ? strf("%d×%d", f->w, f->h) : (f->sampleRate ? strf("%d Hz", f->sampleRate) : "");
        std::string fps = f->hasVideo && f->kind != FootageKind::Image ? strf("%g", std::round(f->fps * 1000) / 1000) : "";
        std::string dur = f->kind == FootageKind::Image ? "" : fmtTimecode(f->duration, f->fps > 0 ? f->fps : 30);
        itemRow(a, f->id, footageIcon(*f), f->name, type, size, fps, dur, depth, f->missing);
    }
}

// ---------------------------------------------------------------------------
// media grid: thumbnails, folders you open with a double-click, breadcrumb back
// ---------------------------------------------------------------------------
static uint64_t g_curFolder = 0;

static void mediaTile(App& a, uint64_t id, const std::string& name, TexPtr thumb, Icon ic, const std::string& badge, bool isFolder, float tw) {
    ImGui::PushID((int)(id & 0x7fffffff));
    float th = tw * 9.f / 16.f, lh = ImGui::GetTextLineHeight() + S(4);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool sel = a.selItems.count(id) > 0;
    ImGui::InvisibleButton("tile", ImVec2(tw, th + lh));
    bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(0)) {
        if (ImGui::GetIO().KeyCtrl) { if (sel) a.selItems.erase(id); else a.selItems.insert(id); }
        else a.selItems = {id};
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) {
        if (isFolder) g_curFolder = id;
        else if (a.project->findComp(id)) a.openComp(id);
        else if (Footage* f = a.project->findFootage(id)) {
            if (a.comp()) a.addFootageLayer(*a.comp(), *f, a.comp()->time, 0);
            else newCompFromFootage(a, *f);
        }
    }
    if (!isFolder && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(DND_ITEM, &id, sizeof(id));
        if (thumb) ImGui::Image(thumb->id(), ImVec2(S(96), S(96) * thumb->h / std::max(1, thumb->w)));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (isFolder && ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
            uint64_t src = *(const uint64_t*)pl->Data;
            if (Footage* f = a.project->findFootage(src)) f->folder = id;
            if (Composition* c = a.project->findComp(src)) c->folder = id;
            a.markChanged();
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (!a.selItems.count(id)) a.selItems = {id};
        itemContext(a, id);
        ImGui::EndPopup();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1 = p0 + ImVec2(tw, th);
    dl->AddRectFilled(p0, p1, isFolder ? pal::Bg3 : pal::Bg0, S(5));
    if (thumb && thumb->w > 0 && thumb->h > 0) {
        // cover-fit, cropped by UVs so the tile keeps its rounded corners
        float ka = (float)thumb->w / thumb->h, kt = tw / th;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (ka > kt) { float u = kt / ka; uv0.x = (1 - u) * 0.5f; uv1.x = 1 - uv0.x; }
        else { float v = ka / kt; uv0.y = (1 - v) * 0.5f; uv1.y = 1 - uv0.y; }
        dl->AddImageRounded(thumb->id(), p0, p1, uv0, uv1, IM_COL32_WHITE, S(5));
    } else {
        drawIcon(dl, ic, (p0 + p1) * 0.5f, S(isFolder ? 30 : 24), isFolder ? pal::Accent : pal::TextFaint);
    }
    if (!badge.empty()) {
        ImVec2 bs = ImGui::CalcTextSize(badge.c_str());
        ImVec2 b1 = p1 - ImVec2(S(5), S(5)), b0 = b1 - bs - ImVec2(S(8), S(2));
        dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 0xB0), S(3));
        dl->AddText(b0 + ImVec2(S(4), S(1)), pal::White, badge.c_str());
    }
    if (sel) dl->AddRect(p0, p1, pal::Accent, S(5), 0, S(2));
    else if (hov) dl->AddRect(p0, p1, IM_COL32(0xFF, 0xFF, 0xFF, 0x50), S(5), 0, S(1));
    dl->PushClipRect(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y + lh), true);
    dl->AddText(ImVec2(p0.x + S(2), p1.y + S(2)), sel ? pal::Accent : pal::Text, name.c_str());
    dl->PopClipRect();
    if (hov) ImGui::SetTooltip("%s", name.c_str());
    ImGui::PopID();
}

static void mediaGrid(App& a, ImVec2 size) {
    Project& p = *a.project;
    if (g_curFolder && std::none_of(p.folders.begin(), p.folders.end(), [](const Folder& f) { return f.id == g_curFolder; })) g_curFolder = 0;
    // breadcrumb: Media > folder > subfolder
    std::vector<const Folder*> chain;
    for (uint64_t id = g_curFolder; id;) {
        const Folder* f = nullptr;
        for (auto& fo : p.folders) if (fo.id == id) f = &fo;
        if (!f) break;
        chain.insert(chain.begin(), f);
        id = f->parent;
    }
    if (ImGui::SmallButton(T("Media"))) g_curFolder = 0;
    for (auto* f : chain) {
        ImGui::SameLine(0, S(2));
        ImGui::TextDisabled(">");
        ImGui::SameLine(0, S(2));
        if (ImGui::SmallButton(strf("%s##bc%llu", f->name.c_str(), (unsigned long long)f->id).c_str())) g_curFolder = f->id;
    }
    ImGui::BeginChild("grid", ImVec2(size.x, size.y - ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
    float gap = S(8), avail = ImGui::GetContentRegionAvail().x;
    float tw = std::max(S(96), std::min(S(170), (avail - gap) / std::max(1.f, std::floor((avail + gap) / (S(124) + gap))) - gap));
    int cols = std::max(1, (int)((avail + gap) / (tw + gap)));
    int n = 0;
    bool searching = g_search[0] != 0;
    auto place = [&] { if (n++ % cols) ImGui::SameLine(0, gap); else if (n > 1) ImGui::Dummy(ImVec2(0, S(2))); };
    if (!searching)
        for (auto& fo : p.folders) {
            if (fo.parent != g_curFolder) continue;
            int cnt = 0;
            TexPtr cover;
            for (auto& f : p.footage) if (f->folder == fo.id) { cnt++; if (!cover && f->thumb) cover = f->thumb; }
            place();
            mediaTile(a, fo.id, fo.name, cover, Icon::Folder, strf("%d", cnt), true, tw);
        }
    for (auto& c : p.comps) {
        if ((searching ? !matches(c->name) : c->folder != g_curFolder)) continue;
        place();
        mediaTile(a, c->id, c->name, nullptr, Icon::Comp, fmtTimeShort(c->duration), false, tw);
    }
    for (auto& f : p.footage) {
        if ((searching ? !matches(f->name) : f->folder != g_curFolder)) continue;
        place();
        std::string badge = f->kind == FootageKind::Image ? "" : fmtTimeShort(f->duration);
        mediaTile(a, f->id, f->name, f->thumb, footageIcon(*f), badge, false, tw);
    }
    if (n == 0) {
        ImGui::Dummy(ImVec2(0, S(20)));
        ImGui::TextDisabled("%s", T(searching ? "Nothing found" : "Import media: drop files, folders or ZIP archives here, or press Ctrl+I"));
    }
    ImGui::EndChild();
}

void panelProject(App& a) {
    if (!ImGui::Begin((std::string(T("Media")) + "###Project").c_str(), &a.pProject)) { ImGui::End(); return; }
    if (!a.project) { ImGui::End(); return; }
    Project& p = *a.project;
    // preview of the selected item
    uint64_t sel = a.selItems.empty() ? 0 : *a.selItems.begin();
    Footage* sf = p.findFootage(sel);
    Composition* sc = p.findComp(sel);
    float thumbH = S(64), thumbW = S(112);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(tp, tp + ImVec2(thumbW, thumbH), pal::Bg0);
    if (sf && sf->thumb) {
        float k = std::min(thumbW / sf->thumb->w, thumbH / sf->thumb->h);
        ImVec2 d(sf->thumb->w * k, sf->thumb->h * k);
        ImVec2 o = tp + (ImVec2(thumbW, thumbH) - d) * 0.5f;
        dl->AddImage(sf->thumb->id(), o, o + d);
    } else if (sf || sc) {
        drawIcon(dl, sc ? Icon::Comp : footageIcon(*sf), tp + ImVec2(thumbW, thumbH) * 0.5f, S(26), pal::TextFaint);
    }
    ImGui::Dummy(ImVec2(thumbW, thumbH));
    ImGui::SameLine(0, S(10));
    ImGui::BeginGroup();
    if (sf) {
        ImGui::TextUnformatted(sf->name.c_str());
        if (sf->hasVideo) ImGui::TextDisabled("%d × %d  (1.00)", sf->w, sf->h);
        if (sf->kind != FootageKind::Image) ImGui::TextDisabled("Δ %s, %g fps", fmtTimecode(sf->duration, sf->fps).c_str(), std::round(sf->fps * 1000) / 1000);
        std::string codecs = sf->codec;
        if (sf->hasAudio) codecs += (codecs.empty() ? "" : " · ") + sf->audioCodec + strf(" %d Hz", sf->sampleRate);
        ImGui::TextDisabled("%s", codecs.c_str());
        if (sf->missing) ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1), "Missing: %s", sf->path.c_str());
    } else if (sc) {
        ImGui::TextUnformatted(sc->name.c_str());
        ImGui::TextDisabled("%d × %d  (1.00)", sc->w, sc->h);
        ImGui::TextDisabled("Δ %s, %g fps", fmtTimecode(sc->duration, sc->fps).c_str(), sc->fps);
        ImGui::TextDisabled("%d layers", (int)sc->layers.size());
    } else {
        ImGui::TextDisabled("%d items", (int)(p.footage.size() + p.comps.size()));
        ImGui::TextDisabled("Drop files here or press Ctrl+I");
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, S(2)));
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() * 2 - S(8));
    ImGui::InputTextWithHint("##search", T("Search project"), g_search, sizeof(g_search));
    ImGui::SameLine(0, S(4));
    if (iconButton("vgrid", Icon::Grid, a.settings.mediaGrid, "Thumbnails")) a.settings.mediaGrid = true;
    ImGui::SameLine(0, S(2));
    if (iconButton("vlist", Icon::Sequence, !a.settings.mediaGrid, "List with details")) a.settings.mediaGrid = false;

    float footerH = ImGui::GetFrameHeight() + S(6);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (a.settings.mediaGrid) {
        mediaGrid(a, ImVec2(avail.x, avail.y - footerH));
    } else if (ImGui::BeginTable("items", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable,
                          ImVec2(avail.x, avail.y - footerH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.46f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("FPS", ImGuiTableColumnFlags_WidthStretch, 0.08f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableHeadersRow();
        listFolder(a, 0, 0);
        ImGui::EndTable();
    }
    // background drop target for files dragged out of folders
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
            uint64_t src = *(const uint64_t*)pl->Data;
            if (Footage* f = p.findFootage(src)) f->folder = 0;
            if (Composition* c = p.findComp(src)) c->folder = 0;
            a.markChanged();
        }
        ImGui::EndDragDropTarget();
    }
    // footer buttons
    if (iconButton("imp", Icon::Plus, false, "Import (Ctrl+I)")) a.importDialog();
    ImGui::SameLine(0, S(2));
    if (iconButton("impdir", Icon::Folder, false, "Import a folder or a ZIP archive (right-click for ZIP)")) a.importFolderDialog();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) a.importZipDialog();
    ImGui::SameLine(0, S(8));
    if (iconButton("fold", Icon::Plus, false, "New Folder")) {
        Folder f;
        f.id = newId();
        f.parent = a.settings.mediaGrid ? g_curFolder : 0;
        f.name = "New Folder";
        p.folders.push_back(f);
        g_renameId = f.id;
        snprintf(g_renameBuf, sizeof(g_renameBuf), "New Folder");
        a.markChanged();
    }
    ImGui::SameLine(0, S(2));
    if (iconButton("comp", Icon::Comp, false, "New Composition (drop footage here to match its settings)")) a.dlgNewComp = true;
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM))
            if (Footage* f = p.findFootage(*(const uint64_t*)pl->Data)) newCompFromFootage(a, *f);
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0, S(2));
    if (iconButton("del", Icon::Trash, false, "Delete selected items") && !a.selItems.empty()) deleteItems(a);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        !a.selItems.empty() && a.selLayers.empty())
        deleteItems(a);
    ImGui::End();
}
