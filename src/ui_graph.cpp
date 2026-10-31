// Graph Editor: easing curves between keyframes. Ready-made curves + two draggable handles (cubic-bezier).
#include "ui.h"
#include "i18n.h"
#include "imgui_internal.h"

const CurveDef kCurvePresets[] = {
    {"Linear", Curve::Bezier, {0, 0, 1, 1}},
    {"Smooth", Curve::Bezier, {0.333f, 0, 0.667f, 1}},
    {"Ease In", Curve::Bezier, {0.42f, 0, 1, 1}},
    {"Ease Out", Curve::Bezier, {0, 0, 0.58f, 1}},
    {"Ease In-Out", Curve::Bezier, {0.42f, 0, 0.58f, 1}},
    {"Fast Start", Curve::Bezier, {0.16f, 1, 0.3f, 1}},
    {"Slow Start", Curve::Bezier, {0.7f, 0, 0.84f, 0}},
    {"Strong In-Out", Curve::Bezier, {0.87f, 0, 0.13f, 1}},
    {"Back In", Curve::Bezier, {0.36f, 0, 0.66f, -0.56f}},
    {"Back Out", Curve::Bezier, {0.34f, 1.56f, 0.64f, 1}},
    {"Back In-Out", Curve::Bezier, {0.68f, -0.6f, 0.32f, 1.6f}},
    {"Bounce", Curve::Bounce, {0, 0, 1, 1}},
    {"Elastic", Curve::Elastic, {0, 0, 1, 1}},
};
const int kCurvePresetCount = (int)(sizeof(kCurvePresets) / sizeof(kCurvePresets[0]));

static inline float bzc(float s, float a, float b) {
    float is = 1 - s;
    return 3 * is * is * s * a + 3 * is * s * s * b + s * s * s;
}

// the classic in/out (ease) interpolation of a segment, expressed as a cubic-bezier
static void classicBezier(const Keyframe& k, const Keyframe& n, float bz[4]) {
    bz[0] = k.out == Interp::Ease ? k.infOut / 100.f : 1.f / 3.f;
    bz[1] = k.out == Interp::Ease ? 0.f : 1.f / 3.f;
    bz[2] = n.in == Interp::Ease ? 1.f - n.infIn / 100.f : 2.f / 3.f;
    bz[3] = n.in == Interp::Ease ? 1.f : 2.f / 3.f;
}

static void drawCurvePath(ImDrawList* dl, ImVec2 p0, ImVec2 sz, Curve type, const float bz[4], float yLo, float yHi, ImU32 col, float th) {
    auto P = [&](float x, float y) { return ImVec2(p0.x + x * sz.x, p0.y + (yHi - y) / (yHi - yLo) * sz.y); };
    ImVec2 pts[97];
    for (int i = 0; i <= 96; i++) {
        float s = i / 96.f;
        if (type == Curve::Bezier) pts[i] = P(bzc(s, bz[0], bz[2]), bzc(s, bz[1], bz[3]));
        else pts[i] = P(s, curveValue(type, bz, s));
    }
    dl->AddPolyline(pts, 97, col, th);
}

static bool sameCurve(Curve t1, const float* a, Curve t2, const float* b) {
    if (t1 != t2) return false;
    if (t1 != Curve::Bezier) return true;
    for (int i = 0; i < 4; i++) if (fabsf(a[i] - b[i]) > 0.002f) return false;
    return true;
}

static bool curveThumb(const char* id, const char* name, Curve type, const float* bz, bool active, float sz) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(sz, sz), hov ? IM_COL32(0x3A, 0x3A, 0x3A, 0xFF) : pal::Bg1, S(3));
    if (active) dl->AddRect(p, p + ImVec2(sz, sz), pal::Accent, S(3), 0, S(1.5f));
    float m = sz * 0.2f;
    drawCurvePath(dl, p + ImVec2(m, m), ImVec2(sz - 2 * m, sz - 2 * m), type, bz, -0.3f, 1.3f, active ? pal::Accent : pal::Text, S(1.5f));
    if (hov) ImGui::SetTooltip("%s", T(name));
    return pressed;
}

