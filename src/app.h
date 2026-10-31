// Application state shared by all panels.
#pragma once
#include "core.h"
#include "project.h"
#include "effects.h"
#include "renderer.h"
#include "audio.h"
#include "exporter.h"
#include "ai_bg.h"
#include "media.h"

enum class Screen { Splash, Home, Editor };
enum class Tool { Select, Hand, Zoom, Rotate, Anchor, MaskRect, MaskEllipse, Text, Razor };

struct Settings {
    // general
    int autosaveMin = 5;
    int undoLevels = 120;
    bool showFacts = true;
    bool confirmDelete = false;
    // appearance
    float uiScale = 1.0f;
    float fontSize = 16.0f;
    int language = -1;  // -1 = follow Windows, see i18n.h
    double freezeDuration = 3.0;
    bool highlightRows = true;
    // playback & media
    int previewRes = 0;  // 0 auto 1 full 2 half 3 third 4 quarter
    bool loop = true;
    int cacheMB = 2048;
    double stillDuration = 5.0;
    double seqFps = 30.0;
    bool importSequences = false;  // numbered photos (IMG_0001.jpg) are photos unless imported as a sequence
    bool skipFrames = true;
    // timeline
    bool snapping = true;
    int timeDisplay = 0;  // 0 timecode 1 frames
    bool mediaGrid = true;  // Media panel: thumbnails or the detailed list
    // output
    std::string exportDir;
    int exportPreset = 0;
    // AI
    int aiProvider = 0;  // see kAiProviders: 0 = built-in neural network
    bool aiGpu = true;   // DirectML on any DX12 GPU, otherwise CPU
    std::string removeBgKey, photoroomKey, customKey;
    std::string rembgUrl = "http://127.0.0.1:7000/api/remove";
    std::string customUrl;
    // graph editor: user curves
    struct CurvePreset {
        std::string name;
        int type = 1;
        float bz[4] = {0.333f, 0, 0.667f, 1};
    };
    std::vector<CurvePreset> curvePresets;
    // plugins
    std::vector<std::string> disabledPlugins;
    // misc
    std::vector<std::string> recent;
    int lastCompPreset = 0;
    void load();
    void save() const;
    void addRecent(const std::string& p);
};

// Identifies an animatable property inside a layer (for timeline rows & keyframe ops)
struct PropRef {
    uint64_t layer = 0;
    int kind = 0;        // 0 transform, 1 effect, 2 mask, 3 text, 4 audio, 5 camera, 6 light, 7 material
    uint64_t owner = 0;  // effect id / mask id
    int index = 0;       // transform 0..4, effect: -1 intensity, -2 speed, >=0 param; mask 0..5; text 0..5
    bool operator==(const PropRef& o) const { return layer == o.layer && kind == o.kind && owner == o.owner && index == o.index; }
};

struct Toast {
    std::string text;
    double until = 0;
    bool error = false;
    double born = 0;
    uint64_t id = 0;
};

struct App {
    HWND hwnd = nullptr;
    float dpi = 1.f;
    Screen screen = Screen::Splash;
    bool quitRequested = false, quitConfirmed = false;

    // splash
    double splashStart = 0;
    bool loadingDone = false;
    double loadDoneAt = 0;
    int loadStage = 0;
    std::string loadingLine;
    int factIndex = 0;
    TexPtr splashTex, logoTex;

    Settings settings;
    std::unique_ptr<Project> project;
    EffectRegistry effects;
    Renderer renderer;
    AudioEngine audio;
    Exporter exporter;
    AiBackgroundRemover ai;

    ImFont* fUI = nullptr;
    ImFont* fBold = nullptr;
    ImFont* fAkira = nullptr;
    ImFont* fArial = nullptr;
    ImFont* fMono = nullptr;
    std::string uiFontName;

    // editor state
    uint64_t activeComp = 0;
    std::vector<uint64_t> openComps;
    std::set<uint64_t> selLayers;
    uint64_t selEffect = 0;
    std::set<uint64_t> selItems;
    Tool tool = Tool::Select;
    bool playing = false;
    double playFrom = 0;
    double lastFrameWall = 0;
    float measuredFps = 0;
    int framesShown = 0;
    double fpsTimer = 0;

    // viewer
    float viewZoom = 0;  // 0 = fit
    ImVec2 viewPan = ImVec2(0, 0);
    int viewChannel = 0;
    bool showTransparency = false, showSafe = false, showGrid = false, showRulers = false;
    TexPtr compRT, viewTex;
    bool needRender = true;
    float lastRenderMs = 0;
    float pickRGBA[4] = {0, 0, 0, 0};
    ImVec2 pickPos = ImVec2(-1, -1);

    // undo
    std::deque<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string lastSnapshot;
    bool changed = false;
    bool dirty = false;
    double lastAutosave = 0;

    // dialogs
    bool dlgPrefs = false, dlgCompSettings = false, dlgNewComp = false, dlgSolid = false, dlgAbout = false, dlgShortcuts = false;
    bool dlgPlugins = false, dlgAi = false, dlgOutput = false, dlgSpeed = false, dlgPrecompose = false, dlgSavePreset = false;
    bool dlgSdk = false, dlgUnsaved = false;
    int prefsPage = 0;
    uint64_t editRenderItem = 0;
    int unsavedAction = 0;  // 1 new 2 open 3 quit 4 home
    std::string pendingOpenPath;

