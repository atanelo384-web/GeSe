#include "ui.h"
#include "i18n.h"
#include "gfx.h"

namespace {
enum class Drag { None, Move, Scale, Rotate, Anchor, Pan, Mask, Point, MaskMove };
struct ViewState {
    Drag drag = Drag::None;
    ImVec2 startMouse;
    int hx = 0, hy = 0;
    std::vector<std::pair<uint64_t, ImVec2>> startPos;
    ImVec2 startScale, startAnchor, startPosSingle;
    float startRot = 0;
    uint64_t layer = 0;
    int pointParam = -1;
    uint64_t effect = 0;
    uint64_t mask = 0;
    ImVec2 maskStartCenter;
    // magnet (smart guides) for moving layers in the viewer
    ImVec2 boxMin, boxMax;               // grabbed layer bounds at drag start (comp px)
    std::vector<float> guideX, guideY;   // frame edges/center + other layers' edges/centers
    float snapX = NAN, snapY = NAN;      // guides hit this frame (drawn while dragging)
} vs;

float previewScale(App& a, const Composition& c, float screenZoom) {
    switch (a.settings.previewRes) {
    case 1: return 1.f;
    case 2: return 0.5f;
    case 3: return 1.f / 3.f;
    case 4: return 0.25f;
    default: {
        // auto: render no larger than what is visible on screen
        float k = screenZoom;
        if (k >= 0.75f) return 1.f;
        if (k >= 0.4f) return 0.5f;
        if (k >= 0.28f) return 1.f / 3.f;
        return 0.25f;
    }
    }
}
float g_lastScreenZoom = 1.f;
}  // namespace

// full screen preview: the result only, over the whole monitor; the controls show while the mouse moves
void viewerFullscreen(App& a) {
    Composition* c = a.comp();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0xFF));
    ImGui::Begin("##fullpreview", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (c) {
        float k = std::min(vp->Size.x / c->w, vp->Size.y / c->h);
        if (g_lastScreenZoom != k) a.needRender = true;
        g_lastScreenZoom = k;  // screen pixels per comp pixel
        ImVec2 sz(c->w * k, c->h * k), o = vp->Pos + (vp->Size - sz) * 0.5f;
        if (a.viewTex) dl->AddImage(a.viewTex->id(), o, o + sz);
        ImGui::SetCursorScreenPos(vp->Pos);
        ImGui::InvisibleButton("##fpbg", vp->Size);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) a.setFullPreview(false);
        else if (ImGui::IsItemClicked(0)) a.togglePlay();
        static double lastMove = 0;
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || !a.playing) lastMove = nowSec();
        if (nowSec() - lastMove < 2.5) {
            // progress bar (click / drag to seek), timecode and hints
            float bh = S(64);
            ImVec2 b0(vp->Pos.x, vp->Pos.y + vp->Size.y - bh), b1 = vp->Pos + vp->Size;
            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 0xB0));
            float px0 = b0.x + S(24), px1 = b1.x - S(24), py = b0.y + S(16);
            float u = c->duration > 0 ? (float)(c->time / c->duration) : 0;
            dl->AddRectFilled(ImVec2(px0, py - S(2)), ImVec2(px1, py + S(2)), IM_COL32(0x60, 0x60, 0x60, 0xFF), S(2));
            dl->AddRectFilled(ImVec2(px0, py - S(2)), ImVec2(px0 + (px1 - px0) * u, py + S(2)), pal::Accent, S(2));
            dl->AddCircleFilled(ImVec2(px0 + (px1 - px0) * u, py), S(6), pal::Accent);
            ImGui::SetCursorScreenPos(ImVec2(px0, py - S(10)));
            ImGui::InvisibleButton("##fpseek", ImVec2(px1 - px0, S(20)));
            if (ImGui::IsItemActive()) a.setTime(clampv((io.MousePos.x - px0) / (px1 - px0), 0.f, 1.f) * c->duration);
            std::string tc = timeLabel(c->time, c->fps) + "  /  " + timeLabel(c->duration, c->fps);
            dl->AddText(ImVec2(px0, py + S(14)), pal::Text, tc.c_str());
            const char* hint = T("Space: play / pause   ← →: frame   Esc or double-click: exit");
            ImVec2 hs = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(px1 - hs.x, py + S(14)), pal::TextDim, hint);
            ImGui::SetCursorScreenPos(ImVec2(b1.x - S(44), vp->Pos.y + S(12)));
            if (iconButton("##fpx", Icon::Close, false, "Exit full screen (Esc)", S(32))) a.setFullPreview(false);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }
    }
    ImGui::End();
}