bool curveEditor(const char* id, Curve& type, float bz[4], float w) {
    const float yLo = -0.35f, yHi = 1.35f;
    float h = w * 1.1f, pad = S(12);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c0 = p + ImVec2(pad, pad), sz(w - pad * 2, h - pad * 2);
    auto P = [&](float x, float y) { return ImVec2(c0.x + x * sz.x, c0.y + (yHi - y) / (yHi - yLo) * sz.y); };
    ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGuiID gid = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), pal::Bg1, S(3));
    for (int i = 0; i <= 4; i++) {
        float f = i / 4.f;
        ImU32 gc = (i == 0 || i == 4) ? pal::Line : IM_COL32(0xFF, 0xFF, 0xFF, 0x12);
        dl->AddLine(P(f, 0), P(f, 1), gc);
        dl->AddLine(P(0, f), P(1, f), gc);
    }
    dl->AddLine(P(0, 0), P(1, 1), IM_COL32(0xFF, 0xFF, 0xFF, 0x22));
    dl->AddText(P(0, 1) + ImVec2(S(3), S(2)), pal::TextFaint, T("value"));
    ImVec2 ts = ImGui::CalcTextSize(T("time"));
    dl->AddText(P(1, 0) - ImVec2(ts.x + S(3), ts.y + S(2)), pal::TextFaint, T("time"));

    bool changed = false;
    int* handle = ImGui::GetStateStorage()->GetIntRef(gid, -1);
    ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        if (type != Curve::Bezier) {  // a ready-made bounce / elastic becomes an editable curve
            type = Curve::Bezier;
            bz[0] = 0.333f, bz[1] = 0, bz[2] = 0.667f, bz[3] = 1;
            changed = true;
        }
        float d1 = ImLengthSqr(m - P(bz[0], bz[1])), d2 = ImLengthSqr(m - P(bz[2], bz[3]));
        *handle = d1 <= d2 ? 0 : 1;
    }
    if (ImGui::IsItemActive() && *handle >= 0) {
        float x = (m.x - c0.x) / sz.x, y = yHi - (m.y - c0.y) / sz.y * (yHi - yLo);
        if (ImGui::GetIO().KeyShift) y = std::round(y * 20) / 20, x = std::round(x * 20) / 20;
        bz[*handle * 2] = clampv(x, 0.f, 1.f);
        bz[*handle * 2 + 1] = clampv(y, yLo, yHi);
        changed = true;
    }
    if (ImGui::IsItemDeactivated()) *handle = -1;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    dl->PushClipRect(p, p + ImVec2(w, h), true);
    drawCurvePath(dl, c0, sz, type, bz, yLo, yHi, pal::Accent, S(2.4f));
    if (type == Curve::Bezier) {
        ImVec2 h1 = P(bz[0], bz[1]), h2 = P(bz[2], bz[3]);
        dl->AddLine(P(0, 0), h1, pal::TextDim, S(1.2f));
        dl->AddLine(P(1, 1), h2, pal::TextDim, S(1.2f));
        for (int i = 0; i < 2; i++) {
            ImVec2 hp = i ? h2 : h1;
            bool hot = *handle == i || (hovered && ImLengthSqr(m - hp) < S(12) * S(12));
            dl->AddCircleFilled(hp, S(hot ? 7.f : 6.f), hot ? pal::White : pal::Accent);
            dl->AddCircle(hp, S(hot ? 7.f : 6.f), IM_COL32(0x10, 0x10, 0x10, 0xFF), 0, S(1.2f));
        }
    }
    dl->AddCircleFilled(P(0, 0), S(3.5f), pal::Text);
    dl->AddCircleFilled(P(1, 1), S(3.5f), pal::Text);
    // a dot running along the curve shows the motion speed
    float ph = std::min(1.f, fmodf((float)ImGui::GetTime(), 1.9f) / 1.4f);
    dl->AddCircleFilled(P(ph, curveValue(type, bz, ph)), S(4), pal::White);
    dl->PopClipRect();
    return changed;
}

