// UI: panels, dialogs and shared widgets.
#pragma once
#include "app.h"

enum class Icon {
    Play, Pause, StepBack, StepFwd, ToStart, ToEnd, Loop, Eye, Speaker, Solo, Lock, Stopwatch, KeyPrev, KeyNext, Key, Plus,
    Close, Folder, Comp, Video, Image, Audio, Text, Solid, Adjust, Null, Trash, Gear, Search, Arrow, Hand, Zoom, Rotate, Anchor,
    Rect, Ellipse, TextTool, Razor, Fx, MotionBlur, Snap, Home, Grid, Safe, Checker, Camera, Plug, ChevronRight, ChevronDown,
    Reset, Shy, Sequence, Link, Freeze, Graph, Cube, Light, Fullscreen
};

// shared widgets (ui_common.cpp)
void drawIcon(ImDrawList* dl, Icon ic, ImVec2 c, float s, ImU32 col);
bool iconButton(const char* id, Icon ic, bool active = false, const char* tip = nullptr, float size = 0);
bool iconToggle(const char* id, Icon ic, bool* v, const char* tip = nullptr);
void tooltip(const char* text);
void sectionLabel(const char* text);
bool accentButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool scrubFloat(const char* id, float* v, float speed, float mn, float mx, const char* fmt, bool accent = true);
bool timecodeField(const char* id, double* t, double fps, ImU32 col, float fontScale = 1.f);
std::string timeLabel(double t, double fps);
float S(float v);  // dpi scaled size

// property editors shared by timeline + effect controls
struct PropUI {
    const char* label = "";
    int kind = 0;  // 0 float 1 vec2 2 color 3 bool 4 choice 5 angle 6 percent 7 point(px) 8 scale(%) 9 dB
    float speed = 1, mn = -1e9f, mx = 1e9f;
    const char* unit = "";
    const std::vector<std::string>* choices = nullptr;
    ImVec2 pxScale = ImVec2(1, 1);  // for points stored normalized
};
bool stopwatch(const char* id, Prop& p, double lt);
bool keyNav(const char* id, Prop& p, double lt, double layerStart, double fps);
bool propValue(const char* id, Prop& p, double lt, const PropUI& ui, float width);
PropUI uiForParam(const ParamDef& d, ImVec2 layerSize);

// graph editor (ui_graph.cpp): easing curve of the motion between two keyframes
struct CurveDef {
    const char* name;
    Curve type;
    float bz[4];
};
extern const CurveDef kCurvePresets[];
extern const int kCurvePresetCount;
struct CurveSeg {
    Keyframe* a;  // the curve is stored on the first key of the segment
    Keyframe* b;
};
bool curveEditor(const char* id, Curve& type, float bz[4], float width);
bool keyCurveSection(App& a, const std::vector<CurveSeg>& segs, bool* applied = nullptr);
std::vector<CurveSeg> selectedKeySegments(App& a);
void panelGraph(App& a);

// theme / fonts
void uiApplyTheme(App& a);
void uiLoadFonts(App& a);

// screens
void uiSplash(App& a);
void uiHome(App& a);
void uiEditor(App& a);

// panels
void panelProject(App& a);
void panelViewer(App& a);
void panelTimeline(App& a);
void panelEffectControls(App& a);
void panelEffects(App& a);
void focusEffectSearch(App& a);
void panelInfo(App& a);
void panelPreview(App& a);
void panelCharacter(App& a);
void panelAudio(App& a);
void panelRenderQueue(App& a);
void uiDialogs(App& a);
void backgroundEditor(App& a, Composition& c);
void uiToasts(App& a);
void handleShortcuts(App& a);
void renderViewerIfNeeded(App& a);
void viewerFullscreen(App& a);

// drag & drop payload ids
#define DND_ITEM "GESE_ITEM"      // uint64 project item id
#define DND_EFFECT "GESE_EFFECT"  // effect def index (int)
#define DND_PRESET "GESE_PRESET"  // preset path index (int)

// composition presets
struct CompPreset {
    const char* name;
    int w, h;
    double fps;
};
extern const CompPreset kCompPresets[];
extern const int kCompPresetCount;
extern const char* const kFacts[];
extern const int kFactCount;