void renderViewerIfNeeded(App& a) {
    Composition* c = a.comp();
    if (!c || !a.project || (!a.pViewer && !a.fullPreview)) return;
    if (!a.needRender && !a.renderer.pending && !a.playing) return;
    double t0 = nowSec();
    a.needRender = false;
    a.renderer.pending = false;
    RenderParams rp;
    rp.scale = previewScale(a, *c, g_lastScreenZoom);
    // effect under the cursor in the Effects panel: shown on the selected layer (or the whole frame), never saved
    const EffectDef* hd = a.hoverFx.empty() ? nullptr : a.effects.find(a.hoverFx);
    Layer* fxTarget = nullptr;
    LayerPtr tmp;
    bool fxOnWas = true;
    if (hd && hd->ok) {
        Layer* sl = a.selectedLayer();
        if (sl && sl->visible && sl->activeAt(c->time) && sl->type != LayerType::Null) {
            fxTarget = sl;
        } else {
            tmp = std::make_shared<Layer>();
            tmp->id = newId();
            tmp->type = LayerType::Adjustment;
            tmp->inPoint = 0;
            tmp->outPoint = c->duration + 1;
            tmp->anchor = Prop(2, c->w * 0.5f, c->h * 0.5f);
            tmp->position = Prop(2, c->w * 0.5f, c->h * 0.5f);
            c->layers.insert(c->layers.begin(), tmp);
            fxTarget = tmp.get();
        }
        fxOnWas = fxTarget->fxOn;
        fxTarget->fxOn = true;
        fxTarget->effects.push_back(a.effects.instantiate(*hd));
    }
    a.compRT = a.renderer.renderComp(*a.project, *c, c->time, rp);
    if (fxTarget) {
        fxTarget->effects.pop_back();
        fxTarget->fxOn = fxOnWas;
        if (tmp) c->layers.erase(c->layers.begin());
    }
    if (!a.compRT) return;
    if (!a.viewTex || a.viewTex->w != a.compRT->w || a.viewTex->h != a.compRT->h)
        a.viewTex = gfx::createTexture(a.compRT->w, a.compRT->h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    a.renderer.finalize(*a.compRT, *a.viewTex, c->bg, a.showTransparency ? 1 : 0, a.viewChannel);
    if (a.playing) a.renderer.prefetch(*a.project, *c, c->time + 0.25, rp.scale);
    a.lastRenderMs = (float)((nowSec() - t0) * 1000.0);
}

static ImVec2 layerCorner(const Mat3& m, ImVec2 ls, int i) {
    ImVec2 p(i == 1 || i == 2 ? ls.x : 0, i >= 2 ? ls.y : 0);
    return m.apply(p);
}

// axis-aligned bounds of a layer in comp pixels
static bool layerBounds(App& a, Composition& c, Layer& l, double t, ImVec2& mn, ImVec2& mx) {
    ImVec2 ls = a.project->layerSize(l, c);
    if (ls.x <= 0 || ls.y <= 0) return false;
    Mat3 m = a.project->layerMatrix(c, l, t);
    mn = ImVec2(1e9f, 1e9f);
    mx = ImVec2(-1e9f, -1e9f);
    for (int i = 0; i < 4; i++) {
        ImVec2 p = layerCorner(m, ls, i);
        mn = ImVec2(std::min(mn.x, p.x), std::min(mn.y, p.y));
        mx = ImVec2(std::max(mx.x, p.x), std::max(mx.y, p.y));
    }
    return true;
}

// collects the lines a moved layer can snap to: frame edges, center, thirds, and other layers
static void collectGuides(App& a, Composition& c, double t) {
    vs.guideX = {0.f, c.w * 0.5f, (float)c.w};
    vs.guideY = {0.f, c.h * 0.5f, (float)c.h};
    if (a.showGrid)
        for (int i = 1; i < 3; i++) { vs.guideX.push_back(c.w * i / 3.f); vs.guideY.push_back(c.h * i / 3.f); }
    for (auto& lp : c.layers) {
        Layer& o = *lp;
        if (a.selLayers.count(o.id) || !o.visible || !o.activeAt(t) || o.type == LayerType::Adjustment) continue;
        ImVec2 mn, mx;
        if (!layerBounds(a, c, o, t, mn, mx)) continue;
        vs.guideX.insert(vs.guideX.end(), {mn.x, (mn.x + mx.x) * 0.5f, mx.x});
        vs.guideY.insert(vs.guideY.end(), {mn.y, (mn.y + mx.y) * 0.5f, mx.y});
    }
}

// snaps one axis: tries the box's min/center/max against every guide, returns the correction
static float snapAxis(float mn, float mx, const std::vector<float>& guides, float thr, float& hit) {
    float pts[3] = {mn, (mn + mx) * 0.5f, mx};
    float best = thr, corr = 0;
    hit = NAN;
    for (float g : guides)
        for (float p : pts)
            if (fabsf(g - p) < best) { best = fabsf(g - p); corr = g - p; hit = g; }
    return corr;
}

static Layer* hitLayer(App& a, Composition& c, ImVec2 cp) {
    for (auto& l : c.layers) {
        if (!l->visible || !l->activeAt(c.time) || l->locked || l->type == LayerType::Adjustment) continue;
        ImVec2 ls = a.project->layerSize(*l, c);
        if (ls.x <= 0) continue;
        Mat3 inv = a.project->layerMatrix(c, *l, c.time).inverse();
        ImVec2 lp = inv.apply(cp);
        if (lp.x >= 0 && lp.y >= 0 && lp.x <= ls.x && lp.y <= ls.y) return l.get();
    }
    return nullptr;
}

static void compTabs(App& a) {
    if (a.openComps.size() <= 1) return;
    if (ImGui::BeginTabBar("comps", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll)) {
        uint64_t closeId = 0;
        for (uint64_t id : a.openComps) {
            Composition* c = a.project->findComp(id);
            if (!c) continue;
            bool open = true;
            ImGuiTabItemFlags fl = id == a.activeComp ? ImGuiTabItemFlags_SetSelected : 0;
            ImGui::PushID((int)(id & 0x7fffffff));
            if (ImGui::BeginTabItem(c->name.c_str(), &open, fl)) ImGui::EndTabItem();
            if (ImGui::IsItemClicked()) a.openComp(id);
            ImGui::PopID();
            if (!open) closeId = id;
        }
        ImGui::EndTabBar();
        if (closeId) a.closeComp(closeId);
    }
}

void panelViewer(App& a) {
    Composition* c = a.comp();
    std::string title = c ? std::string(T("Composition")) + ": " + c->name : std::string(T("Composition"));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin((title + "###Composition").c_str(), &a.pViewer, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    if (!c) {
        ImVec2 av = ImGui::GetContentRegionAvail();
        const char* msg = a.project && !a.project->comps.empty() ? "Double-click a composition in the Project panel" : "New Composition (Ctrl+N)";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((av.x - ts.x) * 0.5f, av.y * 0.45f));
        if (a.project && a.project->comps.empty()) {
            ImGui::SetCursorPosX((av.x - S(220)) * 0.5f);
            if (accentButton("New Composition...", ImVec2(S(220), S(34)))) a.dlgNewComp = true;
            ImGui::SetCursorPosX((av.x - S(220)) * 0.5f);
            if (ImGui::Button("Import Footage...", ImVec2(S(220), S(30)))) a.importDialog();
        } else ImGui::TextDisabled("%s", msg);
        ImGui::End();
        return;
    }
    compTabs(a);
    float barH = ImGui::GetFrameHeight() + S(6);
    ImVec2 cpos = ImGui::GetCursorScreenPos();
    ImVec2 csz = ImGui::GetContentRegionAvail();
    csz.y -= barH;
    if (csz.x < 10 || csz.y < 10) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cpos, cpos + csz, pal::Bg0);

    float fit = std::min((csz.x - S(40)) / c->w, (csz.y - S(40)) / c->h);
    float zoom = a.viewZoom > 0 ? a.viewZoom * a.dpi : fit;  // screen pixels per comp pixel
    g_lastScreenZoom = zoom;
    ImVec2 compScreen(c->w * zoom, c->h * zoom);
    ImVec2 origin = cpos + (csz - compScreen) * 0.5f + a.viewPan;
    origin = ImVec2(std::floor(origin.x), std::floor(origin.y));
    auto toScreen = [&](ImVec2 cp) { return origin + cp * zoom; };
    auto toComp = [&](ImVec2 sp) { return (sp - origin) * (1.f / zoom); };

    dl->PushClipRect(cpos, cpos + csz, true);
    if (a.showTransparency) {
        float cs = S(8);
        dl->AddRectFilled(origin, origin + compScreen, IM_COL32(0x66, 0x66, 0x66, 0xFF));
        int nx = (int)(compScreen.x / cs) + 1, ny = (int)(compScreen.y / cs) + 1;
        if (nx * ny < 40000)
            for (int y = 0; y < ny; y++)
                for (int x = (y & 1); x < nx; x += 2) {
                    ImVec2 p = origin + ImVec2(x * cs, y * cs);
                    dl->AddRectFilled(p, ImVec2(std::min(p.x + cs, origin.x + compScreen.x), std::min(p.y + cs, origin.y + compScreen.y)),
                                      IM_COL32(0x99, 0x99, 0x99, 0xFF));
                }
    }
    if (a.viewTex) {
        if (zoom >= 2.f) dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
        dl->AddImage(a.viewTex->id(), origin, origin + compScreen);
        if (zoom >= 2.f) dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
    }
    dl->AddRect(origin - ImVec2(1, 1), origin + compScreen + ImVec2(1, 1), IM_COL32(0, 0, 0, 255));

    // guides
    if (a.showGrid) {
        for (int i = 1; i < 3; i++) {
            dl->AddLine(toScreen(ImVec2(c->w * i / 3.f, 0)), toScreen(ImVec2(c->w * i / 3.f, (float)c->h)), IM_COL32(255, 255, 255, 60));
            dl->AddLine(toScreen(ImVec2(0, c->h * i / 3.f)), toScreen(ImVec2((float)c->w, c->h * i / 3.f)), IM_COL32(255, 255, 255, 60));
        }
    }
    if (a.showSafe) {
        for (float k : {0.9f, 0.8f}) {
            ImVec2 m(c->w * (1 - k) * 0.5f, c->h * (1 - k) * 0.5f);
            dl->AddRect(toScreen(m), toScreen(ImVec2(c->w - m.x, c->h - m.y)), IM_COL32(255, 255, 255, 90));
        }
        ImVec2 ctr = toScreen(ImVec2(c->w * 0.5f, c->h * 0.5f));
        dl->AddLine(ctr - ImVec2(S(8), 0), ctr + ImVec2(S(8), 0), IM_COL32(255, 255, 255, 90));
        dl->AddLine(ctr - ImVec2(0, S(8)), ctr + ImVec2(0, S(8)), IM_COL32(255, 255, 255, 90));
    }

    // overlays for selected layers
    double t = c->time;
    for (auto& lp : c->layers) {
        Layer& l = *lp;
        if (!a.selLayers.count(l.id) || !l.activeAt(t)) continue;
        ImVec2 ls = a.project->layerSize(l, *c);
        Mat3 m = a.project->layerMatrix(*c, l, t);
        ImVec2 q[4];
        for (int i = 0; i < 4; i++) q[i] = toScreen(layerCorner(m, ls, i));
        dl->AddQuad(q[0], q[1], q[2], q[3], pal::Accent, S(1));
        float hs = S(3.5f);
        for (int i = 0; i < 4; i++) {
            dl->AddRectFilled(q[i] - ImVec2(hs, hs), q[i] + ImVec2(hs, hs), pal::Accent);
            ImVec2 mid = (q[i] + q[(i + 1) % 4]) * 0.5f;
            dl->AddRectFilled(mid - ImVec2(hs, hs), mid + ImVec2(hs, hs), pal::Accent);
        }
        ImVec2 an = toScreen(m.apply(l.anchor.v2(l.localTime(t))));
        dl->AddCircle(an, S(5), pal::White, 16, S(1));
        dl->AddLine(an - ImVec2(S(9), 0), an + ImVec2(S(9), 0), pal::White);
        dl->AddLine(an - ImVec2(0, S(9)), an + ImVec2(0, S(9)), pal::White);
        // masks
        double lt = l.localTime(t);
        for (auto& mk : l.masks) {
            ImVec2 mc = mk.center.v2(lt), ms = mk.size.v2(lt);
            if (mk.shape == 0) {
                ImVec2 r[4] = {mc + ImVec2(-ms.x, -ms.y) * 0.5f, mc + ImVec2(ms.x, -ms.y) * 0.5f, mc + ImVec2(ms.x, ms.y) * 0.5f, mc + ImVec2(-ms.x, ms.y) * 0.5f};
                for (auto& p : r) p = toScreen(m.apply(p));
                dl->AddQuad(r[0], r[1], r[2], r[3], IM_COL32(0xFF, 0xD2, 0x8A, 0xFF), S(1));
            } else {
                for (int i = 0; i < 48; i++) {
                    float an0 = i * 2 * kPi / 48;
                    dl->PathLineTo(toScreen(m.apply(mc + ImVec2(cosf(an0) * ms.x * 0.5f, sinf(an0) * ms.y * 0.5f))));
                }
                dl->PathStroke(IM_COL32(0xFF, 0xD2, 0x8A, 0xFF), ImDrawFlags_Closed, S(1));
            }
        }
        // point parameters of the effect selected in Effect Controls
        for (auto& e : l.effects) {
            if (e.id != a.selEffect) continue;
            const EffectDef* d = a.effects.find(e.defId);
            if (!d) continue;
            for (size_t i = 0; i < d->params.size() && i < e.params.size(); i++) {
                if (d->params[i].type != ParamType::Point) continue;
                ImVec2 pv = e.params[i].v2(lt);
                ImVec2 sp = toScreen(m.apply(ImVec2(pv.x * ls.x, pv.y * ls.y)));
                dl->AddCircle(sp, S(7), pal::Accent, 20, S(2));
                dl->AddLine(sp - ImVec2(S(11), 0), sp + ImVec2(S(11), 0), pal::Accent, S(1));
                dl->AddLine(sp - ImVec2(0, S(11)), sp + ImVec2(0, S(11)), pal::Accent, S(1));
            }
        }
    }

    // interaction surface
    ImGui::SetCursorScreenPos(cpos);
    ImGui::InvisibleButton("canvas", csz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    ImVec2 cm = toComp(mouse);
    if (hovered) {
        a.pickPos = cm;
        if (a.compRT) {
            float rs = (float)a.compRT->w / c->w;
            if (!gfx::readPixel(*a.compRT, (int)(cm.x * rs), (int)(cm.y * rs), a.pickRGBA)) memset(a.pickRGBA, 0, sizeof(a.pickRGBA));
        }
        if (io.MouseWheel != 0) {
            float nz = clampv(zoom * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.02f, 32.f);
            // keep the comp point under the cursor fixed
            ImVec2 before = cm;
            a.viewZoom = nz / a.dpi;
            ImVec2 newOrigin = mouse - before * nz;
            ImVec2 baseOrigin = cpos + (csz - ImVec2(c->w * nz, c->h * nz)) * 0.5f;
            a.viewPan = newOrigin - baseOrigin;
        }
    } else a.pickPos = ImVec2(-1, -1);

    auto lt0 = [&](Layer& l) { return l.localTime(t); };
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle) || (ImGui::IsItemClicked(0) && a.tool == Tool::Hand)) {
        vs.drag = Drag::Pan;
        vs.startMouse = mouse - a.viewPan;
    } else if (ImGui::IsItemClicked(0)) {
        vs.startMouse = mouse;
        vs.drag = Drag::None;
        Layer* sl = a.selectedLayer();
        if (a.tool == Tool::Zoom) {
            float nz = clampv(zoom * (io.KeyAlt ? 0.5f : 2.f), 0.02f, 32.f);
            a.viewZoom = nz / a.dpi;
            ImVec2 baseOrigin = cpos + (csz - ImVec2(c->w * nz, c->h * nz)) * 0.5f;
            a.viewPan = mouse - cm * nz - baseOrigin;
        } else if (a.tool == Tool::Text) {
            a.addText(*c, "Text", cm);
            a.tool = Tool::Select;
            a.focusPanel = "Character";
        } else if (a.tool == Tool::MaskRect || a.tool == Tool::MaskEllipse) {
            Layer* target = sl ? sl : hitLayer(a, *c, cm);
            if (target) { a.selLayers = {target->id}; vs.drag = Drag::Mask; vs.layer = target->id; }
        } else {
            // effect point handles first
            bool grabbed = false;
            if (sl) {
                ImVec2 ls = a.project->layerSize(*sl, *c);
                Mat3 m = a.project->layerMatrix(*c, *sl, t);
                for (auto& e : sl->effects) {
                    if (e.id != a.selEffect) continue;
                    const EffectDef* d = a.effects.find(e.defId);
                    if (!d) continue;
                    for (size_t i = 0; i < d->params.size() && i < e.params.size(); i++) {
                        if (d->params[i].type != ParamType::Point) continue;
                        ImVec2 pv = e.params[i].v2(lt0(*sl));
                        ImVec2 sp = toScreen(m.apply(ImVec2(pv.x * ls.x, pv.y * ls.y)));
                        if (len2(sp - mouse) < S(10)) { vs.drag = Drag::Point; vs.layer = sl->id; vs.effect = e.id; vs.pointParam = (int)i; grabbed = true; }
                    }
                }
                // scale handles
                if (!grabbed && a.tool == Tool::Select) {
                    for (int i = 0; i < 4 && !grabbed; i++) {
                        ImVec2 q0 = toScreen(layerCorner(m, ls, i)), q1 = toScreen(layerCorner(m, ls, (i + 1) % 4));
                        ImVec2 mid = (q0 + q1) * 0.5f;
                        static const int chx[4] = {-1, 1, 1, -1}, chy[4] = {-1, -1, 1, 1};
                        static const int mhx[4] = {0, 1, 0, -1}, mhy[4] = {-1, 0, 1, 0};
                        if (len2(q0 - mouse) < S(7)) { vs.hx = chx[i]; vs.hy = chy[i]; grabbed = true; }
                        else if (len2(mid - mouse) < S(7)) { vs.hx = mhx[i]; vs.hy = mhy[i]; grabbed = true; }
                    }
                    if (grabbed) { vs.drag = Drag::Scale; vs.layer = sl->id; vs.startScale = sl->scale.v2(lt0(*sl)); }
                }
            }
            if (!grabbed) {
                Layer* h = hitLayer(a, *c, cm);
                if (h && !a.selLayers.count(h->id)) a.selectLayer(h->id, io.KeyShift || io.KeyCtrl, false);
                if (!h && !io.KeyShift) a.selLayers.clear();
                if (h) {
                    vs.layer = h->id;
                    if (a.tool == Tool::Rotate) { vs.drag = Drag::Rotate; vs.startRot = h->rotation.f(lt0(*h)); }
                    else if (a.tool == Tool::Anchor) {
                        vs.drag = Drag::Anchor;
                        vs.startAnchor = h->anchor.v2(lt0(*h));
                        vs.startPosSingle = h->position.v2(lt0(*h));
                    } else {
                        vs.drag = Drag::Move;
                        vs.startPos.clear();
                        for (auto* l : a.selectedLayers()) if (!l->locked) vs.startPos.push_back({l->id, l->position.v2(lt0(*l))});
                        if (!layerBounds(a, *c, *h, t, vs.boxMin, vs.boxMax)) vs.boxMin = vs.boxMax = h->position.v2(lt0(*h));
                        collectGuides(a, *c, t);
                    }
                }
            }
        }
    }
    if (vs.drag != Drag::None && ImGui::IsMouseDragging(vs.drag == Drag::Pan ? ImGuiMouseButton_Middle : 0, 0.f) ||
        (vs.drag == Drag::Pan && ImGui::IsMouseDragging(0, 0.f))) {
        ImVec2 d = (mouse - vs.startMouse) * (1.f / zoom);
        Layer* l = c->find(vs.layer);
        switch (vs.drag) {
        case Drag::Pan: a.viewPan = mouse - vs.startMouse; break;
        case Drag::Move: {
            if (io.KeyShift) { if (fabsf(d.x) > fabsf(d.y)) d.y = 0; else d.x = 0; }
            vs.snapX = vs.snapY = NAN;
            // magnet: edges/center of the grabbed layer stick to frame lines and other layers (Alt = free move)
            if (a.settings.snapping && !io.KeyAlt) {
                float thr = S(8) / zoom;
                if (!(io.KeyShift && d.x == 0)) d.x += snapAxis(vs.boxMin.x + d.x, vs.boxMax.x + d.x, vs.guideX, thr, vs.snapX);
                if (!(io.KeyShift && d.y == 0)) d.y += snapAxis(vs.boxMin.y + d.y, vs.boxMax.y + d.y, vs.guideY, thr, vs.snapY);
            }
            for (auto& sp : vs.startPos)
                if (Layer* ml = c->find(sp.first)) { ml->position.set2(lt0(*ml), sp.second.x + d.x, sp.second.y + d.y); }
            // smart guides
            ImU32 gc = IM_COL32(0xFF, 0x88, 0x01, 0xE6);
            if (!std::isnan(vs.snapX)) dl->AddLine(toScreen(ImVec2(vs.snapX, -100000.f)), toScreen(ImVec2(vs.snapX, 100000.f)), gc, S(1));
            if (!std::isnan(vs.snapY)) dl->AddLine(toScreen(ImVec2(-100000.f, vs.snapY)), toScreen(ImVec2(100000.f, vs.snapY)), gc, S(1));
            a.markChanged();
            break;
        }
        case Drag::Scale:
            if (l) {
                Mat3 m = a.project->layerMatrix(*c, *l, t);
                ImVec2 an = toScreen(m.apply(l->anchor.v2(lt0(*l))));
                float rot = l->rotation.f(lt0(*l)) * kPi / 180.f;
                auto local = [&](ImVec2 p) { ImVec2 v = p - an; return ImVec2(cosf(-rot) * v.x - sinf(-rot) * v.y, sinf(-rot) * v.x + cosf(-rot) * v.y); };
                ImVec2 v0 = local(vs.startMouse), v1 = local(mouse);
                float kx = vs.hx != 0 && fabsf(v0.x) > 1 ? v1.x / v0.x : 1.f;
                float ky = vs.hy != 0 && fabsf(v0.y) > 1 ? v1.y / v0.y : 1.f;
                if (io.KeyShift) {
                    float k = vs.hx != 0 && vs.hy != 0 ? std::max(kx, ky) : (vs.hx != 0 ? kx : ky);
                    kx = ky = k;
                }
                l->scale.set2(lt0(*l), vs.startScale.x * kx, vs.startScale.y * ky);
                a.markChanged();
            }
            break;
        case Drag::Rotate:
            if (l) {
                Mat3 m = a.project->layerMatrix(*c, *l, t);
                ImVec2 an = toScreen(m.apply(l->anchor.v2(lt0(*l))));
                float a0 = atan2f(vs.startMouse.y - an.y, vs.startMouse.x - an.x), a1 = atan2f(mouse.y - an.y, mouse.x - an.x);
                float deg = vs.startRot + (a1 - a0) * 180.f / kPi;
                if (io.KeyShift) deg = std::round(deg / 45.f) * 45.f;
                l->rotation.setF(lt0(*l), deg);
                a.markChanged();
            }
            break;
        case Drag::Anchor:
            if (l) {
                // move anchor in layer space and compensate position so the picture stays put
                ImVec2 s = l->scale.v2(lt0(*l));
                float rot = l->rotation.f(lt0(*l)) * kPi / 180.f;
                ImVec2 ld(cosf(-rot) * d.x - sinf(-rot) * d.y, sinf(-rot) * d.x + cosf(-rot) * d.y);
                ld.x /= std::max(0.001f, s.x / 100.f);
                ld.y /= std::max(0.001f, s.y / 100.f);
                l->anchor.set2(lt0(*l), vs.startAnchor.x + ld.x, vs.startAnchor.y + ld.y);
                l->position.set2(lt0(*l), vs.startPosSingle.x + d.x, vs.startPosSingle.y + d.y);
                a.markChanged();
            }
            break;
        case Drag::Point:
            if (l) {
                ImVec2 ls = a.project->layerSize(*l, *c);
                ImVec2 lpx = a.project->layerMatrix(*c, *l, t).inverse().apply(cm);
                for (auto& e : l->effects)
                    if (e.id == vs.effect && vs.pointParam < (int)e.params.size())
                        e.params[vs.pointParam].set2(lt0(*l), lpx.x / std::max(1.f, ls.x), lpx.y / std::max(1.f, ls.y));
                a.markChanged();
            }
            break;
        case Drag::Mask: {
            ImVec2 p0 = vs.startMouse, p1 = mouse;
            dl->AddRect(ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y)),
                        IM_COL32(0xFF, 0xD2, 0x8A, 0xFF));
            break;
        }
        default: break;
        }
    }
    if (vs.drag == Drag::Mask && ImGui::IsMouseReleased(0)) {
        Layer* l = c->find(vs.layer);
        ImVec2 d = mouse - vs.startMouse;
        if (l && fabsf(d.x) > 4 && fabsf(d.y) > 4) {
            Mat3 inv = a.project->layerMatrix(*c, *l, t).inverse();
            ImVec2 a0 = inv.apply(toComp(vs.startMouse)), a1 = inv.apply(cm);
            Mask m;
            m.id = newId();
            m.shape = a.tool == Tool::MaskEllipse ? 1 : 0;
            m.name = strf("Mask %d", (int)l->masks.size() + 1);
            m.center = Prop(2, (a0.x + a1.x) * 0.5f, (a0.y + a1.y) * 0.5f);
            m.size = Prop(2, fabsf(a1.x - a0.x), fabsf(a1.y - a0.y));
            l->masks.push_back(m);
            l->expanded = true;
            l->maskOpen = true;
            a.markChanged();
        }
    }
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2)) vs.drag = Drag::None;

    if (hovered && vs.drag == Drag::None) {
        if (a.tool == Tool::Hand) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (a.tool == Tool::Text) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        else if (a.tool == Tool::MaskRect || a.tool == Tool::MaskEllipse || a.tool == Tool::Anchor) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // drop footage / comps / effects onto the viewer
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
            uint64_t id = *(const uint64_t*)pl->Data;
            if (Footage* f = a.project->findFootage(id)) a.addFootageLayer(*c, *f, c->time, 0);
            else if (Composition* sc = a.project->findComp(id)) a.addCompLayer(*c, *sc, c->time, 0);
        }
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_EFFECT)) {
            int idx = *(const int*)pl->Data;
            if (idx >= 0 && idx < (int)a.effects.defs.size()) {
                Layer* h = hitLayer(a, *c, cm);
                a.applyEffect(a.effects.defs[idx].get(), h);
            }
        }
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_PRESET)) {
            int idx = *(const int*)pl->Data;
            auto files = a.presetFiles();
            if (idx >= 0 && idx < (int)files.size()) a.applyPreset(files[idx], hitLayer(a, *c, cm));
        }
        ImGui::EndDragDropTarget();
    }
    dl->PopClipRect();

    // bottom bar
    ImGui::SetCursorScreenPos(cpos + ImVec2(S(6), csz.y + S(3)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(2)));
    {
        static const float zooms[] = {0, 0.125f, 0.25f, 0.333f, 0.5f, 0.75f, 1, 1.5f, 2, 4, 8};
        std::string zl = a.viewZoom <= 0 ? strf("Fit (%d%%)", (int)std::lround(zoom / a.dpi * 100)) : strf("%g%%", std::round(a.viewZoom * 1000) / 10);
        ImGui::SetNextItemWidth(S(96));
        if (ImGui::BeginCombo("##zoom", zl.c_str())) {
            for (float z : zooms)
                if (ImGui::Selectable(z == 0 ? "Fit" : strf("%g%%", z * 100).c_str(), a.viewZoom == z)) { a.viewZoom = z; a.viewPan = ImVec2(0, 0); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        static const char* res[] = {"Auto", "Full", "Half", "Third", "Quarter"};
        ImGui::SetNextItemWidth(S(78));
        if (ImGui::Combo("##res", &a.settings.previewRes, res, 5)) a.needRender = true;
        tooltip("Preview resolution");
        // narrow panels: the timecode hides and the view toggles fold into a "..." menu, nothing runs off the edge
        float barW = csz.x - S(12);
        bool roomTc = barW > S(640), roomToggles = barW > S(470);
        if (roomTc) {
            ImGui::SameLine(0, S(12));
            double tt = c->time;
            if (timecodeField("##vtc", &tt, c->fps, pal::Accent)) a.setTime(tt);
        }
        ImGui::SameLine(0, S(12));
        if (iconButton("snap", Icon::Camera, false, "Take snapshot: save current frame as PNG")) {
            std::string p = dlgSaveFile(a.hwnd, L"PNG image\0*.png\0", "png", c->name + "_" + std::to_string(timeToFrame(c->time, c->fps)), "Save Frame");
            if (!p.empty()) {
                RenderParams rp;
                rp.sync = true;
                TexPtr img = a.renderer.renderComp(*a.project, *c, c->time, rp);
                TexPtr o = img ? a.renderer.finalizeRGBA8(*img, c->bg, true) : nullptr;
                std::vector<uint8_t> px;
                if (o && gfx::readback(*o, px) && gfx::savePNG(p, px.data(), o->w, o->h)) a.notify("Frame saved: " + fileName(p));
            }
        }
        ImGui::SameLine(0, S(2));
        iconToggle("magnet", Icon::Snap, &a.settings.snapping,
                   "Magnet: layers snap to the frame edges, center (and thirds when the grid is on) and to other layers.\nHold Alt while dragging to move freely.");
        static const char* ch[] = {"RGB", "Red", "Green", "Blue", "Alpha"};
        bool mb = c->motionBlur;
        if (roomToggles) {
            ImGui::SameLine(0, S(2));
            if (iconToggle("trans", Icon::Checker, &a.showTransparency, "Toggle Transparency Grid")) a.needRender = true;
            ImGui::SameLine(0, S(2));
            iconToggle("grid", Icon::Grid, &a.showGrid, "Rule-of-thirds grid");
            ImGui::SameLine(0, S(2));
            iconToggle("safe", Icon::Safe, &a.showSafe, "Title/Action Safe");
            ImGui::SameLine(0, S(2));
            if (iconToggle("mb", Icon::MotionBlur, &mb, "Motion Blur master switch")) { c->motionBlur = mb; a.markChanged(); }
            ImGui::SameLine(0, S(8));
            ImGui::SetNextItemWidth(S(70));
            if (ImGui::Combo("##ch", &a.viewChannel, ch, 5)) a.needRender = true;
            tooltip("Show channel");
        } else {
            ImGui::SameLine(0, S(4));
            if (ImGui::Button("...##vmore")) ImGui::OpenPopup("vmore");
            tooltip("More view options");
            if (ImGui::BeginPopup("vmore")) {
                if (ImGui::MenuItem("Transparency Grid", nullptr, a.showTransparency)) { a.showTransparency = !a.showTransparency; a.needRender = true; }
                if (ImGui::MenuItem("Grid", "Ctrl+'", a.showGrid)) a.showGrid = !a.showGrid;
                if (ImGui::MenuItem("Title/Action Safe", "'", a.showSafe)) a.showSafe = !a.showSafe;
                if (ImGui::MenuItem("Motion Blur (master switch)", nullptr, mb)) { c->motionBlur = !mb; a.markChanged(); }
                if (ImGui::BeginMenu("Show Channel")) {
                    for (int i = 0; i < 5; i++) if (ImGui::MenuItem(ch[i], nullptr, a.viewChannel == i)) { a.viewChannel = i; a.needRender = true; }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
        }
        if (a.renderer.pending && barW > S(560)) {
            ImGui::SameLine(0, S(12));
            ImGui::TextDisabled("decoding...");
        }
        // full screen: always at the right edge
        float fh = ImGui::GetFrameHeight();
        ImGui::SameLine();
        float fx = cpos.x + csz.x - fh - S(6);
        if (ImGui::GetCursorScreenPos().x < fx) ImGui::SetCursorScreenPos(ImVec2(fx, ImGui::GetCursorScreenPos().y));
        if (iconButton("full", Icon::Fullscreen, false, "Full screen preview (F11). Esc to exit")) a.setFullPreview(true);
    }
    ImGui::PopStyleVar();
    ImGui::End();
}