bool keyCurveSection(App& a, const std::vector<CurveSeg>& segs, bool* applied) {
    if (segs.empty()) {
        ImGui::TextWrapped("%s", T("A curve is set between two keyframes: add one more keyframe to this property."));
        return false;
    }
    Keyframe& k = *segs[0].a;
    Curve type = k.curve;
    float bz[4];
    if (type == Curve::Classic) {
        classicBezier(k, *segs[0].b, bz);
        type = Curve::Bezier;
    } else {
        memcpy(bz, k.bz, sizeof(bz));
    }
    bool hold = k.out == Interp::Hold;
    bool changed = false;
    ImGui::PushID("curves");
    float avail = ImGui::GetContentRegionAvail().x;
    float th = S(44), gap = S(4);
    int perRow = std::max(1, (int)((avail + gap) / (th + gap)));
    int n = 0;
    for (int i = 0; i < kCurvePresetCount; i++, n++) {
        if (n % perRow) ImGui::SameLine(0, gap);
        const CurveDef& d = kCurvePresets[i];
        if (curveThumb(strf("##cp%d", i).c_str(), d.name, d.type, d.bz, !hold && sameCurve(type, bz, d.type, d.bz), th)) {
            type = d.type;
            memcpy(bz, d.bz, sizeof(bz));
            changed = true;
        }
    }
    auto& user = a.settings.curvePresets;
    for (int i = 0; i < (int)user.size(); i++, n++) {
        if (n % perRow) ImGui::SameLine(0, gap);
        Curve ut = (Curve)clampv(user[i].type, 1, 3);
        if (curveThumb(strf("##up%d", i).c_str(), user[i].name.c_str(), ut, user[i].bz, !hold && sameCurve(type, bz, ut, user[i].bz), th)) {
            type = ut;
            memcpy(bz, user[i].bz, sizeof(bz));
            changed = true;
        }
        if (ImGui::BeginPopupContextItem(strf("upc%d", i).c_str())) {
            if (ImGui::MenuItem("Delete Curve Preset")) {
                user.erase(user.begin() + i);
                a.settings.save();
                ImGui::EndPopup();
                break;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    if (hold) ImGui::TextColored(pal::v4(pal::Accent), "%s", T("Hold: the value jumps at the next keyframe. Pick a curve to animate smoothly."));
    float ew = std::min(avail, S(320));
    if (curveEditor("##curve", type, bz, ew)) changed = true;
    ImGui::BeginDisabled(type != Curve::Bezier);
    ImGui::SetNextItemWidth(ew);
    if (ImGui::DragFloat4("##bz", bz, 0.005f, -2.f, 3.f, "%.3f")) changed = true;
    ImGui::EndDisabled();
    tooltip("cubic-bezier(x1, y1, x2, y2): drag the handles on the graph or type exact values. Shift snaps to 0.05.");
    bz[0] = clampv(bz[0], 0.f, 1.f);
    bz[2] = clampv(bz[2], 0.f, 1.f);
    // edits show at once; Apply puts the curve on every selected segment (e.g. after selecting more keys)
    if (accentButton(strf("%s##applycurve", T("Apply")).c_str(), ImVec2(ew, 0))) {
        changed = true;
        if (applied) *applied = true;
        a.notify(strf(T("Curve applied to %d keyframe(s)"), (int)segs.size()));
    }
    tooltip("Changes are visible right away; Apply sets this curve on all selected keyframes");
    ImGui::Dummy(ImVec2(0, S(4)));
    static char name[64] = "";
    ImGui::SetNextItemWidth(std::max(S(80), ew - S(130)));
    ImGui::InputTextWithHint("##cvn", T("My curve"), name, sizeof(name));
    ImGui::SameLine(0, S(6));
    if (ImGui::Button("Save Curve", ImVec2(S(124), 0))) {
        Settings::CurvePreset p;
        p.name = trim(name).empty() ? strf("%s %d", T("My curve"), (int)user.size() + 1) : trim(name);
        p.type = (int)type;
        memcpy(p.bz, bz, sizeof(bz));
        user.push_back(p);
        a.settings.save();
        name[0] = 0;
        a.notify(std::string(T("Curve saved: ")) + p.name);
    }
    ImGui::PopID();
    if (changed) {
        for (auto& s : segs) {
            s.a->curve = type;
            memcpy(s.a->bz, bz, sizeof(bz));
            if (s.a->out == Interp::Hold) s.a->out = Interp::Linear;
        }
        a.markChanged();
    }
    return changed;
}

std::vector<CurveSeg> selectedKeySegments(App& a) {
    std::vector<CurveSeg> segs;
    Composition* c = a.comp();
    if (!c) return segs;
    for (auto& l : c->layers)
        for (auto& pr : a.layerProps(*l)) {
            auto& ks = pr.second->keys;
            if (!pr.second->animated || pr.second->stepped) continue;
            for (size_t i = 0; i < ks.size(); i++) {
                if (!ks[i].sel) continue;
                if (i + 1 < ks.size()) segs.push_back({&ks[i], &ks[i + 1]});
                else if (i > 0 && !ks[i - 1].sel) segs.push_back({&ks[i - 1], &ks[i]});
            }
        }
    return segs;
}

void panelGraph(App& a) {
    ImGui::SetNextWindowSize(ImVec2(S(380), S(640)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(T("Graph Editor")) + "###Graph Editor").c_str(), &a.pGraph)) { ImGui::End(); return; }
    auto segs = selectedKeySegments(a);
    if (segs.empty()) {
        ImGui::TextWrapped("%s", T("Select keyframes on the timeline (click a key on a clip or on a property row): the curve of the motion between them is edited here."));
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::TextDisabled("%s", T("Tip: right-click a keyframe on a clip to open its parameters."));
    } else {
        ImGui::TextDisabled(T("Curves between keyframes: %d"), (int)segs.size());
        keyCurveSection(a, segs);
    }
    ImGui::End();
}
