#include "ui.h"
#include "i18n.h"
#include "imgui_internal.h"

namespace {
struct Row {
    int kind = 0;  // 0 layer, 1 group, 2 property
    Layer* layer = nullptr;
    int layerIndex = 0;
    std::string label;
    Prop* prop = nullptr;
    PropUI ui;
    bool* open = nullptr;
    int depth = 0;
    EffectInst* fx = nullptr;
    Mask* mask = nullptr;
    int group = 0;  // 1 transform 2 effects 3 effect 4 masks 5 mask 6 text 7 audio
    std::string uid;
};

struct TLState {
    float leftW = 0;
    bool dragSplit = false;
    // bar dragging
    int barMode = 0;  // 1 move 2 trim in 3 trim out
    uint64_t barLayer = 0;
    double grabT = 0;
    std::vector<std::tuple<uint64_t, double, double, double>> barStart;  // id, start, in, out
    // keyframe dragging
    bool keyDrag = false;
    double keyGrabT = 0;
    std::vector<std::pair<Keyframe*, double>> keyStart;
    // work area / scrub
    int waDrag = 0;
    double waStart0 = 0, waEnd0 = 0, waGrab = 0;
    bool scrubbing = false;
    uint64_t renaming = 0;
    char renameBuf[256] = {};
    bool linkScale = true;
    char search[64] = {};
    bool hideShy = false;
    struct SnapPt {
        double t;
        uint64_t layer;  // clip the target belongs to (0 = playhead / marker / comp)
        int kind;        // 0 comp start/end, 1 work area, 2 playhead, 3 marker, 4 clip edge, 5 keyframe
    };
    std::vector<SnapPt> snapPts;
    SnapPt snapHit{-1, 0, 0};  // what the dragged thing is stuck to (shown with a guide)
    // rubber-band selection: press on an empty spot and drag over layer bars / keyframes
    bool marquee = false, marqueeAdd = false;
    ImVec2 mqStart;
    float mqStartX = 0, mqStartY = 0;  // screen x, y inside the scrolled content
    double mqStartT = 0;
    std::vector<std::pair<uint64_t, ImRect>> barRects;
    std::vector<std::pair<Keyframe*, ImVec2>> keyPts;
    // magnet: time of the guide line shown while something sticks to a target
    double snapLine = -1;
    double keyRefT = 0;  // comp time of the grabbed keyframe
    // keyframe parameters popup (right-click on a key): layer-local time, optional single property
    double kpT = 0;
    Prop* kpProp = nullptr;
} ts;

double xToT(float x);

void startMarquee(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    ts.marquee = true;
    ts.marqueeAdd = io.KeyShift || io.KeyCtrl;
    ts.mqStart = io.MousePos;
    // remembered in content coordinates (scroll offset / time), so scrolling during the drag keeps the box anchored
    ts.mqStartY = io.MousePos.y - ImGui::GetCursorStartPos().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
    ts.mqStartX = io.MousePos.x;
    ts.mqStartT = xToT(io.MousePos.x);
    if (!ts.marqueeAdd) { a.selLayers.clear(); a.clearKeySelection(); }
}

float g_pps = 100;    // pixels per second
double g_scroll = 0;  // seconds at left edge
float g_rx0 = 0, g_rw = 0;

float tToX(double t) { return g_rx0 + (float)((t - g_scroll) * g_pps); }
double xToT(float x) { return g_scroll + (x - g_rx0) / g_pps; }

enum SnapFor { SnapClips = 0, SnapPlayhead = 1, SnapKeys = 2 };

// magnet targets: comp start/end, work area, markers, the playhead and the edges of the other clips;
// the playhead also sticks to keyframes, dragged keys stick to the other keys
void collectSnaps(App& a, Composition& c, int mode) {
    ts.snapPts.clear();
    auto add = [](double t, uint64_t layer, int kind) { ts.snapPts.push_back({t, layer, kind}); };
    add(0, 0, 0);
    add(c.duration, 0, 0);
    add(c.workStart, 0, 1);
    add(c.workEnd, 0, 1);
    if (mode != SnapPlayhead) add(c.time, 0, 2);
    for (auto& m : c.markers) add(m.t, 0, 3);
    for (auto& l : c.layers) {
        if (mode == SnapClips && a.selLayers.count(l->id)) continue;  // these are the ones being moved
        add(l->inPoint, l->id, 4);
        add(l->outPoint, l->id, 4);
        if (mode == SnapClips) continue;
        for (auto& pr : a.layerProps(*l))
            if (pr.second->animated)
                for (auto& k : pr.second->keys)
                    if (mode == SnapPlayhead || !k.sel) add(l->startTime + k.t, l->id, 5);
    }
}
bool magnetOn(App& a) { return a.settings.snapping && !ImGui::GetIO().KeyAlt; }
double snapRadius() { return S(12) / g_pps; }
double frameRound(double t, double fps) { return std::round(t * fps) / fps; }
// nearest target closer than `dist` (dist shrinks to the found distance); remembers what was hit
bool nearestSnap(double t, double& best, double& dist) {
    bool found = false;
    for (auto& s : ts.snapPts)
        if (fabs(s.t - t) < dist) { dist = fabs(s.t - t); best = s.t; ts.snapHit = s; found = true; }
    return found;
}
double snap(App& a, double t, double fps) {
    if (magnetOn(a)) {
        double best = t, dist = snapRadius();
        if (nearestSnap(t, best, dist)) { ts.snapLine = best; return best; }
    }
    return frameRound(t, fps);
}

// all keyframe times of a layer (layer-local), merged across its properties
std::vector<double> layerKeyTimes(App& a, Layer& l) {
    std::vector<double> r;
    for (auto& pr : a.layerProps(l))
        if (pr.second->animated)
            for (auto& k : pr.second->keys) r.push_back(k.t);
    std::sort(r.begin(), r.end());
    std::vector<double> u;
    for (double t : r) if (u.empty() || t - u.back() > 1e-4) u.push_back(t);
    return u;
}
bool keysAtSelected(App& a, Layer& l, double t) {
    for (auto& pr : a.layerProps(l)) {
        int i = pr.second->animated ? pr.second->keyAt(t) : -1;
        if (i >= 0 && pr.second->keys[i].sel) return true;
    }
    return false;
}
void selectKeysAt(App& a, Layer& l, double t, bool on) {
    for (auto& pr : a.layerProps(l)) {
        int i = pr.second->animated ? pr.second->keyAt(t) : -1;
        if (i >= 0) pr.second->keys[i].sel = on;
    }
}
void beginKeyDrag(App& a, Composition& c, double refCompT) {
    ts.keyDrag = true;
    ts.keyGrabT = xToT(ImGui::GetIO().MousePos.x);
    ts.keyRefT = refCompT;
    ts.keyStart.clear();
    for (auto& lp : c.layers)
        for (auto& pr : a.layerProps(*lp))
            for (auto& kk : pr.second->keys) if (kk.sel) ts.keyStart.push_back({&kk, kk.t});
    collectSnaps(a, c, SnapKeys);
}
void moveKeys(App& a, Composition& c) {
    double d = xToT(ImGui::GetIO().MousePos.x) - ts.keyGrabT;
    double off = frameRound(d, c.fps);
    if (magnetOn(a)) {
        double best, dist = snapRadius();
        if (nearestSnap(ts.keyRefT + d, best, dist)) { off = best - ts.keyRefT; ts.snapLine = best; }
    }
    for (auto& ks : ts.keyStart) ks.first->t = ks.second + off;
    a.markChanged();
}
void endKeyDrag(App& a, Composition& c) {
    ts.keyDrag = false;
    ts.keyStart.clear();
    for (auto& lp : c.layers) for (auto& pr : a.layerProps(*lp)) pr.second->sortKeys();
}

std::string propName(App& a, Layer& l, const PropRef& r) {
    static const char* xf[] = {"Anchor Point", "Position", "Scale", "Rotation", "Opacity", "Position Z", "X Rotation", "Y Rotation"};
    static const char* mk[] = {"Mask Center", "Mask Size", "Mask Feather", "Roundness", "Mask Opacity", "Mask Expansion"};
    static const char* tx[] = {"Font Size", "Fill Color", "Stroke Color", "Stroke Width", "Tracking", "Line Spacing"};
    static const char* cm[] = {"Point of Interest", "Point of Interest Z", "Zoom", "Focus Distance", "Aperture", "Blur Level"};
    static const char* lg[] = {"Point of Interest", "Point of Interest Z", "Color", "Intensity", "Cone Angle", "Cone Feather", "Radius", "Falloff Distance"};
    static const char* mt[] = {"Ambient", "Diffuse", "Specular", "Shininess"};
    switch (r.kind) {
    case 0: return T(xf[clampv(r.index, 0, 7)]);
    case 5: return T(cm[clampv(r.index, 0, 5)]);
    case 6: return T(lg[clampv(r.index, 0, 7)]);
    case 7: return T(mt[clampv(r.index, 0, 3)]);
    case 1:
        for (auto& e : l.effects) {
            if (e.id != r.owner) continue;
            std::string pn = r.index == -1 ? T("Intensity") : r.index == -2 ? T("Speed") : "";
            if (r.index >= 0) {
                const EffectDef* d = a.effects.find(e.defId);
                pn = d && r.index < (int)d->params.size() ? T(d->params[r.index].label.c_str()) : strf("#%d", r.index);
            }
            return e.name + " / " + pn;
        }
        return "Effect";
    case 2:
        for (auto& m : l.masks) if (m.id == r.owner) return m.name + " / " + T(mk[clampv(r.index, 0, 5)]);
        return "Mask";
    case 3: return T(tx[clampv(r.index, 0, 5)]);
    default: return T("Audio Levels");
    }
}

// right-click on a keyframe: its values, interpolation and the Graph Editor
void keyPopup(App& a, Composition& c, Layer& l) {
    struct KP {
        std::string name;
        Prop* p;
        int idx;
    };
    std::vector<KP> list;
    for (auto& pr : a.layerProps(l)) {
        if (ts.kpProp && pr.second != ts.kpProp) continue;
        int i = pr.second->animated ? pr.second->keyAt(ts.kpT) : -1;
        if (i >= 0) list.push_back({propName(a, l, pr.first), pr.second, i});
    }
    if (list.empty()) { ImGui::CloseCurrentPopup(); return; }
    ImGui::PushFont(a.fBold);
    ImGui::TextUnformatted(T("Keyframe"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s  ·  %s", timeLabel(l.startTime + ts.kpT, c.fps).c_str(), l.name.c_str());
    if (ImGui::Button("Go to Keyframe")) a.setTime(l.startTime + ts.kpT);
    ImGui::SameLine();
    if (ImGui::Button("Delete Keyframe")) {
        for (auto& kp : list) {
            float cur[4];
            kp.p->eval(ts.kpT, cur);
            kp.p->keys.erase(kp.p->keys.begin() + kp.idx);
            if (kp.p->keys.empty()) { kp.p->animated = false; memcpy(kp.p->v, cur, sizeof(cur)); }
        }
        a.markChanged();
        ImGui::CloseCurrentPopup();
        return;
    }
    ImGui::SeparatorText("Properties");
    for (auto& kp : list) {
        const Keyframe& k = kp.p->keys[kp.idx];
        ImGui::TextUnformatted(kp.name.c_str());
        ImGui::SameLine(S(200));
        if (kp.p->dim == 4) ImGui::ColorButton("##c", ImVec4(k.v[0], k.v[1], k.v[2], k.v[3]), ImGuiColorEditFlags_NoTooltip, ImVec2(S(40), ImGui::GetTextLineHeight()));
        else if (kp.p->dim == 2) ImGui::TextDisabled("%.1f, %.1f", k.v[0], k.v[1]);
        else ImGui::TextDisabled("%.2f", k.v[0]);
    }
    ImGui::SeparatorText("Interpolation");
    auto setInterp = [&](int mode) {
        for (auto& kp : list) {
            Keyframe& k = kp.p->keys[kp.idx];
            k.curve = Curve::Classic;
            switch (mode) {
            case 0: k.in = k.out = Interp::Linear; break;
            case 1: k.in = k.out = Interp::Ease; k.infIn = k.infOut = 33.333f; break;
            case 2: k.in = Interp::Ease; k.infIn = 33.333f; break;
            case 3: k.out = Interp::Ease; k.infOut = 33.333f; break;
            case 4: k.out = Interp::Hold; break;
            }
        }
        a.markChanged();
    };
    if (ImGui::Button("Linear")) setInterp(0);
    ImGui::SameLine(0, S(4));
    if (ImGui::Button("Easy Ease")) setInterp(1);
    ImGui::SameLine(0, S(4));
    if (ImGui::Button("Ease In")) setInterp(2);
    ImGui::SameLine(0, S(4));
    if (ImGui::Button("Ease Out")) setInterp(3);
    ImGui::SameLine(0, S(4));
    if (ImGui::Button("Hold")) setInterp(4);
    ImGui::SeparatorText("Graph Editor");
    std::vector<CurveSeg> segs;
    bool outgoing = false;
    for (auto& kp : list) {
        auto& ks = kp.p->keys;
        if (kp.p->stepped) continue;
        if (kp.idx + 1 < (int)ks.size()) segs.push_back({&ks[kp.idx], &ks[kp.idx + 1]}), outgoing = true;
        else if (kp.idx > 0) segs.push_back({&ks[kp.idx - 1], &ks[kp.idx]});
    }
    if (!segs.empty()) ImGui::TextDisabled("%s", T(outgoing ? "Curve from this keyframe to the next one" : "Curve from the previous keyframe to this one"));
    ImGui::BeginChild("##gc", ImVec2(S(340), S(600)), ImGuiChildFlags_None);
    bool applied = false;
    keyCurveSection(a, segs, &applied);
    ImGui::EndChild();
    if (applied) ImGui::CloseCurrentPopup();
}

void drawKey(ImDrawList* dl, ImVec2 c, float r, const Keyframe& k, bool hover) {
    ImU32 fill = k.sel ? pal::Accent : (hover ? IM_COL32(0xE0, 0xE0, 0xE0, 0xFF) : IM_COL32(0xA8, 0xA8, 0xA8, 0xFF));
    ImU32 edge = IM_COL32(0x10, 0x10, 0x10, 0xFF);
    if (k.out == Interp::Hold) {
        dl->AddRectFilled(c - ImVec2(r * 0.8f, r), c + ImVec2(r * 0.8f, r), fill);
        dl->AddRect(c - ImVec2(r * 0.8f, r), c + ImVec2(r * 0.8f, r), edge);
    } else if (k.in == Interp::Ease || k.out == Interp::Ease) {
        // easy ease: hourglass-like shape
        ImVec2 pts[6] = {c + ImVec2(-r * 0.9f, -r), c + ImVec2(r * 0.9f, -r), c + ImVec2(r * 0.25f, 0), c + ImVec2(r * 0.9f, r), c + ImVec2(-r * 0.9f, r),
                         c + ImVec2(-r * 0.25f, 0)};
        dl->AddConcavePolyFilled(pts, 6, fill);
        dl->AddPolyline(pts, 6, edge, 1.0f, ImDrawFlags_Closed);
    } else {
        dl->AddQuadFilled(c + ImVec2(0, -r), c + ImVec2(r, 0), c + ImVec2(0, r), c + ImVec2(-r, 0), fill);
        dl->AddQuad(c + ImVec2(0, -r), c + ImVec2(r, 0), c + ImVec2(0, r), c + ImVec2(-r, 0), edge);
    }
}

PropUI simpleUI(int kind, float speed = 1, float mn = -1e9f, float mx = 1e9f, const char* unit = "") {
    PropUI u;
    u.kind = kind;
    u.speed = speed;
    u.mn = mn;
    u.mx = mx;
    u.unit = unit;
    return u;
}

void buildRows(App& a, Composition& c, std::vector<Row>& rows) {
    int idx = 0;
    for (auto& lp : c.layers) {
        Layer& l = *lp;
        idx++;
        if (ts.hideShy && l.shy) continue;
        if (ts.search[0] && toLower(l.name).find(toLower(ts.search)) == std::string::npos) continue;
        Row r;
        r.kind = 0;
        r.layer = &l;
        r.layerIndex = idx - 1;
        r.label = l.name;
        r.uid = strf("L%llu", (unsigned long long)l.id);
        rows.push_back(r);
        if (!l.expanded) continue;
        auto group = [&](const std::string& label, bool* open, int depth, int g, const std::string& uid) {
            Row gr;
            gr.kind = 1;
            gr.layer = &l;
            gr.layerIndex = idx - 1;
            gr.label = label;
            gr.open = open;
            gr.depth = depth;
            gr.group = g;
            gr.uid = uid;
            rows.push_back(gr);
            return *open;
        };
        auto prop = [&](const std::string& label, Prop* p, PropUI ui, int depth, const std::string& uid) {
            Row pr;
            pr.kind = 2;
            pr.layer = &l;
            pr.layerIndex = idx - 1;
            pr.label = label;
            pr.prop = p;
            pr.ui = ui;
            pr.depth = depth;
            pr.uid = uid;
            rows.push_back(pr);
        };
        std::string lu = r.uid;
        ImVec2 ls = a.project->layerSize(l, c);
        if (!l.effects.empty() && group("Effects", &l.fxOpen, 1, 2, lu + "fx")) {
            for (auto& e : l.effects) {
                const EffectDef* d = a.effects.find(e.defId);
                Row er;
                er.kind = 1;
                er.layer = &l;
                er.layerIndex = idx - 1;
                er.label = e.name;
                er.open = &e.expanded;
                er.depth = 2;
                er.group = 3;
                er.fx = &e;
                er.uid = lu + strf("e%llu", (unsigned long long)e.id);
                rows.push_back(er);
                if (!e.expanded) continue;
                bool manual = d && !d->autoMix;
                prop("Intensity", &e.intensity, simpleUI(6, 0.5f, 0, manual ? 200.f : 100.f), 3, er.uid + "i");
                if (d && d->animated) prop("Speed", &e.speed, simpleUI(0, 0.01f, 0, 20, "x"), 3, er.uid + "s");
                if (d)
                    for (size_t i = 0; i < d->params.size() && i < e.params.size(); i++) {
                        PropUI u = uiForParam(d->params[i], ls);
                        prop(d->params[i].label, &e.params[i], u, 3, er.uid + strf("p%d", (int)i));
                    }
            }
        }
        if (!l.masks.empty() && group("Masks", &l.maskOpen, 1, 4, lu + "mk")) {
            for (auto& m : l.masks) {
                Row mr;
                mr.kind = 1;
                mr.layer = &l;
                mr.layerIndex = idx - 1;
                mr.label = m.name;
                mr.open = &m.expanded;
                mr.depth = 2;
                mr.group = 5;
                mr.mask = &m;
                mr.uid = lu + strf("m%llu", (unsigned long long)m.id);
                rows.push_back(mr);
                if (!m.expanded) continue;
                prop("Mask Center", &m.center, simpleUI(1), 3, mr.uid + "c");
                prop("Mask Size", &m.size, simpleUI(1, 1, 0), 3, mr.uid + "s");
                prop("Mask Feather", &m.feather, simpleUI(0, 0.5f, 0, 2000, "px"), 3, mr.uid + "f");
                prop("Roundness", &m.roundness, simpleUI(0, 0.5f, 0, 5000, "px"), 3, mr.uid + "r");
                prop("Mask Opacity", &m.opacity, simpleUI(6, 0.5f, 0, 100), 3, mr.uid + "o");
                prop("Mask Expansion", &m.expansion, simpleUI(0, 0.5f, -5000, 5000, "px"), 3, mr.uid + "e");
            }
        }
        if (l.type == LayerType::Text && group("Text", &l.textOpen, 1, 6, lu + "tx")) {
            prop("Font Size", &l.text.size, simpleUI(0, 0.5f, 1, 2000, "px"), 2, lu + "t0");
            prop("Fill Color", &l.text.fill, simpleUI(2), 2, lu + "t1");
            prop("Stroke Color", &l.text.stroke, simpleUI(2), 2, lu + "t2");
            prop("Stroke Width", &l.text.strokeWidth, simpleUI(0, 0.1f, 0, 200, "px"), 2, lu + "t3");
            prop("Tracking", &l.text.tracking, simpleUI(0, 1, -500, 2000), 2, lu + "t4");
            prop("Line Spacing", &l.text.leading, simpleUI(0, 0.01f, 0.3f, 5, "x"), 2, lu + "t5");
        }
        bool camLight = l.type == LayerType::Camera || l.type == LayerType::Light;
        if (l.type == LayerType::Camera && group("Camera Options", &l.optOpen, 1, 8, lu + "cam")) {
            prop("Zoom", &l.camZoom, simpleUI(0, 2, 1, 100000, "px"), 2, lu + "cz");
            prop("Point of Interest", &l.target, simpleUI(1), 2, lu + "ct");
            prop("Point of Interest Z", &l.targetZ, simpleUI(0, 1, -100000, 100000, "px"), 2, lu + "ctz");
            prop("Focus Distance", &l.focusDist, simpleUI(0, 2, 1, 100000, "px"), 2, lu + "cf");
            prop("Aperture", &l.aperture, simpleUI(0, 0.5f, 0, 2000, "px"), 2, lu + "ca");
            prop("Blur Level", &l.blurLevel, simpleUI(6, 0.5f, 0, 500), 2, lu + "cb");
        }
        if (l.type == LayerType::Light && group("Light Options", &l.optOpen, 1, 9, lu + "lo")) {
            prop("Intensity", &l.lightIntensity, simpleUI(6, 0.5f, 0, 1000), 2, lu + "li");
            prop("Color", &l.lightColor, simpleUI(2), 2, lu + "lc");
            if (l.lightKind != LK_Ambient) {
                prop("Point of Interest", &l.target, simpleUI(1), 2, lu + "lt");
                prop("Point of Interest Z", &l.targetZ, simpleUI(0, 1, -100000, 100000, "px"), 2, lu + "ltz");
            }
            if (l.lightKind == LK_Spot) {
                prop("Cone Angle", &l.coneAngle, simpleUI(5, 0.5f, 1, 179), 2, lu + "la");
                prop("Cone Feather", &l.coneFeather, simpleUI(6, 0.5f, 0, 100), 2, lu + "lf");
            }
            if (l.lightKind == LK_Point || l.lightKind == LK_Spot) {
                prop("Radius", &l.lightRadius, simpleUI(0, 1, 0, 100000, "px"), 2, lu + "lr");
                prop("Falloff Distance", &l.lightFalloff, simpleUI(0, 1, 0, 100000, "px"), 2, lu + "lfd");
            }
        }
        if (group("Transform", &l.xfOpen, 1, 1, lu + "xf")) {
            if (!camLight) prop("Anchor Point", &l.anchor, simpleUI(1), 2, lu + "a");
            if (!(l.type == LayerType::Light && l.lightKind == LK_Ambient)) prop("Position", &l.position, simpleUI(1), 2, lu + "p");
            if (l.threeD || (camLight && l.lightKind != LK_Ambient) || l.type == LayerType::Camera) prop("Position Z", &l.posZ, simpleUI(0, 1, -100000, 100000, "px"), 2, lu + "pz");
            if (!camLight) prop("Scale", &l.scale, simpleUI(8, 0.25f, -10000, 10000), 2, lu + "s");
            if (l.threeD || l.type == LayerType::Camera) {
                prop("X Rotation", &l.rotX, simpleUI(5, 0.5f), 2, lu + "rx");
                prop("Y Rotation", &l.rotY, simpleUI(5, 0.5f), 2, lu + "ry");
            }
            if (l.type != LayerType::Light) prop(l.threeD || l.type == LayerType::Camera ? "Z Rotation" : "Rotation", &l.rotation, simpleUI(5, 0.5f), 2, lu + "r");
            if (!camLight) prop("Opacity", &l.opacity, simpleUI(6, 0.5f, 0, 100), 2, lu + "o");
        }
        if (l.threeD && !camLight && group("Material Options", &l.matOpen, 1, 10, lu + "mat")) {
            prop("Ambient", &l.matAmbient, simpleUI(6, 0.5f, 0, 500), 2, lu + "m0");
            prop("Diffuse", &l.matDiffuse, simpleUI(6, 0.5f, 0, 500), 2, lu + "m1");
            prop("Specular", &l.matSpecular, simpleUI(6, 0.5f, 0, 500), 2, lu + "m2");
            prop("Shininess", &l.matShininess, simpleUI(6, 0.5f, 0, 100), 2, lu + "m3");
        }
        bool hasAudio = false;
        if (l.type == LayerType::Footage) if (Footage* f = a.project->findFootage(l.source)) hasAudio = f->hasAudio;
        if (l.type == LayerType::Precomp) hasAudio = true;
        if (hasAudio && group("Audio", &l.audioOpen, 1, 7, lu + "au")) prop("Audio Levels", &l.volume, simpleUI(9, 0.1f, -96, 24), 2, lu + "v");
    }
}

ImU32 barColor(bool sel, bool hov) {
    if (sel) return pal::Accent;
    return hov ? IM_COL32(0x3A, 0x1E, 0x05, 0xFF) : pal::AccentDark;
}

void ruler(App& a, Composition& c, ImVec2 p, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), pal::Bg1);
    dl->PushClipRect(p, p + ImVec2(w, h), true);
    // work area bar
    float wy = p.y + S(2), wh = S(7);
    float wx0 = tToX(c.workStart), wx1 = tToX(c.workEnd);
    dl->AddRectFilled(ImVec2(wx0, wy), ImVec2(wx1, wy + wh), IM_COL32(0x5A, 0x5A, 0x5A, 0xFF), S(2));
    dl->AddRectFilled(ImVec2(wx0, wy), ImVec2(wx0 + S(4), wy + wh), IM_COL32(0x8A, 0x8A, 0x8A, 0xFF));
    dl->AddRectFilled(ImVec2(wx1 - S(4), wy), ImVec2(wx1, wy + wh), IM_COL32(0x8A, 0x8A, 0x8A, 0xFF));
    // ticks
    double fps = c.fps;
    static const double steps[] = {1.0 / 120, 1.0 / 60, 1.0 / 30, 1.0 / 15, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    double minorStep = 1.0 / fps;
    for (double s : steps) if (s * g_pps >= S(8)) { minorStep = std::max(s, 1.0 / fps); break; }
    double majorStep = minorStep;
    for (double s : steps) if (s * g_pps >= S(80) && s >= minorStep) { majorStep = s; break; }
    double t0 = std::floor(xToT(p.x) / minorStep) * minorStep;
    for (double t = std::max(0.0, t0); t <= xToT(p.x + w) && t <= c.duration + 1e-9; t += minorStep) {
        float x = tToX(t);
        bool major = fabs(fmod(t + 1e-7, majorStep)) < 2e-6 || fabs(fmod(t + 1e-7, majorStep) - majorStep) < 2e-6;
        dl->AddLine(ImVec2(x, p.y + h - (major ? S(9) : S(4))), ImVec2(x, p.y + h), major ? pal::TextDim : pal::TextFaint);
        if (major) {
            std::string lbl = a.settings.timeDisplay == 1 ? strf("%lldf", (long long)timeToFrame(t, fps))
                                                          : (majorStep >= 1 ? fmtTimeShort(t).substr(0, fmtTimeShort(t).size() - 3) + "s" : strf("%lldf", (long long)timeToFrame(t, fps)));
            if (majorStep >= 1) lbl = strf("%d:%02d", (int)(t / 60), (int)fmod(t, 60.0));
            dl->AddText(ImVec2(x + S(3), p.y + S(10)), pal::TextDim, lbl.c_str());
        }
    }
    // beyond duration
    float ex = tToX(c.duration);
    if (ex < p.x + w) dl->AddRectFilled(ImVec2(ex, p.y), p + ImVec2(w, h), IM_COL32(0, 0, 0, 90));
    // markers
    for (auto& m : c.markers) {
        float x = tToX(m.t);
        dl->AddTriangleFilled(ImVec2(x - S(4), p.y + h - S(8)), ImVec2(x + S(4), p.y + h - S(8)), ImVec2(x, p.y + h - S(2)), pal::Accent);
    }
    // playhead head
    float px = tToX(c.time);
    dl->AddRectFilled(ImVec2(px - S(5), p.y + h - S(12)), ImVec2(px + S(5), p.y + h - S(4)), pal::Accent, S(1));
    dl->AddTriangleFilled(ImVec2(px - S(5), p.y + h - S(4)), ImVec2(px + S(5), p.y + h - S(4)), ImVec2(px, p.y + h), pal::Accent);
    dl->PopClipRect();

    // interactions
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("ruler", ImVec2(w, h));
    ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        bool onWa = m.y < wy + wh + S(2) && m.x > wx0 - S(4) && m.x < wx1 + S(4);
        if (onWa) {
            ts.waDrag = m.x < wx0 + S(6) ? 1 : (m.x > wx1 - S(6) ? 2 : 3);
            ts.waStart0 = c.workStart;
            ts.waEnd0 = c.workEnd;
            ts.waGrab = xToT(m.x);
        } else ts.scrubbing = true;
        collectSnaps(a, c, SnapPlayhead);
    }
    if (ImGui::IsItemActive()) {
        double tt = xToT(m.x);
        if (ts.waDrag) {
            double d = frameSnap(tt - ts.waGrab, c.fps);
            if (ts.waDrag == 1) c.workStart = clampv(snap(a, tt, c.fps), 0.0, c.workEnd - 1 / c.fps);
            else if (ts.waDrag == 2) c.workEnd = clampv(snap(a, tt, c.fps), c.workStart + 1 / c.fps, c.duration);
            else {
                double len = ts.waEnd0 - ts.waStart0;
                double s0 = clampv(ts.waStart0 + d, 0.0, c.duration - len);
                c.workStart = s0;
                c.workEnd = s0 + len;
            }
            a.markChanged();
        } else if (ts.scrubbing) {
            // the playhead sticks to clip edges, keyframes and markers (hold Alt to scrub freely)
            a.setTime(snap(a, tt, c.fps) + 1e-7);
        }
    }
    if (ImGui::IsItemDeactivated()) { ts.waDrag = 0; ts.scrubbing = false; }
    if (ImGui::IsItemHovered() && m.y < wy + wh + S(2) && m.x > wx0 - S(4) && m.x < wx1 + S(4)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
}

void layerLeft(App& a, Composition& c, Row& r, float x0, float y, float rowH) {
    Layer& l = *r.layer;
    float fh = ImGui::GetFrameHeight();
    float iy = y + (rowH - fh) * 0.5f;
    float sw = S(20);
    ImGui::PushID(r.uid.c_str());
    auto sw_ = [&](int col, const char* id, Icon ic, bool* v, const char* tip, bool show) {
        ImGui::SetCursorScreenPos(ImVec2(x0 + col * sw, iy));
        if (!show) { ImGui::Dummy(ImVec2(fh, fh)); return; }
        if (iconButton(id, ic, *v, tip, fh)) { *v = !*v; a.markChanged(); }
    };
    bool hasVideo = !(l.type == LayerType::Footage && a.project->findFootage(l.source) && !a.project->findFootage(l.source)->hasVideo);
    bool hasAudio = l.type == LayerType::Precomp || (l.type == LayerType::Footage && a.project->findFootage(l.source) && a.project->findFootage(l.source)->hasAudio);
    sw_(0, "vis", Icon::Eye, &l.visible, "Video (show/hide)", hasVideo);
    sw_(1, "aud", Icon::Speaker, &l.audioOn, "Audio (mute)", hasAudio);
    sw_(2, "solo", Icon::Solo, &l.solo, "Solo", true);
    sw_(3, "lock", Icon::Lock, &l.locked, "Lock", true);
    float x = x0 + 4 * sw + S(6);
    // twirl
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    if (iconButton("tw", l.expanded ? Icon::ChevronDown : Icon::ChevronRight, false, nullptr, fh * 0.8f)) l.expanded = !l.expanded;
    x += fh * 0.8f + S(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // index box
    std::string num = std::to_string(r.layerIndex + 1);
    dl->AddRectFilled(ImVec2(x, y + S(4)), ImVec2(x + S(20), y + rowH - S(4)), pal::Bg4, S(2));
    ImVec2 nts = ImGui::CalcTextSize(num.c_str());
    dl->AddText(ImVec2(x + (S(20) - nts.x) * 0.5f, y + (rowH - nts.y) * 0.5f), pal::Text, num.c_str());
    x += S(26);
    Icon ti = Icon::Video;
    switch (l.type) {
    case LayerType::Solid: ti = Icon::Solid; break;
    case LayerType::Text: ti = Icon::Text; break;
    case LayerType::Adjustment: ti = Icon::Adjust; break;
    case LayerType::Null: ti = Icon::Null; break;
    case LayerType::Precomp: ti = Icon::Comp; break;
    case LayerType::Camera: ti = Icon::Camera; break;
    case LayerType::Light: ti = Icon::Light; break;
    default: {
        Footage* f = a.project->findFootage(l.source);
        if (f) ti = f->kind == FootageKind::Image ? Icon::Image : f->kind == FootageKind::Audio ? Icon::Audio : f->kind == FootageKind::Sequence ? Icon::Sequence : Icon::Video;
    }
    }
    bool sel = a.selLayers.count(l.id) > 0;
    drawIcon(dl, ti, ImVec2(x + S(8), y + rowH * 0.5f), S(13), sel ? pal::Accent : pal::TextDim);
    x += S(20);
    // name (with layer drag source for reordering)
    float nameW = ts.leftW - (x - x0) - S(320);
    nameW = std::max(nameW, S(60));
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    if (ts.renaming == l.id) {
        ImGui::SetNextItemWidth(nameW);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##nm", ts.renameBuf, sizeof(ts.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            l.name = ts.renameBuf;
            ts.renaming = 0;
            a.markChanged();
        } else if (ImGui::IsItemDeactivated()) ts.renaming = 0;
    } else {
        ImGui::InvisibleButton("name", ImVec2(nameW, fh));
        if (ImGui::IsItemClicked()) a.selectLayer(l.id, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (l.type == LayerType::Precomp) a.openComp(l.source);
            else { ts.renaming = l.id; snprintf(ts.renameBuf, sizeof(ts.renameBuf), "%s", l.name.c_str()); }
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GESE_LAYER", &l.id, sizeof(l.id));
            ImGui::Text("Move: %s", l.name.c_str());
            ImGui::EndDragDropSource();
        }
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + nameW, y + rowH), true);
        dl->AddText(ImVec2(x + S(2), y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), sel ? pal::White : pal::Text, l.name.c_str());
        dl->PopClipRect();
    }
    x += nameW + S(6);
    // fx / motion blur switches
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    if (iconButton("fx", Icon::Fx, l.fxOn && !l.effects.empty(), "Effects on/off", fh)) { l.fxOn = !l.fxOn; a.markChanged(); }
    ImGui::SetCursorScreenPos(ImVec2(x + sw, iy));
    if (iconButton("mb", Icon::MotionBlur, l.motionBlur, "Motion Blur", fh)) { l.motionBlur = !l.motionBlur; a.markChanged(); }
    bool visual = l.type != LayerType::Null && l.type != LayerType::Adjustment && l.type != LayerType::Camera && l.type != LayerType::Light;
    ImGui::SetCursorScreenPos(ImVec2(x + 2 * sw, iy));
    if (visual || l.type == LayerType::Null) {
        if (iconButton("3d", Icon::Cube, l.threeD, "3D Layer: depth (Z), X / Y rotation, camera and lights", fh)) {
            for (auto* sl : a.selectedLayers()) sl->threeD = !l.threeD;
            l.threeD = !l.threeD;
            if (l.threeD) l.xfOpen = true;
            a.markChanged();
        }
    }
    x += 3 * sw + S(8);
    // mode / track matte / parent
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4), S(1)));
    float cw = S(84);
    ImGui::SetCursorScreenPos(ImVec2(x, iy + S(1)));
    ImGui::SetNextItemWidth(cw);
    if (visual) {
        if (ImGui::BeginCombo("##mode", kBlendNames[l.blend], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < BM_Count; i++) {
                if (i == 2 || i == 6 || i == 10 || i == 17 || i == 21) ImGui::Separator();
                if (ImGui::Selectable(kBlendNames[i], l.blend == i)) { l.blend = i; a.markChanged(); }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(x + cw + S(4), iy + S(1)));
    ImGui::SetNextItemWidth(cw);
    if (r.layerIndex > 0 && visual) {
        static const char* shortM[] = {"None", "Alpha", "Alpha Inv", "Luma", "Luma Inv"};
        if (ImGui::BeginCombo("##matte", shortM[l.matte])) {
            for (int i = 0; i < 5; i++) if (ImGui::Selectable(kMatteNames[i], l.matte == i)) { l.matte = i; a.markChanged(); }
            ImGui::EndCombo();
        }
        tooltip("Track Matte: uses the layer directly above as a matte");
    }
    ImGui::SetCursorScreenPos(ImVec2(x + 2 * (cw + S(4)), iy + S(1)));
    ImGui::SetNextItemWidth(cw);
    Layer* par = l.parent ? c.find(l.parent) : nullptr;
    if (ImGui::BeginCombo("##par", par ? par->name.c_str() : "None")) {
        if (ImGui::Selectable("None", !par)) { l.parent = 0; a.markChanged(); }
        for (auto& o : c.layers) {
            if (o->id == l.id) continue;
            // avoid cycles
            bool cyc = false;
            for (Layer* q = o.get(); q && !cyc; q = q->parent ? c.find(q->parent) : nullptr) if (q->id == l.id) cyc = true;
            if (cyc) continue;
            if (ImGui::Selectable(strf("%d. %s", c.indexOf(o->id) + 1, o->name.c_str()).c_str(), l.parent == o->id)) {
                // keep the world transform when parenting
                double lt = l.localTime(c.time);
                Mat3 world = a.project->layerMatrix(c, l, c.time);
                l.parent = o->id;
                Mat3 pinv = a.project->layerMatrix(c, *o, c.time).inverse();
                Mat3 local = pinv * world;
                ImVec2 an = l.anchor.v2(lt);
                ImVec2 np = local.apply(an);
                float sx = sqrtf(local.m[0] * local.m[0] + local.m[3] * local.m[3]) * 100.f;
                float sy = sqrtf(local.m[1] * local.m[1] + local.m[4] * local.m[4]) * 100.f;
                float rot = atan2f(local.m[3], local.m[0]) * 180.f / kPi;
                if (!l.position.animated) l.position = Prop(2, np.x, np.y);
                if (!l.scale.animated) l.scale = Prop(2, sx, sy);
                if (!l.rotation.animated) l.rotation = Prop(1, rot);
                a.markChanged();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void propLeft(App& a, Composition& c, Row& r, float x0, float y, float rowH) {
    Layer& l = *r.layer;
    float fh = ImGui::GetFrameHeight();
    float iy = y + (rowH - fh) * 0.5f;
    double lt = l.localTime(c.time);
    ImGui::PushID(r.uid.c_str());
    float x = x0 + 4 * S(20) + S(6) + r.depth * S(16);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (r.kind == 1) {
        ImGui::SetCursorScreenPos(ImVec2(x, iy));
        if (iconButton("tw", *r.open ? Icon::ChevronDown : Icon::ChevronRight, false, nullptr, fh * 0.8f)) *r.open = !*r.open;
        x += fh * 0.8f + S(4);
        if (r.fx) {
            ImGui::SetCursorScreenPos(ImVec2(x, iy));
            if (iconButton("en", Icon::Fx, r.fx->enabled, "Effect on/off", fh)) { r.fx->enabled = !r.fx->enabled; a.markChanged(); }
            x += fh + S(4);
        }
        dl->AddText(ImVec2(x, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), pal::Text, T(r.label.c_str()));
        if (r.fx) {
            ImGui::SetCursorScreenPos(ImVec2(x, iy));
            ImGui::InvisibleButton("sel", ImVec2(ImGui::CalcTextSize(r.label.c_str()).x, fh));
            if (ImGui::IsItemClicked()) { a.selEffect = r.fx->id; a.selLayers = {l.id}; }
        }
        if (r.mask) {
            float mx = x + ImGui::CalcTextSize(r.label.c_str()).x + S(14);
            ImGui::SetCursorScreenPos(ImVec2(mx, iy + S(1)));
            static const char* modes[] = {"Add", "Subtract", "Intersect"};
            ImGui::SetNextItemWidth(S(84));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4), S(1)));
            if (ImGui::Combo("##mm", &r.mask->mode, modes, 3)) a.markChanged();
            ImGui::SameLine();
            if (ImGui::Checkbox("Inverted", &r.mask->inverted)) a.markChanged();
            ImGui::SameLine();
            if (iconButton("del", Icon::Trash, false, "Delete mask", fh)) {
                uint64_t mid = r.mask->id;
                l.masks.erase(std::remove_if(l.masks.begin(), l.masks.end(), [&](const Mask& m) { return m.id == mid; }), l.masks.end());
                a.markChanged();
                ImGui::PopStyleVar();
                ImGui::PopID();
                return;
            }
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
        return;
    }
    // property row: stopwatch, label, value
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    if (stopwatch("sw", *r.prop, lt)) a.markChanged();
    x += fh + S(4);
    dl->AddText(ImVec2(x, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), pal::TextDim, T(r.label.c_str()));
    float vx = x0 + std::max(S(260), ts.leftW * 0.48f);
    ImGui::SetCursorScreenPos(ImVec2(vx - fh * 2.6f, iy));
    if (keyNav("kn", *r.prop, lt, l.startTime, c.fps)) a.markChanged();
    ImGui::SetCursorScreenPos(ImVec2(vx, iy));
    if (r.ui.kind == 8 && r.prop == &l.scale) {
        // linked scale
        float before[4];
        r.prop->eval(lt, before);
        if (propValue("v", *r.prop, lt, r.ui, S(120)) && ts.linkScale) {
            float now[4];
            r.prop->eval(lt, now);
            if (fabsf(now[0] - before[0]) > 1e-5f && fabsf(before[0]) > 1e-5f) now[1] = before[1] * now[0] / before[0];
            else if (fabsf(now[1] - before[1]) > 1e-5f && fabsf(before[1]) > 1e-5f) now[0] = before[0] * now[1] / before[1];
            r.prop->set(lt, now);
        }
        ImGui::SameLine(0, S(6));
        iconToggle("link", Icon::Link, &ts.linkScale, "Constrain proportions");
    } else {
        propValue("v", *r.prop, lt, r.ui, S(120));
    }
    ImGui::PopID();
}

void layerRight(App& a, Composition& c, Row& r, float y, float rowH, float rx1) {
    Layer& l = *r.layer;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushID(r.uid.c_str());
    float x0 = tToX(l.inPoint), x1 = tToX(l.outPoint);
    float by0 = y + S(2), by1 = y + rowH - S(2);
    bool sel = a.selLayers.count(l.id) > 0;
    ImGui::SetCursorScreenPos(ImVec2(g_rx0, y));
    ImGui::InvisibleButton("bar", ImVec2(std::max(1.f, rx1 - g_rx0), rowH));
    bool hov = ImGui::IsItemHovered();
    ImVec2 m = io.MousePos;
    bool onBar = m.x >= x0 && m.x <= x1;
    float edge = S(6);
    int zone = !onBar ? 0 : (m.x < x0 + edge ? 2 : (m.x > x1 - edge ? 3 : 1));
    // draw bar
    dl->AddRectFilled(ImVec2(x0, by0), ImVec2(x1, by1), barColor(sel, hov && onBar), S(2));
    dl->AddRect(ImVec2(x0, by0), ImVec2(x1, by1), sel ? IM_COL32(0xFF, 0xA0, 0x50, 0xFF) : pal::AccentA(150), S(2));
    dl->PushClipRect(ImVec2(std::max(x0, g_rx0), by0), ImVec2(std::min(x1, rx1), by1), true);
    float tx = std::max(x0, g_rx0) + S(5);
    Footage* f = l.type == LayerType::Footage ? a.project->findFootage(l.source) : nullptr;
    if (f && f->thumb && f->hasVideo) {
        float th = by1 - by0 - S(2);
        float tw = th * f->thumb->w / std::max(1, f->thumb->h);
        dl->AddImage(f->thumb->id(), ImVec2(tx - S(4), by0 + S(1)), ImVec2(tx - S(4) + tw, by0 + S(1) + th));
        tx += tw;
    }
    if (f && f->hasAudio && f->audio && f->audio->ready && !f->audio->peaks.empty()) {
        // waveform
        const auto& pk = f->audio->peaks;
        float mid = (by0 + by1) * 0.5f, amp = (by1 - by0) * 0.45f;
        for (float x = std::max(x0, g_rx0); x < std::min(x1, rx1); x += 2) {
            double st = l.sourceTime(xToT(x));
            int i = (int)(st * AudioData::kRate / AudioData::kPeakBlock);
            if (i < 0 || i >= (int)pk.size()) continue;
            float v = std::min(1.f, pk[i]) * amp;
            dl->AddLine(ImVec2(x, mid - v), ImVec2(x, mid + v), sel ? IM_COL32(0x40, 0x18, 0x00, 0xC0) : pal::AccentA(170));
        }
    }
    // keyframes of every property sit in their own lane at the bottom of the clip, the name stays above them
    std::vector<double> kts = layerKeyTimes(a, l);
    float laneH = kts.empty() ? 0.f : S(13);
    float textY = by0 + (by1 - laneH - by0 - ImGui::GetTextLineHeight()) * 0.5f;
    if (l.type == LayerType::Adjustment) drawIcon(dl, Icon::Fx, ImVec2(tx + S(6), textY + ImGui::GetTextLineHeight() * 0.5f), S(14), sel ? pal::White : pal::Accent), tx += S(16);
    if (l.type == LayerType::Camera || l.type == LayerType::Light)
        drawIcon(dl, l.type == LayerType::Camera ? Icon::Camera : Icon::Light, ImVec2(tx + S(6), textY + ImGui::GetTextLineHeight() * 0.5f), S(13), sel ? pal::White : pal::Accent), tx += S(16);
    dl->AddText(ImVec2(tx + S(2), textY), sel ? pal::White : pal::Text, l.name.c_str());
    if (!l.audioOn && f && f->hasAudio) drawIcon(dl, Icon::Speaker, ImVec2(std::min(x1, rx1) - S(10), textY + ImGui::GetTextLineHeight() * 0.5f), S(11), IM_COL32(0xFF, 0xFF, 0xFF, 0x70));
    float ky = by1 - laneH * 0.5f, kr = S(5.5f);
    int hoverKey = -1;
    if (!kts.empty()) {
        dl->AddRectFilled(ImVec2(x0, by1 - laneH), ImVec2(x1, by1), IM_COL32(0, 0, 0, sel ? 0x50 : 0x70), S(2), ImDrawFlags_RoundCornersBottom);
        for (int i = 0; i < (int)kts.size(); i++) {
            float kx = tToX(l.startTime + kts[i]);
            if (hov && kx >= x0 - 1 && kx <= x1 + 1 && fabsf(m.x - kx) <= kr + S(3) && fabsf(m.y - ky) <= laneH * 0.5f + S(3)) hoverKey = i;
        }
    }
    for (int i = 0; i < (int)kts.size(); i++) {
        ImVec2 kc(tToX(l.startTime + kts[i]), ky);
        bool ks = keysAtSelected(a, l, kts[i]);
        float r = std::min(laneH * 0.5f - S(0.5f), ks || hoverKey == i ? kr * 1.15f : kr);
        ImU32 fill = ks ? pal::White : (hoverKey == i ? IM_COL32(0xEE, 0xEE, 0xEE, 0xFF) : IM_COL32(0xB4, 0xB4, 0xB4, 0xFF));
        dl->AddQuadFilled(kc + ImVec2(0, -r), kc + ImVec2(r, 0), kc + ImVec2(0, r), kc + ImVec2(-r, 0), fill);
        dl->AddQuad(kc + ImVec2(0, -r), kc + ImVec2(r, 0), kc + ImVec2(0, r), kc + ImVec2(-r, 0), ks ? pal::Accent : IM_COL32(0x10, 0x10, 0x10, 0xFF), S(1.4f));
    }
    dl->PopClipRect();
    if (l.reversed) drawIcon(dl, Icon::KeyPrev, ImVec2(x1 - S(8), (by0 + by1) * 0.5f), S(10), pal::White);
    if (hoverKey >= 0) zone = 4;
    if (hoverKey >= 0 && !ImGui::IsMouseDown(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        std::string names;
        for (auto& pr : a.layerProps(l))
            if (pr.second->animated && pr.second->keyAt(kts[hoverKey]) >= 0) names += (names.empty() ? "" : ", ") + propName(a, l, pr.first);
        ImGui::SetTooltip("%s\n%s", names.c_str(), T("Drag to move · right-click: keyframe parameters and Graph Editor"));
    }

    // interactions
    if (hov && (zone == 2 || zone == 3) && !l.locked) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActivated() && onBar && hoverKey >= 0 && a.tool != Tool::Razor) {
        // grab the keyframes under the cursor (all properties at that time)
        if (!sel) a.selectLayer(l.id, io.KeyCtrl, false);
        bool wasSel = keysAtSelected(a, l, kts[hoverKey]);
        if (!io.KeyShift && !wasSel) a.clearKeySelection();
        selectKeysAt(a, l, kts[hoverKey], !(io.KeyShift && wasSel));
        ts.barMode = 0;
        beginKeyDrag(a, c, l.startTime + kts[hoverKey]);
    } else if (ImGui::IsItemActivated() && onBar) {
        if (a.tool == Tool::Razor) {
            a.selLayers = {l.id};
            a.splitSelectedLayers(xToT(m.x));
        } else {
            if (!sel) a.selectLayer(l.id, io.KeyCtrl || io.KeyShift, false);
            else if (io.KeyCtrl) a.selLayers.erase(l.id);
            if (!l.locked) {
                ts.barMode = zone;
                ts.barLayer = l.id;
                ts.grabT = xToT(m.x);
                ts.barStart.clear();
                for (auto* sl : a.selectedLayers())
                    if (!sl->locked) ts.barStart.push_back({sl->id, sl->startTime, sl->inPoint, sl->outPoint});
                collectSnaps(a, c, SnapClips);
            }
        }
    } else if (ImGui::IsItemActivated() && !onBar) {
        startMarquee(a);
    }
    ts.barRects.push_back({l.id, ImRect(ImVec2(x0, by0), ImVec2(x1, by1))});
    if (hov && ImGui::IsMouseDoubleClicked(0) && hoverKey >= 0) a.setTime(l.startTime + kts[hoverKey]);
    else if (hov && ImGui::IsMouseDoubleClicked(0) && onBar && l.type == LayerType::Precomp) a.openComp(l.source);
    if (ImGui::IsItemActive() && ts.keyDrag && ImGui::IsMouseDragging(0, 1.f)) moveKeys(a, c);
    if (ImGui::IsItemActive() && ts.barMode && ts.barLayer == l.id && ImGui::IsMouseDragging(0, 1.f)) {
        double cur = xToT(m.x);
        double d = cur - ts.grabT;
        // move: one offset for the whole selection, so clips keep their spacing; the clip edge nearest
        // to a magnet target (other clip, playhead, marker, comp start/end) sticks to it
        double off = frameRound(d, c.fps);
        if (ts.barMode == 1) {
            if (magnetOn(a)) {
                double dist = snapRadius(), best;
                for (auto& b : ts.barStart)
                    for (double e : {std::get<2>(b), std::get<3>(b)})
                        if (nearestSnap(e + d, best, dist)) { off = best - e; ts.snapLine = best; }
            }
            double minIn = 1e18;
            for (auto& b : ts.barStart) minIn = std::min(minIn, std::get<2>(b));
            if (minIn + off < 0) { off = -minIn; ts.snapLine = 0; ts.snapHit = {0, 0, 0}; }  // clips can't be dragged before the start
        }
        for (auto& [id, st, in, out] : ts.barStart) {
            Layer* bl = c.find(id);
            if (!bl) continue;
            if (ts.barMode == 1) {
                bl->startTime = st + off;
                bl->inPoint = in + off;
                bl->outPoint = out + off;
            } else if (ts.barMode == 2) {
                double ni = snap(a, in + d, c.fps);
                if (bl->type == LayerType::Footage) {
                    Footage* bf = a.project->findFootage(bl->source);
                    if (bf && bf->kind != FootageKind::Image && !bl->reversed) ni = std::max(ni, st);  // can't trim before source start
                }
                bl->inPoint = std::min(ni, out - 1 / c.fps);
            } else if (ts.barMode == 3) {
                double no = snap(a, out + d, c.fps);
                if (bl->type == LayerType::Footage) {
                    Footage* bf = a.project->findFootage(bl->source);
                    if (bf && bf->kind != FootageKind::Image && bf->duration > 0 && !bl->reversed) no = std::min(no, st + bf->duration / std::max(0.01, bl->speed));
                }
                bl->outPoint = std::max(no, in + 1 / c.fps);
            }
        }
        a.markChanged();
    }
    if (ImGui::IsItemDeactivated()) {
        ts.barMode = 0;
        if (ts.keyDrag) endKeyDrag(a, c);
    }
    if (hov && onBar && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (hoverKey >= 0) {
            if (!keysAtSelected(a, l, kts[hoverKey])) { a.clearKeySelection(); selectKeysAt(a, l, kts[hoverKey], true); }
            ts.kpT = kts[hoverKey];
            ts.kpProp = nullptr;
            ImGui::OpenPopup("keyp");
        } else {
            ImGui::OpenPopup("barctx");
        }
    }
    if (ImGui::BeginPopup("keyp")) {
        keyPopup(a, c, l);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("barctx")) {
        if (!a.selLayers.count(l.id)) a.selLayers = {l.id};
        if (ImGui::MenuItem("Split at Playhead", "Ctrl+Shift+D")) a.splitSelectedLayers(c.time);
        if ((l.type == LayerType::Footage || l.type == LayerType::Precomp) && ImGui::MenuItem("Freeze Frame", "Shift+F")) a.freezeFrame(c.time);
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) a.duplicateSelectedLayers();
        if (ImGui::MenuItem("Pre-compose / Compound Clip...", "Ctrl+Shift+C")) a.dlgPrecompose = true;
        if (l.type == LayerType::Precomp && ImGui::MenuItem("Unpack Compound Clip", "Ctrl+Alt+Shift+C")) a.pendingUnpack = l.id;  // after this frame's rows
        if (ImGui::MenuItem("Speed / Duration...", "Ctrl+R")) a.dlgSpeed = true;
        if (ImGui::MenuItem("Time-Reverse", "Ctrl+Alt+R", l.reversed)) { for (auto* sl : a.selectedLayers()) sl->reversed = !sl->reversed; a.markChanged(); }
        if (l.type != LayerType::Null && l.type != LayerType::Adjustment) {
            int fm = a.fadeMode(l);
            if (ImGui::MenuItem("Fade In", nullptr, fm == 1 || fm == 3)) for (auto* sl : a.selectedLayers()) a.toggleFade(*sl, false);
            if (ImGui::MenuItem("Fade Out", nullptr, fm == 2 || fm == 3)) for (auto* sl : a.selectedLayers()) a.toggleFade(*sl, true);
        }
        if (l.type == LayerType::Footage && ImGui::MenuItem("Remove Background (AI)...")) a.dlgAi = true;
        bool layerAudio = l.type == LayerType::Precomp || (f && f->hasAudio);
        if (layerAudio) {
            ImGui::Separator();
            if (ImGui::MenuItem(l.audioOn ? "Mute Clip Audio" : "Unmute Clip Audio")) {
                bool on = !l.audioOn;
                for (auto* sl : a.selectedLayers()) sl->audioOn = on;
                a.markChanged();
            }
            if (f && f->hasVideo && ImGui::MenuItem("Extract Audio to New Layer")) a.extractAudio(l);
            if (f && ImGui::MenuItem("Save Clip Audio As...")) a.saveLayerAudio(l);
            if (ImGui::MenuItem("Voice & Speed...")) { a.pFxControls = true; l.audioOpen = true; a.focusPanel = "###Effect Controls"; }
        }
        if (!kts.empty() && ImGui::MenuItem("Graph Editor", nullptr, a.pGraph)) a.pGraph = !a.pGraph;
        ImGui::Separator();
        if (ImGui::MenuItem("Bring Forward", "Ctrl+]")) a.moveSelectedLayers(-1, false);
        if (ImGui::MenuItem("Send Backward", "Ctrl+[")) a.moveSelectedLayers(1, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) a.deleteSelectedLayers(false);
        if (ImGui::MenuItem("Ripple Delete", "Shift+Delete")) a.deleteSelectedLayers(true);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void propRight(App& a, Composition& c, Row& r, float y, float rowH, float rx1) {
    if (r.kind != 2 || !r.prop || !r.prop->animated) return;
    Layer& l = *r.layer;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushID(r.uid.c_str());
    float cy = y + rowH * 0.5f, rad = S(5.5f);
    // value graph-free view: keyframes only
    int hoverKey = -1;
    for (int i = 0; i < (int)r.prop->keys.size(); i++) {
        float x = tToX(l.startTime + r.prop->keys[i].t);
        if (fabsf(io.MousePos.x - x) <= rad + 1 && fabsf(io.MousePos.y - cy) <= rad + 1) hoverKey = i;
    }
    ImGui::SetCursorScreenPos(ImVec2(g_rx0, y));
    ImGui::InvisibleButton("keys", ImVec2(std::max(1.f, rx1 - g_rx0), rowH));
    bool hov = ImGui::IsItemHovered();
    for (int i = 0; i < (int)r.prop->keys.size(); i++) {
        float x = tToX(l.startTime + r.prop->keys[i].t);
        if (x < g_rx0 - rad || x > rx1 + rad) continue;
        drawKey(dl, ImVec2(x, cy), rad, r.prop->keys[i], hov && hoverKey == i);
    }
    if (ImGui::IsItemActivated()) {
        if (hoverKey >= 0) {
            Keyframe& k = r.prop->keys[hoverKey];
            if (!k.sel && !io.KeyShift) a.clearKeySelection();
            if (io.KeyShift && k.sel) k.sel = false;
            else k.sel = true;
            beginKeyDrag(a, c, l.startTime + k.t);
        } else {
            startMarquee(a);
        }
    }
    for (auto& k : r.prop->keys) ts.keyPts.push_back({&k, ImVec2(tToX(l.startTime + k.t), cy)});
    if (hov && hoverKey >= 0 && ImGui::IsMouseDoubleClicked(0)) a.setTime(l.startTime + r.prop->keys[hoverKey].t);
    if (ImGui::IsItemActive() && ts.keyDrag && ImGui::IsMouseDragging(0, 1.f)) moveKeys(a, c);
    if (ImGui::IsItemDeactivated() && ts.keyDrag) endKeyDrag(a, c);
    if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (hoverKey >= 0) {
            if (!r.prop->keys[hoverKey].sel) { a.clearKeySelection(); r.prop->keys[hoverKey].sel = true; }
            ts.kpT = r.prop->keys[hoverKey].t;
            ts.kpProp = r.prop;
            ImGui::OpenPopup("keyp");
        } else {
            ImGui::OpenPopup("kctx");
        }
    }
    if (ImGui::BeginPopup("keyp")) {
        keyPopup(a, c, l);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("kctx")) {
        bool any = a.anyKeySelected();
        if (ImGui::MenuItem("Linear", nullptr, false, any)) a.setSelectedKeysInterp(0);
        if (ImGui::MenuItem("Easy Ease", "F9", false, any)) a.setSelectedKeysInterp(1);
        if (ImGui::MenuItem("Easy Ease In", "Shift+F9", false, any)) a.setSelectedKeysInterp(2);
        if (ImGui::MenuItem("Easy Ease Out", "Ctrl+Shift+F9", false, any)) a.setSelectedKeysInterp(3);
        if (ImGui::MenuItem("Toggle Hold Keyframe", "Ctrl+Alt+H", false, any)) a.setSelectedKeysInterp(4);
        ImGui::Separator();
        if (ImGui::MenuItem("Add Keyframe at Playhead")) {
            double lt = l.localTime(c.time);
            float v[4];
            r.prop->eval(lt, v);
            r.prop->addKey(lt, v);
            a.markChanged();
        }
        if (ImGui::MenuItem("Delete Keyframes", "Delete", false, any)) a.deleteSelectedKeys();
        ImGui::Separator();
        if (ImGui::MenuItem("Graph Editor", nullptr, a.pGraph)) a.pGraph = !a.pGraph;
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
}  // namespace

void panelTimeline(App& a) {
    Composition* c = a.comp();
    std::string title = c ? c->name : std::string("Timeline");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin((std::string(T("Timeline")) + ": " + title + "###Timeline").c_str(), &a.pTimeline, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open || !c) {
        if (open) {
            ImGui::SetCursorPos(ImVec2(S(12), S(12)));
            ImGui::TextDisabled("No composition is open.");
        }
        ImGui::End();
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (ts.leftW <= 0) ts.leftW = S(560);
    ts.snapLine = -1;
    ImVec2 wp = ImGui::GetCursorScreenPos();
    ImVec2 wsz = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float propRowH = S(24), layerRowH = S(32), rowH = propRowH;
    float headH = S(36), rulerH = S(30);

    // ---- header: timecode + search + switches + zoom
    dl->AddRectFilled(wp, wp + ImVec2(wsz.x, headH), pal::Bg2);
    ImGui::SetCursorScreenPos(wp + ImVec2(S(10), S(4)));
    double tt = c->time;
    if (timecodeField("##tc", &tt, c->fps, pal::Accent, 1.65f)) a.setTime(tt);
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
    ImGui::TextDisabled("%05lld (%g fps)", (long long)timeToFrame(c->time, c->fps), c->fps);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked()) a.dlgCompSettings = true;
    tooltip("Composition Settings (Ctrl+K): frame rate, size, duration");
    ImGui::SetCursorScreenPos(wp + ImVec2(ts.leftW * 0.5f, (headH - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(S(150));
    ImGui::InputTextWithHint("##tls", T("Search layers"), ts.search, sizeof(ts.search));
    ImGui::SameLine(0, S(8));
    iconToggle("shy", Icon::Shy, &ts.hideShy, "Hide shy layers");
    ImGui::SameLine(0, S(2));
    bool mb = c->motionBlur;
    if (iconToggle("mbm", Icon::MotionBlur, &mb, "Motion Blur master switch")) { c->motionBlur = mb; a.markChanged(); }
    ImGui::SameLine(0, S(2));
    iconToggle("snp", Icon::Snap, &a.settings.snapping, "Snapping (hold Alt to bypass)");
    ImGui::SameLine(0, S(2));
    if (iconButton("razor", Icon::Razor, a.tool == Tool::Razor, "Blade tool (C)")) a.tool = a.tool == Tool::Razor ? Tool::Select : Tool::Razor;
    ImGui::SameLine(0, S(2));
    if (iconButton("freeze", Icon::Freeze, false, "Freeze Frame (Shift+F): holds the frame under the playhead")) a.freezeFrame(c->time);
    ImGui::SameLine(0, S(2));
    if (iconButton("mkr", Icon::Key, false, "Add marker at playhead (*)")) { c->markers.push_back({c->time, ""}); a.markChanged(); }
    ImGui::SameLine(0, S(2));
    if (iconButton("graph", Icon::Graph, a.pGraph, "Graph Editor: animation curves between keyframes")) a.pGraph = !a.pGraph;
    // zoom slider on the right
    float zoomW = S(150);
    ImGui::SetCursorScreenPos(ImVec2(wp.x + wsz.x - zoomW - S(40), wp.y + (headH - ImGui::GetFrameHeight()) * 0.5f));
    float fitPps = (wsz.x - ts.leftW - S(20)) / std::max(0.5, c->duration);
    if (c->tlZoom <= 0) c->tlZoom = fitPps;
    float lz = log2f(c->tlZoom);
    ImGui::SetNextItemWidth(zoomW);
    if (ImGui::SliderFloat("##tz", &lz, log2f(fitPps * 0.5f), log2f(std::max(fitPps * 2, (float)(c->fps * S(30)))), "")) c->tlZoom = powf(2, lz);
    tooltip("Timeline zoom (Alt + mouse wheel)");
    ImGui::SameLine(0, S(4));
    if (iconButton("fit", Icon::Safe, false, "Zoom to fit")) { c->tlZoom = fitPps; c->tlScroll = 0; }
    g_pps = c->tlZoom;

    // ---- ruler row
    float bodyY = wp.y + headH + rulerH;
    g_rx0 = wp.x + ts.leftW + S(6);
    g_rw = wsz.x - ts.leftW - S(6) - S(12);
    float rx1 = g_rx0 + g_rw;
    double visible = g_rw / g_pps;
    c->tlScroll = clampv(c->tlScroll, 0.0, std::max(0.0, c->duration - visible * 0.5));
    g_scroll = c->tlScroll;
    // follow the playhead while playing
    if (a.playing && (c->time > g_scroll + visible * 0.95 || c->time < g_scroll)) { c->tlScroll = std::max(0.0, c->time - visible * 0.1); g_scroll = c->tlScroll; }
    dl->AddRectFilled(ImVec2(wp.x, wp.y + headH), ImVec2(wp.x + ts.leftW, bodyY), pal::Bg1);
    {
        // column headers
        float x = wp.x;
        float hy = wp.y + headH + (rulerH - S(14)) * 0.5f;
        drawIcon(dl, Icon::Eye, ImVec2(x + S(10), hy + S(7)), S(12), pal::TextDim);
        drawIcon(dl, Icon::Speaker, ImVec2(x + S(30), hy + S(7)), S(12), pal::TextDim);
        drawIcon(dl, Icon::Solo, ImVec2(x + S(50), hy + S(7)), S(12), pal::TextDim);
        drawIcon(dl, Icon::Lock, ImVec2(x + S(70), hy + S(7)), S(12), pal::TextDim);
        float tY = wp.y + headH + (rulerH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(x + S(110), tY), pal::TextDim, "#");
        dl->AddText(ImVec2(x + S(150), tY), pal::TextDim, T("Layer Name"));
        float cx = x + ts.leftW - S(300) + S(6) + S(40) + S(8) + S(4);
        dl->AddText(ImVec2(cx, tY), pal::TextDim, T("Mode"));
        dl->AddText(ImVec2(cx + S(88), tY), pal::TextDim, T("TrkMat"));
        dl->AddText(ImVec2(cx + S(176), tY), pal::TextDim, T("Parent & Link"));
    }
    ImGui::PushID("ruler");
    ruler(a, *c, ImVec2(g_rx0, wp.y + headH), g_rw, rulerH);
    ImGui::PopID();
    dl->AddLine(ImVec2(wp.x, bodyY), ImVec2(wp.x + wsz.x, bodyY), pal::Line);

    // splitter
    ImGui::SetCursorScreenPos(ImVec2(wp.x + ts.leftW, wp.y + headH));
    ImGui::InvisibleButton("split", ImVec2(S(6), wsz.y - headH));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) ts.leftW = clampv(ts.leftW + io.MouseDelta.x, S(380), wsz.x - S(200));

    // ---- body
    std::vector<Row> rows;
    buildRows(a, *c, rows);
    ts.barRects.clear();
    ts.keyPts.clear();
    ImGui::SetCursorScreenPos(ImVec2(wp.x, bodyY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("tlbody", ImVec2(wsz.x, wsz.y - headH - rulerH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* bdl = ImGui::GetWindowDrawList();
    ImVec2 bp = ImGui::GetCursorScreenPos();
    float bodyH = ImGui::GetWindowHeight();
    float y = bp.y;
    for (size_t i = 0; i < rows.size(); i++) {
        Row& r = rows[i];
        float rowH = r.kind == 0 ? layerRowH : propRowH;  // clip rows are taller: name on top, keyframe lane below
        bool selRow = r.kind == 0 && a.selLayers.count(r.layer->id);
        ImU32 bg = r.kind == 0 ? (selRow ? IM_COL32(0x33, 0x2A, 0x22, 0xFF) : pal::Bg3) : pal::Bg2;
        bdl->AddRectFilled(ImVec2(wp.x, y), ImVec2(wp.x + ts.leftW, y + rowH), bg);
        if (r.kind == 0 && selRow) bdl->AddRectFilled(ImVec2(wp.x, y), ImVec2(wp.x + S(2), y + rowH), pal::Accent);
        bdl->AddRectFilled(ImVec2(g_rx0, y), ImVec2(rx1, y + rowH), r.kind == 0 ? pal::Bg1 : IM_COL32(0x1A, 0x1A, 0x1A, 0xFF));
        // shade outside layer in/out on property rows
        bdl->AddLine(ImVec2(wp.x, y + rowH - 1), ImVec2(wp.x + wsz.x, y + rowH - 1), pal::Line);
        bool visibleRow = y + rowH >= bp.y + ImGui::GetScrollY() - rowH && y <= bp.y + ImGui::GetScrollY() + bodyH + rowH;
        if (visibleRow) {
            bdl->PushClipRect(ImVec2(wp.x, y), ImVec2(wp.x + ts.leftW, y + rowH), true);
            if (r.kind == 0) layerLeft(a, *c, r, wp.x, y, rowH);
            else propLeft(a, *c, r, wp.x, y, rowH);
            bdl->PopClipRect();
            bdl->PushClipRect(ImVec2(g_rx0, y), ImVec2(rx1, y + rowH), true);
            if (r.kind == 0) layerRight(a, *c, r, y, rowH, rx1);
            else if (r.kind == 2) propRight(a, *c, r, y, rowH, rx1);
            else {
                // group rows: dim the area outside the layer
                float x0 = tToX(r.layer->inPoint), x1 = tToX(r.layer->outPoint);
                bdl->AddRectFilled(ImVec2(x0, y + S(9)), ImVec2(x1, y + rowH - S(9)), IM_COL32(0xFF, 0xFF, 0xFF, 0x10));
            }
            bdl->PopClipRect();
        }
        // drop targets per layer row: reorder / footage / effects
        if (r.kind == 0) {
            ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
            ImGui::Dummy(ImVec2(ts.leftW, rowH));
            ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
            ImGui::InvisibleButton(("drop" + r.uid).c_str(), ImVec2(1, rowH));
            ImRect rr(ImVec2(wp.x, y), ImVec2(rx1, y + rowH));
            if (ImGui::BeginDragDropTargetCustom(rr, ImGui::GetID(("dt" + r.uid).c_str()))) {
                if (auto* pl = ImGui::AcceptDragDropPayload("GESE_LAYER")) {
                    uint64_t id = *(const uint64_t*)pl->Data;
                    int from = c->indexOf(id), to = c->indexOf(r.layer->id);
                    if (from >= 0 && to >= 0 && from != to) {
                        LayerPtr lp = c->layers[from];
                        c->layers.erase(c->layers.begin() + from);
                        c->layers.insert(c->layers.begin() + to, lp);
                        a.markChanged();
                    }
                }
                if (auto* pl = ImGui::AcceptDragDropPayload(DND_EFFECT)) {
                    int idx = *(const int*)pl->Data;
                    if (idx >= 0 && idx < (int)a.effects.defs.size()) a.applyEffect(a.effects.defs[idx].get(), r.layer);
                }
                if (auto* pl = ImGui::AcceptDragDropPayload(DND_PRESET)) {
                    int idx = *(const int*)pl->Data;
                    auto files = a.presetFiles();
                    if (idx >= 0 && idx < (int)files.size()) a.applyPreset(files[idx], r.layer);
                }
                if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
                    uint64_t id = *(const uint64_t*)pl->Data;
                    double at = io.MousePos.x > g_rx0 ? frameSnap(std::max(0.0, xToT(io.MousePos.x)), c->fps) : c->time;
                    int to = c->indexOf(r.layer->id);
                    if (Footage* f = a.project->findFootage(id)) a.addFootageLayer(*c, *f, at, to);
                    else if (Composition* sc = a.project->findComp(id)) a.addCompLayer(*c, *sc, at, to);
                }
                ImGui::EndDragDropTarget();
            }
        }
        y += rowH;
        ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
    }
    // empty area below the layers: drop target that creates new layers / effect layers
    float restH = std::max(rowH * 2, bp.y + bodyH - y);
    ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
    ImGui::InvisibleButton("empty", ImVec2(wsz.x - S(14), restH));
    if (ImGui::IsItemActivated()) startMarquee(a);
    if (ImGui::BeginDragDropTarget()) {
        double at = io.MousePos.x > g_rx0 ? frameSnap(std::max(0.0, xToT(io.MousePos.x)), c->fps) : c->time;
        int toIdx = (int)c->layers.size();
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_EFFECT)) {
            int idx = *(const int*)pl->Data;
            if (idx >= 0 && idx < (int)a.effects.defs.size()) a.addEffectLayer(*c, a.effects.defs[idx].get(), at, 0);
        }
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_PRESET)) {
            int idx = *(const int*)pl->Data;
            auto files = a.presetFiles();
            if (idx >= 0 && idx < (int)files.size()) {
                Layer* el = a.addEffectLayer(*c, nullptr, at, 0);
                a.applyPreset(files[idx], el);
            }
        }
        if (auto* pl = ImGui::AcceptDragDropPayload(DND_ITEM)) {
            uint64_t id = *(const uint64_t*)pl->Data;
            if (Footage* f = a.project->findFootage(id)) a.addFootageLayer(*c, *f, at, toIdx);
            else if (Composition* sc = a.project->findComp(id)) a.addCompLayer(*c, *sc, at, toIdx);
        }
        if (auto* pl = ImGui::AcceptDragDropPayload("GESE_LAYER")) {
            uint64_t id = *(const uint64_t*)pl->Data;
            int from = c->indexOf(id);
            if (from >= 0) {
                LayerPtr lp = c->layers[from];
                c->layers.erase(c->layers.begin() + from);
                c->layers.push_back(lp);
                a.markChanged();
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (rows.empty()) {
        const char* hint = "Drag footage here from the Project panel, or drag an effect here to create an Effect Layer";
        ImVec2 hs = ImGui::CalcTextSize(hint);
        bdl->AddText(ImVec2(g_rx0 + (g_rw - hs.x) * 0.5f, bp.y + S(30)), pal::TextFaint, hint);
    }
    // rubber-band selection: the start point stays on the content while the wheel (or the edge) scrolls
    if (ts.marquee) {
        if (!ImGui::IsMouseDown(0)) ts.marquee = false;
        else {
            ImVec2 m = io.MousePos;
            float top0 = bp.y + ImGui::GetScrollY(), bot0 = top0 + bodyH;
            if (m.y < top0 + S(4)) ImGui::SetScrollY(ImGui::GetScrollY() - S(14));
            else if (m.y > bot0 - S(4)) ImGui::SetScrollY(ImGui::GetScrollY() + S(14));
            ImVec2 st(ts.mqStartX >= g_rx0 ? tToX(ts.mqStartT) : ts.mqStartX, bp.y + ts.mqStartY);
            ImRect mr(ImVec2(std::min(st.x, m.x), std::min(st.y, m.y)), ImVec2(std::max(st.x, m.x), std::max(st.y, m.y)));
            if (mr.GetWidth() > 3 || mr.GetHeight() > 3) {
                if (!ts.marqueeAdd) { a.selLayers.clear(); }
                for (auto& br : ts.barRects)
                    if (br.second.Overlaps(mr)) a.selLayers.insert(br.first);
                for (auto& kp : ts.keyPts) {
                    bool in = mr.Contains(kp.second);
                    if (in) kp.first->sel = true;
                    else if (!ts.marqueeAdd) kp.first->sel = false;
                }
                ImDrawList* fdl = ImGui::GetWindowDrawList();
                fdl->AddRectFilled(mr.Min, mr.Max, pal::AccentA(40));
                fdl->AddRect(mr.Min, mr.Max, pal::Accent);
            }
        }
    }
    // playhead & work area shading over the body
    float px = tToX(c->time);
    float top = bp.y + ImGui::GetScrollY(), bot = top + bodyH;
    bdl->PushClipRect(ImVec2(g_rx0, top), ImVec2(rx1, bot), true);
    float wx0 = tToX(c->workStart), wx1 = tToX(c->workEnd);
    bdl->AddRectFilled(ImVec2(g_rx0, top), ImVec2(wx0, bot), IM_COL32(0, 0, 0, 50));
    bdl->AddRectFilled(ImVec2(wx1, top), ImVec2(rx1, bot), IM_COL32(0, 0, 0, 50));
    for (auto& mk : c->markers) bdl->AddLine(ImVec2(tToX(mk.t), top), ImVec2(tToX(mk.t), bot), pal::AccentA(70));
    bdl->AddLine(ImVec2(px, top), ImVec2(px, bot), pal::Accent, S(1));
    if (ts.snapLine >= 0) {
        // magnet guide: a line at the target plus what it is (the clip it belongs to is outlined)
        float sx = tToX(ts.snapLine);
        bdl->AddLine(ImVec2(sx, top), ImVec2(sx, bot), IM_COL32(0xFF, 0xFF, 0xFF, 0xD0), S(1.5f));
        bdl->AddTriangleFilled(ImVec2(sx - S(5), top), ImVec2(sx + S(5), top), ImVec2(sx, top + S(6)), pal::White);
        std::string what;
        static const char* kinds[] = {"Composition edge", "Work area", "Playhead", "Marker", "", "Keyframe"};
        if (ts.snapHit.layer) {
            for (auto& br : ts.barRects)
                if (br.first == ts.snapHit.layer) {
                    ImRect r = br.second;
                    bdl->AddRect(r.Min - ImVec2(S(1), S(1)), r.Max + ImVec2(S(1), S(1)), pal::White, S(2), 0, S(2));
                    float ex = ts.snapHit.kind == 4 ? sx : -1;
                    if (ex >= 0) bdl->AddRectFilled(ImVec2(ex - S(2), r.Min.y), ImVec2(ex + S(2), r.Max.y), pal::White);
                }
            if (Layer* tl = c->find(ts.snapHit.layer)) what = (ts.snapHit.kind == 5 ? std::string(T("Keyframe")) + ": " : std::string()) + tl->name;
        } else {
            what = T(kinds[clampv(ts.snapHit.kind, 0, 5)]);
        }
        if (!what.empty()) {
            ImVec2 tsz = ImGui::CalcTextSize(what.c_str());
            ImVec2 tp(std::min(sx + S(6), rx1 - tsz.x - S(10)), top + S(8));
            bdl->AddRectFilled(tp - ImVec2(S(4), S(2)), tp + tsz + ImVec2(S(4), S(2)), IM_COL32(0x10, 0x10, 0x10, 0xE0), S(3));
            bdl->AddText(tp, pal::White, what.c_str());
        }
    }
    bdl->PopClipRect();

    // wheel: vertical scroll by default, Alt = zoom, Shift = scroll time (also while drawing a selection box)
    if ((ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ts.marquee) && io.MouseWheel != 0) {
        if (io.KeyAlt || io.KeyCtrl) {
            double anchorT = xToT(io.MousePos.x);
            c->tlZoom = clampv(c->tlZoom * (io.MouseWheel > 0 ? 1.2f : 1 / 1.2f), fitPps * 0.5f, (float)(c->fps * S(40)));
            c->tlScroll = std::max(0.0, anchorT - (io.MousePos.x - g_rx0) / c->tlZoom);
        } else if (io.KeyShift) {
            c->tlScroll = std::max(0.0, c->tlScroll - io.MouseWheel * 60.0 / c->tlZoom);
        } else {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * rowH * 3);
        }
    }
    if (io.MouseWheelH != 0 && ImGui::IsWindowHovered()) c->tlScroll = std::max(0.0, c->tlScroll - io.MouseWheelH * 60.0 / c->tlZoom);
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // horizontal time scrollbar
    ImGui::End();
}
