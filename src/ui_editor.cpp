#include "ui.h"
#include "i18n.h"
#include "imgui_internal.h"
#include "gfx.h"

static const char* kWorkspaces[] = {"Standard", "Editing", "Effects", "Minimal"};

static void buildLayout(App& a, ImGuiID dock, ImVec2 size) {
    using namespace ImGui;
    DockBuilderRemoveNode(dock);
    DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    DockBuilderSetNodeSize(dock, size);
    ImGuiID top, bottom, left, center, right, rTop, rMid, rBot;
    a.pProject = a.pFxControls = a.pViewer = a.pTimeline = a.pEffects = true;
    a.pInfo = a.pPreview = a.pCharacter = a.pAudio = a.pRender = true;
    switch (a.workspace) {
    default:
    case 0:
        DockBuilderSplitNode(dock, ImGuiDir_Down, 0.38f, &bottom, &top);
        DockBuilderSplitNode(top, ImGuiDir_Left, 0.21f, &left, &center);
        DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, &right, &center);
        DockBuilderSplitNode(right, ImGuiDir_Up, 0.25f, &rTop, &rBot);
        DockBuilderSplitNode(rBot, ImGuiDir_Up, 0.22f, &rMid, &rBot);
        DockBuilderDockWindow("###Project", left);
        DockBuilderDockWindow("###Effect Controls", left);
        DockBuilderDockWindow("###Composition", center);
        DockBuilderDockWindow("Info", rTop);
        DockBuilderDockWindow("Audio", rTop);
        DockBuilderDockWindow("Preview", rMid);
        DockBuilderDockWindow("Effects & Presets", rBot);
        DockBuilderDockWindow("Character", rBot);
        DockBuilderDockWindow("###Timeline", bottom);
        DockBuilderDockWindow("Render Queue", bottom);
        break;
    case 1:  // edit page
        DockBuilderSplitNode(dock, ImGuiDir_Down, 0.44f, &bottom, &top);
        DockBuilderSplitNode(top, ImGuiDir_Left, 0.22f, &left, &center);
        DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, &right, &center);
        DockBuilderSplitNode(right, ImGuiDir_Down, 0.22f, &rBot, &rTop);
        DockBuilderDockWindow("###Project", left);
        DockBuilderDockWindow("Effects & Presets", left);
        DockBuilderDockWindow("###Composition", center);
        DockBuilderDockWindow("###Effect Controls", rTop);
        DockBuilderDockWindow("Character", rTop);
        DockBuilderDockWindow("Info", rTop);
        DockBuilderDockWindow("Preview", rBot);
        DockBuilderDockWindow("Audio", rBot);
        DockBuilderDockWindow("###Timeline", bottom);
        DockBuilderDockWindow("Render Queue", bottom);
        break;
    case 2:  // effects focused
        DockBuilderSplitNode(dock, ImGuiDir_Down, 0.34f, &bottom, &top);
        DockBuilderSplitNode(top, ImGuiDir_Left, 0.2f, &left, &center);
        DockBuilderSplitNode(center, ImGuiDir_Right, 0.32f, &right, &center);
        DockBuilderDockWindow("Effects & Presets", left);
        DockBuilderDockWindow("###Project", left);
        DockBuilderDockWindow("###Composition", center);
        DockBuilderDockWindow("###Effect Controls", right);
        DockBuilderDockWindow("Character", right);
        DockBuilderDockWindow("Info", right);
        DockBuilderDockWindow("###Timeline", bottom);
        DockBuilderDockWindow("Preview", bottom);
        DockBuilderDockWindow("Audio", bottom);
        DockBuilderDockWindow("Render Queue", bottom);
        break;
    case 3:
        DockBuilderSplitNode(dock, ImGuiDir_Down, 0.36f, &bottom, &top);
        DockBuilderDockWindow("###Composition", top);
        DockBuilderDockWindow("###Timeline", bottom);
        a.pProject = a.pFxControls = a.pEffects = a.pInfo = a.pPreview = a.pCharacter = a.pAudio = a.pRender = false;
        break;
    }
    DockBuilderFinish(dock);
}

static void effectMenu(App& a) {
    for (auto& cat : a.effects.categories) {
        if (ImGui::BeginMenu(cat.c_str())) {
            for (auto* d : a.effects.inCategory(cat)) {
                if (!d->ok) continue;
                if (ImGui::MenuItem(d->name.c_str())) a.applyEffect(d);
            }
            ImGui::EndMenu();
        }
    }
}