    // layout
    int workspace = 0;
    bool resetLayout = true;
    bool pProject = true, pFxControls = true, pViewer = true, pTimeline = true, pEffects = true, pInfo = true, pPreview = true,
         pCharacter = true, pAudio = true, pRender = true, pGraph = false;
    std::string focusPanel;

    // full screen preview (F11) and the effect under the cursor in the Effects panel (previewed in the viewer)
    bool fullPreview = false;
    WINDOWPLACEMENT fpPlacement{};
    LONG_PTR fpStyle = 0;
    std::string hoverFx;
    uint64_t audioFxGen = 0;

    // clipboard (JSON)
    Json clipLayers, clipEffects;
    std::string lastEffectId;
    std::vector<Toast> toasts;

    // ---- methods (app.cpp)
    Composition* comp();
    Layer* selectedLayer();
    std::vector<Layer*> selectedLayers();
    void notify(const std::string& s, bool error = false);
    void setDpi(float d);

    void newProject();
    bool openProject(const std::string& path);
    bool saveProject(bool saveAs);
    void closeProject();
    void attachMedia(Project& p, Project* old);
    void requestAction(int action);  // checks unsaved changes
    void doAction(int action);

    void markChanged() { changed = true; needRender = true; }
    void commitIfIdle();
    void commitNow();
    void resetUndo();
    void undo();
    void redo();
    void restoreSnapshot(const std::string& s);

    Composition* newComp(const std::string& name, int w, int h, double fps, double dur);
    void openComp(uint64_t id);
    void closeComp(uint64_t id);
    std::vector<FootagePtr> importFiles(const std::vector<std::string>& paths);
    FootagePtr importFile(const std::string& path);
    std::vector<FootagePtr> importFolder(const std::string& dir, uint64_t parentFolder, const std::string& name = "");
    std::vector<FootagePtr> importZip(const std::string& zip);
    void importDialog();
    void importFolderDialog();
    void importZipDialog();
    Layer* addFootageLayer(Composition& c, Footage& f, double at, int index = 0);
    Layer* addCompLayer(Composition& c, Composition& src, double at, int index = 0);
    Layer* addSolid(Composition& c, const std::string& name, int w, int h, const float col[4], LayerType type = LayerType::Solid);
    Layer* addText(Composition& c, const std::string& text, ImVec2 pos);
    Layer* addNull(Composition& c);
    Layer* addEffectLayer(Composition& c, const EffectDef* d, double at, int index = 0);
    Layer* addCamera(Composition& c);
    Layer* addLight(Composition& c, int kind);
    void applyEffect(const EffectDef* d, Layer* target = nullptr);
    void applyPreset(const std::string& path, Layer* target = nullptr);
    bool savePreset(const std::string& name, Layer& l);
    void deleteSelectedLayers(bool ripple);
    void duplicateSelectedLayers();
    void splitSelectedLayers(double t);
    void freezeFrame(double t);  // splits and inserts a held frame
    void precomposeSelected(const std::string& name);
    void unpackPrecomp(Layer& compoundClip);
    uint64_t pendingUnpack = 0;  // requested from a timeline row: done after the frame
    void moveSelectedLayers(int dir, bool toEnd);
    void copyLayers();
    void pasteLayers();
    void selectLayer(uint64_t id, bool add, bool range);
    void fitLayer(Layer& l, bool fill);
    void deleteSelectedKeys();
    void setSelectedKeysInterp(int mode);  // 0 linear 1 ease 2 ease in 3 ease out 4 hold toggle
    bool anyKeySelected();
    void clearKeySelection();
    Prop* resolve(const PropRef& r);
    std::vector<std::pair<PropRef, Prop*>> layerProps(Layer& l);

    void togglePlay();
    void setTime(double t);
    double time();
    void stepFrames(int n);
    void updatePlayback();
    void buildAudio();
    void queueRender(uint64_t compId);
    void startRenderQueue();
    void tickRender();
    void startAi(int provider, bool allFrames);
    void tickAi();
    // audio of a clip: extract to its own layer / save to a file; whole comp audio
    void extractAudio(Layer& l);
    void saveLayerAudio(Layer& l);
    void saveCompAudio(Composition& c);
    void setLayerSpeed(Layer& l, double speed);
    Prop* effectParam(EffectInst& e, const char* name);
    EffectInst* findFade(Layer& l);
    int fadeMode(Layer& l);              // 0 none 1 fade in 2 fade out 3 both
    void toggleFade(Layer& l, bool out);  // video fade in (проявление) / fade out (выцветание)
    int deleteHandledFrame = -1;          // a panel used Delete this frame (e.g. removed an effect)
    void pollAudioFx();
    // view
    void setFullPreview(bool on);
    void jumpToEdit(int dir);  // playhead to the previous / next clip edge, keyframe or marker
    void autosave();
    std::vector<std::string> presetFiles();
    std::string presetDir() const;
};
extern App* g_app;

// file dialogs (ui_util.cpp)
std::string dlgOpenFile(HWND owner, const wchar_t* filter, const char* title);
std::vector<std::string> dlgOpenFiles(HWND owner, const wchar_t* filter, const char* title);
std::string dlgSaveFile(HWND owner, const wchar_t* filter, const char* defExt, const std::string& defName, const char* title);
std::string dlgPickFolder(HWND owner, const char* title);
void shellOpen(const std::string& path);