static void menuBar(App& a) {
    Composition* c = a.comp();
    Layer* sl = a.selectedLayer();
    bool hasP = a.project != nullptr;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(6)));
    if (!ImGui::BeginMainMenuBar()) { ImGui::PopStyleVar(); return; }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu("New")) {
            if (ImGui::MenuItem("New Project", "Ctrl+Alt+N")) a.requestAction(1);
            if (ImGui::MenuItem("New Composition...", "Ctrl+N", false, hasP)) a.dlgNewComp = true;
            if (ImGui::MenuItem("New Folder", nullptr, false, hasP)) {
                Folder f;
                f.id = newId();
                f.name = "New Folder";
                a.project->folders.push_back(f);
                a.markChanged();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open Project...", "Ctrl+O")) a.requestAction(2);
        if (ImGui::BeginMenu("Open Recent", !a.settings.recent.empty())) {
            for (auto& r : a.settings.recent)
                if (ImGui::MenuItem(r.c_str())) { a.pendingOpenPath = r; a.requestAction(2); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Close Project", nullptr, false, hasP)) a.requestAction(4);
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasP)) a.saveProject(false);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, hasP)) a.saveProject(true);
        ImGui::Separator();
        if (ImGui::BeginMenu("Import", hasP)) {
            if (ImGui::MenuItem("File...", "Ctrl+I")) a.importDialog();
            if (ImGui::MenuItem("Folder...", "Ctrl+Shift+I")) a.importFolderDialog();
            if (ImGui::MenuItem("ZIP Archive...")) a.importZipDialog();
            if (ImGui::MenuItem("Image Sequence...")) {
                std::string f = dlgOpenFile(a.hwnd, L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp\0", "Import Image Sequence (pick the first frame)");
                if (!f.empty()) {
                    bool keep = a.settings.importSequences;
                    a.settings.importSequences = true;
                    a.importFiles({f});
                    a.settings.importSequences = keep;
                }
            }
            if (ImGui::MenuItem("Plugin Effect (.gsfx)...")) {
                std::string f = dlgOpenFile(a.hwnd, L"GeSe Effect (*.gsfx)\0*.gsfx\0", "Install Plugin");
                if (!f.empty()) a.importFile(f);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Export", hasP && c)) {
            if (ImGui::MenuItem("Add to Render Queue", "Ctrl+M")) a.queueRender(c->id);
            if (ImGui::MenuItem("Save Frame As PNG...", "Ctrl+Alt+S")) {
                std::string p = dlgSaveFile(a.hwnd, L"PNG image\0*.png\0", "png", c->name + "_frame", "Save Frame");
                if (!p.empty()) {
                    RenderParams rp;
                    rp.sync = true;
                    TexPtr img = a.renderer.renderComp(*a.project, *c, c->time, rp);
                    TexPtr o = img ? a.renderer.finalizeRGBA8(*img, c->bg, true) : nullptr;
                    std::vector<uint8_t> px;
                    if (o && gfx::readback(*o, px) && gfx::savePNG(p, px.data(), o->w, o->h)) a.notify("Frame saved: " + fileName(p));
                    else a.notify("Could not save frame", true);
                }
            }
            if (ImGui::MenuItem("Audio Only (WAV / MP3 / M4A)...")) a.saveCompAudio(*c);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Home Screen")) a.requestAction(4);
        if (ImGui::MenuItem("Exit", "Ctrl+Q")) a.requestAction(3);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !a.undoStack.empty())) a.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !a.redoStack.empty())) a.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, sl != nullptr)) { a.copyLayers(); a.deleteSelectedLayers(false); }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, sl != nullptr)) a.copyLayers();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, c && a.clipLayers.size() > 0)) a.pasteLayers();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, sl != nullptr)) a.duplicateSelectedLayers();
        if (ImGui::MenuItem("Split Layer", "Ctrl+Shift+D", false, c != nullptr)) a.splitSelectedLayers(c->time);
        if (ImGui::MenuItem("Delete", "Delete", false, sl != nullptr)) a.deleteSelectedLayers(false);
        if (ImGui::MenuItem("Ripple Delete", "Shift+Delete", false, sl != nullptr)) a.deleteSelectedLayers(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A", false, c != nullptr)) for (auto& l : c->layers) a.selLayers.insert(l->id);
        if (ImGui::MenuItem("Deselect All", "F2")) { a.selLayers.clear(); a.clearKeySelection(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Purge Image Caches")) { a.renderer.clearCaches(); gfx::trimPool(0); a.notify("Caches purged"); }
        if (ImGui::MenuItem("Preferences...", "Ctrl+,")) a.dlgPrefs = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Composition", hasP)) {
        if (ImGui::MenuItem("New Composition...", "Ctrl+N")) a.dlgNewComp = true;
        if (ImGui::MenuItem("Composition Settings...", "Ctrl+K", false, c != nullptr)) a.dlgCompSettings = true;
        if (ImGui::MenuItem("Background...", nullptr, false, c != nullptr)) { a.selLayers.clear(); a.pFxControls = true; a.focusPanel = "###Effect Controls"; }
        ImGui::Separator();
        if (ImGui::MenuItem("Trim Comp to Work Area", nullptr, false, c != nullptr)) {
            double s = c->workStart, e = c->workEnd;
            for (auto& l : c->layers) { l->startTime -= s; l->inPoint -= s; l->outPoint -= s; }
            c->duration = e - s;
            c->workStart = 0;
            c->workEnd = c->duration;
            c->time = clampv(c->time - s, 0.0, c->duration);
            a.markChanged();
        }
        if (ImGui::MenuItem("Motion Blur (master switch)", nullptr, c && c->motionBlur, c != nullptr)) { c->motionBlur = !c->motionBlur; a.markChanged(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Add to Render Queue", "Ctrl+M", false, c != nullptr)) a.queueRender(c->id);
        if (ImGui::MenuItem("Play / Stop", "Space", false, c != nullptr)) a.togglePlay();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layer", hasP && c)) {
        if (ImGui::BeginMenu("New")) {
            if (ImGui::MenuItem("Text", "Ctrl+Alt+Shift+T")) a.addText(*c, "Text", ImVec2(-1, -1));
            if (ImGui::MenuItem("Solid...", "Ctrl+Y")) a.dlgSolid = true;
            if (ImGui::MenuItem("Effect Layer (Adjustment)", "Ctrl+Alt+Y")) a.addEffectLayer(*c, nullptr, c->time, 0);
            if (ImGui::MenuItem("Null Object", "Ctrl+Alt+Shift+Y")) a.addNull(*c);
            ImGui::Separator();
            if (ImGui::MenuItem("Camera", "Ctrl+Alt+Shift+K")) a.addCamera(*c);
            if (ImGui::BeginMenu("Light")) {
                for (int k : {LK_Point, LK_Spot, LK_Parallel, LK_Ambient})
                    if (ImGui::MenuItem(kLightKindNames[k])) a.addLight(*c, k);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove Background (AI)...", nullptr, false, sl && sl->type == LayerType::Footage)) a.dlgAi = true;
        if (ImGui::BeginMenu("Mask", sl != nullptr)) {
            auto addMask = [&](int shape) {
                Mask m;
                m.id = newId();
                m.shape = shape;
                m.name = strf("Mask %d", (int)sl->masks.size() + 1);
                ImVec2 ls = a.project->layerSize(*sl, *c);
                m.center = Prop(2, ls.x * 0.5f, ls.y * 0.5f);
                m.size = Prop(2, ls.x * 0.6f, ls.y * 0.6f);
                sl->masks.push_back(m);
                sl->maskOpen = true;
                a.markChanged();
            };
            if (ImGui::MenuItem("New Rectangle Mask")) addMask(0);
            if (ImGui::MenuItem("New Ellipse Mask")) addMask(1);
            if (ImGui::MenuItem("Remove All Masks", nullptr, false, !sl->masks.empty())) { sl->masks.clear(); a.markChanged(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Transform", sl != nullptr)) {
            if (ImGui::MenuItem("Reset")) {
                ImVec2 ls = a.project->layerSize(*sl, *c);
                sl->anchor = Prop(2, ls.x * 0.5f, ls.y * 0.5f);
                sl->position = Prop(2, c->w * 0.5f, c->h * 0.5f);
                sl->scale = Prop(2, 100, 100);
                sl->rotation = Prop(1, 0);
                sl->opacity = Prop(1, 100);
                a.markChanged();
            }
            if (ImGui::MenuItem("Fit to Comp", "Ctrl+Alt+F")) a.fitLayer(*sl, false);
            if (ImGui::MenuItem("Fill Comp")) a.fitLayer(*sl, true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Time", sl != nullptr)) {
            if (ImGui::MenuItem("Time-Reverse Layer", "Ctrl+Alt+R", sl && sl->reversed)) { for (auto* l : a.selectedLayers()) l->reversed = !l->reversed; a.markChanged(); }
            if (ImGui::MenuItem("Speed / Duration...", "Ctrl+R")) a.dlgSpeed = true;
            if (ImGui::MenuItem("Freeze Frame", "Shift+F")) a.freezeFrame(c->time);
            ImGui::EndMenu();
        }
        Footage* slf = sl && sl->type == LayerType::Footage ? a.project->findFootage(sl->source) : nullptr;
        bool slAudio = sl && (sl->type == LayerType::Precomp || (slf && slf->hasAudio));
        if (ImGui::BeginMenu("Audio", slAudio)) {
            if (ImGui::MenuItem("Mute Clip Audio", nullptr, !sl->audioOn)) {
                bool on = !sl->audioOn;
                for (auto* l : a.selectedLayers()) l->audioOn = on;
                a.markChanged();
            }
            if (ImGui::MenuItem("Extract Audio to New Layer", nullptr, false, slf && slf->hasVideo)) a.extractAudio(*sl);
            if (ImGui::MenuItem("Save Clip Audio As...", nullptr, false, slf != nullptr)) a.saveLayerAudio(*sl);
            ImGui::Separator();
            if (ImGui::MenuItem("Voice & Speed...")) { a.pFxControls = true; sl->audioOpen = true; a.focusPanel = "###Effect Controls"; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Blending Mode", sl != nullptr)) {
            for (int i = 0; i < BM_Count; i++)
                if (ImGui::MenuItem(kBlendNames[i], nullptr, sl->blend == i)) { for (auto* l : a.selectedLayers()) l->blend = i; a.markChanged(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Track Matte", sl != nullptr)) {
            for (int i = 0; i < 5; i++)
                if (ImGui::MenuItem(kMatteNames[i], nullptr, sl->matte == i)) { sl->matte = i; a.markChanged(); }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Pre-compose / Compound Clip...", "Ctrl+Shift+C", false, sl != nullptr)) a.dlgPrecompose = true;
        if (ImGui::MenuItem("Unpack Compound Clip", "Ctrl+Alt+Shift+C", false, sl && sl->type == LayerType::Precomp)) a.unpackPrecomp(*sl);
        if (ImGui::MenuItem("3D Layer", "Ctrl+Alt+3", sl && sl->threeD, sl && sl->type != LayerType::Camera && sl->type != LayerType::Light && sl->type != LayerType::Adjustment)) {
            bool on = !sl->threeD;
            for (auto* l : a.selectedLayers()) if (l->type != LayerType::Camera && l->type != LayerType::Light && l->type != LayerType::Adjustment) l->threeD = on;
            a.markChanged();
        }
        if (ImGui::BeginMenu("Arrange", sl != nullptr)) {
            if (ImGui::MenuItem("Bring Layer to Front", "Ctrl+Shift+]")) a.moveSelectedLayers(-1, true);
            if (ImGui::MenuItem("Bring Layer Forward", "Ctrl+]")) a.moveSelectedLayers(-1, false);
            if (ImGui::MenuItem("Send Layer Backward", "Ctrl+[")) a.moveSelectedLayers(1, false);
            if (ImGui::MenuItem("Send Layer to Back", "Ctrl+Shift+[")) a.moveSelectedLayers(1, true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Switches", sl != nullptr)) {
            if (ImGui::MenuItem("Hide Layer", nullptr, !sl->visible)) { sl->visible = !sl->visible; a.markChanged(); }
            if (ImGui::MenuItem("Solo", nullptr, sl->solo)) { sl->solo = !sl->solo; a.markChanged(); }
            if (ImGui::MenuItem("Lock", "Ctrl+L", sl->locked)) { sl->locked = !sl->locked; a.markChanged(); }
            if (ImGui::MenuItem("Motion Blur", nullptr, sl->motionBlur)) { sl->motionBlur = !sl->motionBlur; a.markChanged(); }
            if (ImGui::MenuItem("Effects On", nullptr, sl->fxOn)) { sl->fxOn = !sl->fxOn; a.markChanged(); }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effect", hasP && c)) {
        if (ImGui::MenuItem("Effect Controls", "F3")) { a.pFxControls = true; a.focusPanel = "###Effect Controls"; }
        const EffectDef* last = a.effects.find(a.lastEffectId);
        if (ImGui::MenuItem(last ? ("Apply " + last->name).c_str() : "Apply Last Effect", "Ctrl+Alt+Shift+E", false, last != nullptr)) a.applyEffect(last);
        if (ImGui::MenuItem("Remove All Effects", "Ctrl+Shift+E", false, sl != nullptr)) { for (auto* l : a.selectedLayers()) l->effects.clear(); a.markChanged(); }
        ImGui::Separator();
        effectMenu(a);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Animation", hasP && c)) {
        if (ImGui::MenuItem("Save Animation Preset...", nullptr, false, sl && !sl->effects.empty())) a.dlgSavePreset = true;
        if (ImGui::BeginMenu("Apply Animation Preset")) {
            for (auto& p : a.presetFiles())
                if (ImGui::MenuItem(fileStem(p).c_str())) a.applyPreset(p);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool ks = a.anyKeySelected();
        if (ImGui::BeginMenu("Keyframe Interpolation", ks)) {
            if (ImGui::MenuItem("Linear")) a.setSelectedKeysInterp(0);
            if (ImGui::MenuItem("Easy Ease", "F9")) a.setSelectedKeysInterp(1);
            if (ImGui::MenuItem("Easy Ease In", "Shift+F9")) a.setSelectedKeysInterp(2);
            if (ImGui::MenuItem("Easy Ease Out", "Ctrl+Shift+F9")) a.setSelectedKeysInterp(3);
            if (ImGui::MenuItem("Toggle Hold Keyframe", "Ctrl+Alt+H")) a.setSelectedKeysInterp(4);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Graph Editor", "Shift+F3", a.pGraph)) a.pGraph = !a.pGraph;
        if (ImGui::MenuItem("Delete Selected Keyframes", "Delete", false, ks)) a.deleteSelectedKeys();
        if (ImGui::MenuItem("Deselect Keyframes", nullptr, false, ks)) a.clearKeySelection();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Zoom In", ".")) a.viewZoom = std::min(16.f, (a.viewZoom > 0 ? a.viewZoom : 0.5f) * 1.25f);
        if (ImGui::MenuItem("Zoom Out", ",")) a.viewZoom = std::max(0.02f, (a.viewZoom > 0 ? a.viewZoom : 0.5f) / 1.25f);
        if (ImGui::MenuItem("Fit in Viewer", "Shift+/")) { a.viewZoom = 0; a.viewPan = ImVec2(0, 0); }
        if (ImGui::MenuItem("Full Screen Preview", "F11", a.fullPreview)) a.setFullPreview(!a.fullPreview);
        if (ImGui::MenuItem("Actual Size (100%)", "Alt+/")) { a.viewZoom = 1.f / a.dpi; a.viewPan = ImVec2(0, 0); }
        ImGui::Separator();
        static const char* res[] = {"Auto", "Full", "Half", "Third", "Quarter"};
        if (ImGui::BeginMenu("Preview Resolution")) {
            for (int i = 0; i < 5; i++) if (ImGui::MenuItem(res[i], nullptr, a.settings.previewRes == i)) { a.settings.previewRes = i; a.needRender = true; }
            ImGui::EndMenu();
        }
        static const char* ch[] = {"RGB", "Red", "Green", "Blue", "Alpha"};
        if (ImGui::BeginMenu("Show Channel")) {
            for (int i = 0; i < 5; i++) if (ImGui::MenuItem(ch[i], nullptr, a.viewChannel == i)) { a.viewChannel = i; a.needRender = true; }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Transparency Grid", nullptr, a.showTransparency)) { a.showTransparency = !a.showTransparency; a.needRender = true; }
        if (ImGui::MenuItem("Grid", "Ctrl+'", a.showGrid)) a.showGrid = !a.showGrid;
        if (ImGui::MenuItem("Title/Action Safe", "'", a.showSafe)) a.showSafe = !a.showSafe;
        ImGui::Separator();
        if (ImGui::MenuItem("Timecode Display", nullptr, a.settings.timeDisplay == 0)) a.settings.timeDisplay = 0;
        if (ImGui::MenuItem("Frame Number Display", nullptr, a.settings.timeDisplay == 1)) a.settings.timeDisplay = 1;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::BeginMenu("Workspace")) {
            for (int i = 0; i < 4; i++) if (ImGui::MenuItem(kWorkspaces[i], nullptr, a.workspace == i)) { a.workspace = i; a.resetLayout = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Current Workspace")) a.resetLayout = true;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Media", "Ctrl+0", &a.pProject);
        ImGui::MenuItem("Composition", nullptr, &a.pViewer);
        ImGui::MenuItem("Timeline", nullptr, &a.pTimeline);
        ImGui::MenuItem("Effect Controls", "F3", &a.pFxControls);
        ImGui::MenuItem("Effects & Presets", "Ctrl+5", &a.pEffects);
        ImGui::MenuItem("Character", "Ctrl+6", &a.pCharacter);
        ImGui::MenuItem("Info", "Ctrl+2", &a.pInfo);
        ImGui::MenuItem("Preview", "Ctrl+3", &a.pPreview);
        ImGui::MenuItem("Audio", "Ctrl+4", &a.pAudio);
        ImGui::MenuItem("Render Queue", "Ctrl+Alt+0", &a.pRender);
        ImGui::MenuItem("Graph Editor", "Shift+F3", &a.pGraph);
        ImGui::Separator();
        if (ImGui::MenuItem("Plugin Manager...")) a.dlgPlugins = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1")) a.dlgShortcuts = true;
        if (ImGui::MenuItem("Plugin SDK (.gsfx format)")) a.dlgSdk = true;
        if (ImGui::MenuItem("Open Log Folder")) shellOpen(localDataDir());
        if (ImGui::MenuItem("Open User Plugins Folder")) shellOpen(a.effects.userPluginDir());
        ImGui::Separator();
        if (ImGui::MenuItem("About GeSe")) a.dlgAbout = true;
        ImGui::EndMenu();
    }
    // project name + dirty marker on the right
    std::string title = (a.project ? a.project->displayName() : std::string("")) + (a.dirty ? " *" : "");
    float tw = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - tw - S(16));
    ImGui::TextDisabled("%s", title.c_str());
    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar();
}

static void toolbar(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = ImGui::GetFrameHeight() + S(10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(5)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal::v4(pal::Bg2));
    if (ImGui::BeginViewportSideBar("##toolbar", vp, ImGuiDir_Up, h, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        if (iconButton("home", Icon::Home, false, "Home screen")) a.requestAction(4);
        ImGui::SameLine(0, S(14));
        struct ToolDef { Tool t; Icon i; const char* tip; };
        static const ToolDef tools[] = {{Tool::Select, Icon::Arrow, "Selection Tool (V)"},
                                  {Tool::Hand, Icon::Hand, "Hand Tool (H)"},
                                  {Tool::Zoom, Icon::Zoom, "Zoom Tool (Z) - Alt-click to zoom out"},
                                  {Tool::Rotate, Icon::Rotate, "Rotation Tool (W)"},
                                  {Tool::Anchor, Icon::Anchor, "Pan Behind / Anchor Point Tool (Y)"},
                                  {Tool::MaskRect, Icon::Rect, "Rectangle Mask Tool (Q)"},
                                  {Tool::MaskEllipse, Icon::Ellipse, "Ellipse Mask Tool (Q)"},
                                  {Tool::Text, Icon::TextTool, "Type Tool (Ctrl+T)"},
                                  {Tool::Razor, Icon::Razor, "Razor / Blade Tool (C) - click a layer bar to split"}};
        for (auto& t : tools) {
            ImGui::PushID((int)t.t);
            if (iconButton("tool", t.i, a.tool == t.t, t.tip)) a.tool = t.t;
            ImGui::PopID();
            ImGui::SameLine(0, S(2));
        }
        ImGui::SameLine(0, S(14));
        ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos() + ImVec2(0, S(3)),
                                            ImGui::GetCursorScreenPos() + ImVec2(0, ImGui::GetFrameHeight() - S(3)), pal::Bg5);
        ImGui::SameLine(0, S(14));
        iconToggle("snap", Icon::Snap, &a.settings.snapping, "Snapping");
        // workspaces (right aligned)
        float total = 0;
        for (auto* w : kWorkspaces) total += ImGui::CalcTextSize(w).x + S(22);
        ImGui::SameLine(ImGui::GetWindowWidth() - total - S(12));
        for (int i = 0; i < 4; i++) {
            ImGui::PushID(i);
            ImVec2 ts = ImGui::CalcTextSize(kWorkspaces[i]);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("ws", ImVec2(ts.x + S(16), ImGui::GetFrameHeight()));
            bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            bool act = a.workspace == i;
            dl->AddText(p + ImVec2(S(8), (ImGui::GetFrameHeight() - ts.y) * 0.5f), act ? pal::Accent : (hov ? pal::White : pal::TextDim), T(kWorkspaces[i]));
            if (act) dl->AddLine(p + ImVec2(S(8), ImGui::GetFrameHeight() - 1), p + ImVec2(S(8) + ts.x, ImGui::GetFrameHeight() - 1), pal::Accent, S(2));
            if (clicked) { if (act) a.resetLayout = true; a.workspace = i; a.resetLayout = true; }
            ImGui::PopID();
            ImGui::SameLine(0, S(6));
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

static void statusBar(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = ImGui::GetTextLineHeight() + S(8);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(4)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal::v4(pal::Bg1));
    if (ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, h, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        Composition* c = a.comp();
        if (c) ImGui::TextDisabled("%s  ·  %d×%d  ·  %g fps  ·  %d layers", c->name.c_str(), c->w, c->h, c->fps, (int)c->layers.size());
        else ImGui::TextDisabled("No composition open");
        ImGui::SameLine(0, S(24));
        if (a.exporter.active()) {
            ImGui::TextColored(pal::v4(pal::Accent), "Rendering %d%%", (int)(a.exporter.progress() * 100));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", a.exporter.status().c_str());
            ImGui::SameLine(0, S(24));
        }
        if (a.ai.running()) {
            ImGui::TextColored(pal::v4(pal::Accent), "AI background removal %d%%", (int)(a.ai.progress() * 100));
            ImGui::SameLine(0, S(24));
        }
        std::string right = strf("%s   GPU %.1f ms   %s", a.playing ? strf("%.1f fps", a.measuredFps).c_str() : "", a.lastRenderMs,
                                 fmtBytes((int64_t)gfx::poolBytes()).c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - S(14));
        ImGui::TextDisabled("%s", right.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// messages are built in English with names / numbers appended: translate the fixed part
static std::string toastText(const std::string& s) {
    const char* tr = T(s.c_str());
    if (tr != s.c_str() && s != tr) return tr;
    size_t c = s.find(": ");
    if (c != std::string::npos) {
        std::string head = s.substr(0, c);
        return std::string(T(head.c_str())) + s.substr(c);
    }
    return s;
}

// notifications: a compact card in the corner (icon, text, close, time line), like the app's own panels
void uiToasts(App& a) {
    double now = nowSec();
    a.toasts.erase(std::remove_if(a.toasts.begin(), a.toasts.end(), [&](const Toast& t) { return t.until < now; }), a.toasts.end());
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + vp->Size.y - S(36);
    const ImU32 kErr = IM_COL32(0xD9, 0x4A, 0x3D, 0xFF);
    int shown = 0;
    for (int i = (int)a.toasts.size() - 1; i >= 0 && shown < 4; i--, shown++) {
        Toast& t = a.toasts[i];
        std::string text = toastText(t.text);
        float wrap = S(360);
        ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
        float pad = S(12), icon = S(18);
        ImVec2 sz(std::max(S(240), ts.x + icon + pad * 3 + S(18)), std::max(icon, ts.y) + pad * 2);
        float life = (float)(now - t.born), left = (float)(t.until - now);
        float k = std::min(1.f, std::min(life / 0.16f, left / 0.25f));  // fade in / out
        ImVec2 p(vp->Pos.x + vp->Size.x - sz.x - S(16) + (1 - k) * S(24), y - sz.y);
        ImGui::SetNextWindowPos(p);
        ImGui::SetNextWindowSize(sz);
        ImGui::SetNextWindowBgAlpha(0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, k);
        ImGui::Begin(strf("##toast%llu", (unsigned long long)t.id).c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 alpha = (ImU32)(k * 255) << IM_COL32_A_SHIFT;
        auto A = [&](ImU32 col) { return (col & ~IM_COL32_A_MASK) | alpha; };
        dl->AddRectFilled(p, p + sz, A(IM_COL32(0x2A, 0x2A, 0x2A, 0xFF)), S(5));
        dl->AddRect(p, p + sz, A(IM_COL32(0x40, 0x40, 0x40, 0xFF)), S(5));
        // icon: info = outlined accent circle with "i", error = filled red circle with "!"
        ImVec2 ic = p + ImVec2(pad + icon * 0.5f, pad + icon * 0.5f);
        if (t.error) {
            dl->AddCircleFilled(ic, icon * 0.5f, A(kErr));
            dl->AddRectFilled(ic + ImVec2(-S(1), -S(5)), ic + ImVec2(S(1), S(1.5f)), A(pal::White));
            dl->AddCircleFilled(ic + ImVec2(0, S(4)), S(1.3f), A(pal::White));
        } else {
            dl->AddCircle(ic, icon * 0.5f - S(0.5f), A(pal::Accent), 0, S(1.6f));
            dl->AddCircleFilled(ic + ImVec2(0, -S(4)), S(1.3f), A(pal::Accent));
            dl->AddRectFilled(ic + ImVec2(-S(1), -S(1.5f)), ic + ImVec2(S(1), S(5)), A(pal::Accent));
        }
        dl->AddText(nullptr, 0, p + ImVec2(pad * 2 + icon, pad + std::max(0.f, (icon - ImGui::GetTextLineHeight()) * 0.5f)), A(pal::Text),
                    text.c_str(), nullptr, wrap);
        // close button
        ImGui::SetCursorScreenPos(p + ImVec2(sz.x - S(26), S(6)));
        bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        if (iconButton("x", Icon::Close, false, nullptr, S(20))) t.until = now;
        // time left (paused while the pointer is over the card)
        if (hovered) t.until = std::max(t.until, now + 1.5);
        float total = t.error ? 7.f : 4.f, u = clampv(left / total, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(p.x + S(5), p.y + sz.y - S(3)), ImVec2(p.x + S(5) + (sz.x - S(10)) * u, p.y + sz.y - S(1.5f)),
                          A(t.error ? kErr : pal::AccentA(200)), S(1));
        ImGui::End();
        ImGui::PopStyleVar(3);
        y -= sz.y + S(8);
    }
}

static bool chord(ImGuiKeyChord k) { return ImGui::IsKeyChordPressed(k); }

void handleShortcuts(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    Composition* c = a.comp();
    const int Ctrl = ImGuiMod_Ctrl, Shift = ImGuiMod_Shift, Alt = ImGuiMod_Alt;
    if (chord(Ctrl | ImGuiKey_Z)) a.undo();
    if (chord(Ctrl | Shift | ImGuiKey_Z)) a.redo();
    if (chord(Ctrl | ImGuiKey_S)) a.saveProject(false);
    if (chord(Ctrl | Shift | ImGuiKey_S)) a.saveProject(true);
    if (chord(Ctrl | ImGuiKey_O)) a.requestAction(2);
    if (chord(Ctrl | Alt | ImGuiKey_N)) a.requestAction(1);
    if (chord(Ctrl | ImGuiKey_Q)) a.requestAction(3);
    if (chord(Ctrl | ImGuiKey_I)) a.importDialog();
    if (chord(Ctrl | ImGuiKey_Comma)) a.dlgPrefs = true;
    if (chord(ImGuiKey_F1)) a.dlgShortcuts = true;
    if (!a.project) return;
    if (chord(Ctrl | ImGuiKey_N)) a.dlgNewComp = true;
    if (!c) return;
    if (chord(ImGuiKey_Space)) a.togglePlay();
    if (chord(Ctrl | ImGuiKey_K)) a.dlgCompSettings = true;
    if (chord(Ctrl | ImGuiKey_M)) a.queueRender(c->id);
    if (chord(ImGuiKey_Home)) a.setTime(0);
    if (chord(ImGuiKey_End)) a.setTime(c->duration);
    // playhead: ← → one frame (hold to repeat), Shift = 10 frames, ↑ ↓ jump to the previous / next clip edge or keyframe
    auto rep = [](ImGuiKeyChord k) { return ImGui::IsKeyChordPressed(k, ImGuiInputFlags_Repeat); };
    if (rep(ImGuiKey_RightArrow) || rep(ImGuiKey_PageDown) || rep(Ctrl | ImGuiKey_RightArrow)) a.stepFrames(1);
    if (rep(ImGuiKey_LeftArrow) || rep(ImGuiKey_PageUp) || rep(Ctrl | ImGuiKey_LeftArrow)) a.stepFrames(-1);
    if (rep(Shift | ImGuiKey_RightArrow) || rep(Shift | ImGuiKey_PageDown)) a.stepFrames(10);
    if (rep(Shift | ImGuiKey_LeftArrow) || rep(Shift | ImGuiKey_PageUp)) a.stepFrames(-10);
    if (rep(ImGuiKey_DownArrow)) a.jumpToEdit(1);
    if (rep(ImGuiKey_UpArrow)) a.jumpToEdit(-1);
    if (chord(ImGuiKey_F11)) a.setFullPreview(!a.fullPreview);
    if (chord(ImGuiKey_Escape) && a.fullPreview) a.setFullPreview(false);
    if (chord(Shift | ImGuiKey_F3)) a.pGraph = !a.pGraph;
    if (chord(Ctrl | ImGuiKey_F)) focusEffectSearch(a);
    if (chord(Ctrl | Alt | Shift | ImGuiKey_C) && a.selectedLayer() && a.selectedLayer()->type == LayerType::Precomp) a.pendingUnpack = a.selectedLayer()->id;
    if (chord(Ctrl | Alt | Shift | ImGuiKey_K)) a.addCamera(*c);
    if (chord(Ctrl | Alt | ImGuiKey_3)) {
        for (auto* l : a.selectedLayers())
            if (l->type != LayerType::Camera && l->type != LayerType::Light && l->type != LayerType::Adjustment) l->threeD = !l->threeD;
        a.markChanged();
    }
    if (chord(Ctrl | Shift | ImGuiKey_I)) a.importFolderDialog();
    if (chord(ImGuiKey_B)) { c->workStart = std::min(c->time, c->workEnd - 1.0 / c->fps); a.markChanged(); }
    if (chord(ImGuiKey_N)) { c->workEnd = std::max(c->time + 1.0 / c->fps, c->workStart + 1.0 / c->fps); a.markChanged(); }
    if (chord(ImGuiKey_V)) a.tool = Tool::Select;
    if (chord(ImGuiKey_H)) a.tool = Tool::Hand;
    if (chord(ImGuiKey_Z)) a.tool = Tool::Zoom;
    if (chord(ImGuiKey_W)) a.tool = Tool::Rotate;
    if (chord(ImGuiKey_Y)) a.tool = Tool::Anchor;
    if (chord(ImGuiKey_Q)) a.tool = a.tool == Tool::MaskRect ? Tool::MaskEllipse : Tool::MaskRect;
    if (chord(Ctrl | ImGuiKey_T)) a.tool = Tool::Text;
    if (chord(ImGuiKey_C)) a.tool = Tool::Razor;
    if (chord(ImGuiKey_F2)) { a.selLayers.clear(); a.clearKeySelection(); }
    if (chord(ImGuiKey_F3)) { a.pFxControls = true; a.focusPanel = "###Effect Controls"; }
    if (chord(ImGuiKey_F9)) a.setSelectedKeysInterp(1);
    if (chord(Shift | ImGuiKey_F9)) a.setSelectedKeysInterp(2);
    if (chord(Ctrl | Shift | ImGuiKey_F9)) a.setSelectedKeysInterp(3);
    if (chord(Ctrl | Alt | ImGuiKey_H)) a.setSelectedKeysInterp(4);
    if (chord(Ctrl | ImGuiKey_Y)) a.dlgSolid = true;
    if (chord(Ctrl | Alt | ImGuiKey_Y)) a.addEffectLayer(*c, nullptr, c->time, 0);
    if (chord(Ctrl | Alt | Shift | ImGuiKey_Y)) a.addNull(*c);
    if (chord(Ctrl | Alt | Shift | ImGuiKey_T)) a.addText(*c, "Text", ImVec2(-1, -1));
    if (chord(Ctrl | ImGuiKey_Period) || chord(ImGuiKey_Period)) a.viewZoom = std::min(16.f, (a.viewZoom > 0 ? a.viewZoom : 0.5f) * 1.25f);
    if (chord(ImGuiKey_Comma)) a.viewZoom = std::max(0.02f, (a.viewZoom > 0 ? a.viewZoom : 0.5f) / 1.25f);
    if (chord(Shift | ImGuiKey_Slash)) { a.viewZoom = 0; a.viewPan = ImVec2(0, 0); }
    if (chord(ImGuiKey_Apostrophe)) a.showSafe = !a.showSafe;
    if (chord(Ctrl | ImGuiKey_Apostrophe)) a.showGrid = !a.showGrid;
    if (chord(ImGuiKey_KeypadMultiply) || chord(Shift | ImGuiKey_8)) { c->markers.push_back({c->time, ""}); a.markChanged(); }
    bool hasSel = !a.selLayers.empty();
    if ((chord(ImGuiKey_Delete) || chord(ImGuiKey_Backspace)) && a.deleteHandledFrame != ImGui::GetFrameCount()) {
        if (a.anyKeySelected()) a.deleteSelectedKeys();
        else if (hasSel) a.deleteSelectedLayers(false);
    }
    if (chord(Shift | ImGuiKey_Delete)) a.deleteSelectedLayers(true);
    if (chord(Ctrl | ImGuiKey_C)) a.copyLayers();
    if (chord(Ctrl | ImGuiKey_X)) { a.copyLayers(); a.deleteSelectedLayers(false); }
    if (chord(Ctrl | ImGuiKey_V)) a.pasteLayers();
    if (chord(Ctrl | ImGuiKey_D)) a.duplicateSelectedLayers();
    if (chord(Ctrl | Shift | ImGuiKey_D)) a.splitSelectedLayers(c->time);
    if (chord(Shift | ImGuiKey_F)) a.freezeFrame(c->time);
    if (chord(Ctrl | ImGuiKey_A)) for (auto& l : c->layers) a.selLayers.insert(l->id);
    if (chord(Ctrl | Shift | ImGuiKey_C) && hasSel) a.dlgPrecompose = true;
    if (chord(Ctrl | ImGuiKey_RightBracket)) a.moveSelectedLayers(-1, false);
    if (chord(Ctrl | ImGuiKey_LeftBracket)) a.moveSelectedLayers(1, false);
    if (chord(Ctrl | Shift | ImGuiKey_RightBracket)) a.moveSelectedLayers(-1, true);
    if (chord(Ctrl | Shift | ImGuiKey_LeftBracket)) a.moveSelectedLayers(1, true);
    if (chord(Ctrl | Alt | ImGuiKey_R)) { for (auto* l : a.selectedLayers()) l->reversed = !l->reversed; a.markChanged(); }
    if (chord(Ctrl | ImGuiKey_R) && hasSel) a.dlgSpeed = true;
    if (chord(Ctrl | Alt | ImGuiKey_F)) for (auto* l : a.selectedLayers()) a.fitLayer(*l, false);
    if (chord(Ctrl | Shift | ImGuiKey_E)) { for (auto* l : a.selectedLayers()) l->effects.clear(); a.markChanged(); }
    if (chord(Ctrl | Alt | Shift | ImGuiKey_E)) a.applyEffect(a.effects.find(a.lastEffectId));
    if (chord(Ctrl | ImGuiKey_L)) { for (auto* l : a.selectedLayers()) l->locked = !l->locked; a.markChanged(); }
    // trimming: Alt+[ / Alt+] trim in/out, [ / ] move layer to start/end at playhead
    if (chord(Alt | ImGuiKey_LeftBracket)) { for (auto* l : a.selectedLayers()) if (c->time < l->outPoint) l->inPoint = c->time; a.markChanged(); }
    if (chord(Alt | ImGuiKey_RightBracket)) { for (auto* l : a.selectedLayers()) if (c->time > l->inPoint) l->outPoint = c->time + 1.0 / c->fps; a.markChanged(); }
    if (chord(ImGuiKey_LeftBracket)) { for (auto* l : a.selectedLayers()) { double d = c->time - l->inPoint; l->inPoint += d; l->outPoint += d; l->startTime += d; } a.markChanged(); }
    if (chord(ImGuiKey_RightBracket)) { for (auto* l : a.selectedLayers()) { double d = c->time + 1.0 / c->fps - l->outPoint; l->inPoint += d; l->outPoint += d; l->startTime += d; } a.markChanged(); }
    // reveal properties: P S R T A U
    auto reveal = [&](int which) {
        for (auto* l : a.selectedLayers()) { l->expanded = true; l->xfOpen = which >= 0; if (which == 9) l->fxOpen = true; }
    };
    if (chord(ImGuiKey_P) || chord(ImGuiKey_S) || chord(ImGuiKey_R) || chord(ImGuiKey_T) || chord(ImGuiKey_A)) reveal(0);
    if (chord(ImGuiKey_E)) reveal(9);
    if (chord(ImGuiKey_U)) for (auto* l : a.selectedLayers()) l->expanded = !l->expanded;
}

static void unsavedDialog(App& a) {
    if (a.dlgUnsaved) { ImGui::OpenPopup("Unsaved Changes"); a.dlgUnsaved = false; }
    ImGui::SetNextWindowSize(ImVec2(S(420), 0));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Save changes to \"%s\" before closing?", a.project ? a.project->displayName().c_str() : "project");
        ImGui::Dummy(ImVec2(0, S(10)));
        float bw = S(110);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 3 - S(26));
        if (accentButton("Save", ImVec2(bw, 0))) {
            ImGui::CloseCurrentPopup();
            if (a.saveProject(false)) a.doAction(a.unsavedAction);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(bw, 0))) { ImGui::CloseCurrentPopup(); a.dirty = false; a.doAction(a.unsavedAction); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0))) { ImGui::CloseCurrentPopup(); a.pendingOpenPath.clear(); }
        ImGui::EndPopup();
    }
}

void uiEditor(App& a) {
    if (a.fullPreview) {
        viewerFullscreen(a);
        handleShortcuts(a);
        uiToasts(a);
        return;
    }
    menuBar(a);
    toolbar(a);
    statusBar(a);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiID dock = ImHashStr("GeSeMainDock");
    if (a.resetLayout) {
        buildLayout(a, dock, vp->WorkSize);
        a.resetLayout = false;
        a.focusPanel = "###Timeline";
    }
    ImGui::DockSpaceOverViewport(dock, vp, ImGuiDockNodeFlags_None);

    if (a.pProject) panelProject(a);
    if (a.pFxControls) panelEffectControls(a);
    if (a.pEffects) panelEffects(a);
    if (a.pInfo) panelInfo(a);
    if (a.pAudio) panelAudio(a);
    if (a.pPreview) panelPreview(a);
    if (a.pCharacter) panelCharacter(a);
    if (a.pRender) panelRenderQueue(a);
    if (a.pTimeline) panelTimeline(a);
    if (a.pViewer) panelViewer(a);
    if (a.pGraph) panelGraph(a);
    if (a.pendingUnpack) {
        if (Composition* pc = a.comp())
            if (Layer* ul = pc->find(a.pendingUnpack)) a.unpackPrecomp(*ul);
        a.pendingUnpack = 0;
    }
    if (!a.focusPanel.empty()) { ImGui::SetWindowFocus(a.focusPanel.c_str()); a.focusPanel.clear(); }

    handleShortcuts(a);
    uiDialogs(a);
    unsavedDialog(a);
    uiToasts(a);
}
