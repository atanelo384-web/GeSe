// Project data model: footage, compositions, layers, animatable properties.
#pragma once
#include "core.h"
#include "json.h"
#include "gfx.h"

struct MediaSource;
struct AudioData;

enum class Interp : uint8_t { Linear = 0, Ease = 1, Hold = 2 };

// Graph-editor curve of the segment that starts at a keyframe
enum class Curve : uint8_t { Classic = 0, Bezier = 1, Bounce = 2, Elastic = 3 };

struct Keyframe {
    double t = 0;  // layer-local time (seconds from layer start)
    float v[4] = {0, 0, 0, 0};
    Interp in = Interp::Linear, out = Interp::Linear;
    float infIn = 33.333f, infOut = 33.333f;  // ease influence %
    Curve curve = Curve::Classic;              // Classic = in/out interpolation above
    float bz[4] = {0.333f, 0.f, 0.667f, 1.f};  // cubic-bezier x1 y1 x2 y2 (CSS style) for Curve::Bezier
    bool sel = false;
};
float curveValue(Curve type, const float bz[4], float u);  // eased progress 0..1 (may overshoot)

struct Prop {
    int dim = 1;
    float v[4] = {0, 0, 0, 0};
    bool animated = false;
    bool stepped = false;  // bool / choice params always hold
    std::vector<Keyframe> keys;

    Prop() {}
    Prop(int d, float a, float b = 0, float c = 0, float e = 0) : dim(d) { v[0] = a; v[1] = b; v[2] = c; v[3] = e; }
    void eval(double t, float out[4]) const;
    float f(double t) const { float o[4]; eval(t, o); return o[0]; }
    ImVec2 v2(double t) const { float o[4]; eval(t, o); return ImVec2(o[0], o[1]); }
    ImVec4 col(double t) const { float o[4]; eval(t, o); return ImVec4(o[0], o[1], o[2], o[3]); }
    void set(double t, const float* val);
    void setF(double t, float x) { float a[4] = {x, v[1], v[2], v[3]}; set(t, a); }
    void set2(double t, float x, float y) { float a[4] = {x, y, v[2], v[3]}; set(t, a); }
    void setAnimated(bool on, double t);
    int keyAt(double t, double eps = 1e-4) const;
    int addKey(double t, const float* val);
    void sortKeys();
    bool hasSelectedKeys() const;
    Json save() const;
    void load(const Json& j);
};

enum class LayerType : int { Footage = 0, Solid, Text, Adjustment, Null, Precomp, Camera, Light };
static const char* const kLayerTypeNames[] = {"Footage", "Solid", "Text", "Effect Layer", "Null", "Compound Clip", "Camera", "Light"};
enum LightKind { LK_Parallel = 0, LK_Spot, LK_Point, LK_Ambient };
static const char* const kLightKindNames[] = {"Parallel", "Spot", "Point", "Ambient"};

enum BlendMode {
    BM_Normal = 0, BM_Dissolve, BM_Darken, BM_Multiply, BM_ColorBurn, BM_LinearBurn, BM_Lighten, BM_Screen, BM_ColorDodge,
    BM_Add, BM_Overlay, BM_SoftLight, BM_HardLight, BM_VividLight, BM_LinearLight, BM_PinLight, BM_HardMix, BM_Difference,
    BM_Exclusion, BM_Subtract, BM_Divide, BM_Hue, BM_Saturation, BM_Color, BM_Luminosity, BM_Count
};
static const char* const kBlendNames[] = {"Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Lighten",
                                          "Screen", "Color Dodge", "Add", "Overlay", "Soft Light", "Hard Light", "Vivid Light",
                                          "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion", "Subtract",
                                          "Divide", "Hue", "Saturation", "Color", "Luminosity"};
static const char* const kMatteNames[] = {"None", "Alpha Matte", "Alpha Inverted Matte", "Luma Matte", "Luma Inverted Matte"};

struct Mask {
    uint64_t id = 0;
    std::string name = "Mask 1";
    int shape = 0;   // 0 rectangle, 1 ellipse
    int mode = 0;    // 0 add, 1 subtract, 2 intersect
    bool inverted = false;
    bool enabled = true;
    bool expanded = true;
    Prop center{2, 0, 0}, size{2, 100, 100}, feather{1, 0}, roundness{1, 0}, opacity{1, 100}, expansion{1, 0};
    Json save() const;
    void load(const Json& j);
};

struct EffectInst {
    uint64_t id = 0;
    std::string defId;
    std::string name;
    bool enabled = true;
    bool expanded = true;
    Prop intensity{1, 100};
    Prop speed{1, 1};
    std::vector<Prop> params;
    std::vector<std::string> paramNames;  // to remap after plugin reloads
    Json save() const;
    void load(const Json& j);
};

struct TextData {
    std::string text = "Text";
    std::string font = "Arial";
    bool bold = true, italic = false, allCaps = false;
    int align = 1;  // 0 left 1 center 2 right
    Prop size{1, 120}, fill{4, 1, 1, 1, 1}, stroke{4, 0, 0, 0, 1}, strokeWidth{1, 0}, tracking{1, 0}, leading{1, 1.15f};
    Json save() const;
    void load(const Json& j);
};

struct Layer {
    uint64_t id = 0;
    std::string name;
    LayerType type = LayerType::Footage;
    uint64_t source = 0;  // footage id or comp id
    double startTime = 0, inPoint = 0, outPoint = 10;
    double speed = 1.0;
    bool reversed = false;
    bool visible = true, audioOn = true, solo = false, locked = false, shy = false;
    bool fxOn = true, motionBlur = false;
    // motion blur: amount, blend with the sharp frame, direction, number of copies
    float mbBlur = 30;     // % (100 = two frames of movement)
    float mbBlend = 100;   // % of blurred copies mixed over the sharp frame
    int mbDir = 0;         // 0 both sides, 1 left (trail behind), 2 right (ahead)
    int mbSamples = 8;     // "times" / multiplicity, 2..16
    // audio speed & voice
    bool keepPitch = false;  // speed change keeps the voice pitch (time-stretch); off = "vinyl", pitch follows speed
    float pitch = 0;         // semitones, independent of speed
    int voice = 0;           // voice preset, see kVoicePresets (audiofx.h)
    bool denoise = false, enhanceVoice = false;
    float audioFadeIn = 0, audioFadeOut = 0;  // seconds
    int blend = BM_Normal;
    int matte = 0;
    uint64_t parent = 0;
    // transform (layer px / comp px)
    Prop anchor{2, 0, 0}, position{2, 0, 0}, scale{2, 100, 100}, rotation{1, 0}, opacity{1, 100};
    Prop volume{1, 0};  // dB
    // 3D: depth, X / Y rotation (rotation above is Z), seen through the comp camera
    bool threeD = false;
    Prop posZ{1, 0}, rotX{1, 0}, rotY{1, 0};
    // material: how a 3D layer reacts to lights (%)
    bool acceptsLights = true;
    Prop matAmbient{1, 100}, matDiffuse{1, 100}, matSpecular{1, 20}, matShininess{1, 15};
    // camera / light layers: point of interest (target), zoom & depth of field / light colour, intensity, cone
    bool useTarget = true;
    Prop target{2, 960, 540}, targetZ{1, 0};
    Prop camZoom{1, 2666.7f}, focusDist{1, 2666.7f}, aperture{1, 25}, blurLevel{1, 100};
    bool dof = false;
    int lightKind = LK_Point;
    Prop lightColor{4, 1, 0.96f, 0.88f, 1}, lightIntensity{1, 100}, coneAngle{1, 90}, coneFeather{1, 50}, lightRadius{1, 0}, lightFalloff{1, 800};
    std::vector<EffectInst> effects;
    std::vector<Mask> masks;
    TextData text;
    float solidColor[4] = {0.72f, 0.33f, 0.0f, 1};
    int solidW = 1920, solidH = 1080;
    // ui state
    bool expanded = false, xfOpen = true, fxOpen = true, maskOpen = true, audioOpen = false, textOpen = false, optOpen = true, matOpen = false;

    double localTime(double compT) const { return compT - startTime; }
    double sourceTime(double compT) const {
        double t = reversed ? (inPoint + outPoint - compT) : compT;
        return (t - startTime) * speed;
    }
    bool activeAt(double t) const { return t >= inPoint - 1e-9 && t < outPoint - 1e-9; }
    Json save() const;
    void load(const Json& j);
};
using LayerPtr = std::shared_ptr<Layer>;

struct Marker {
    double t = 0;
    std::string label;
};

struct Composition {
    uint64_t id = 0;
    std::string name = "Comp 1";
    int w = 1920, h = 1080;
    double fps = 30;
    double duration = 10;
    float bg[4] = {0, 0, 0, 1};
    // background ("canvas"): 0 color (bg), 1 blurred copy of the bottom clip, 2 image
    int bgMode = 0;
    float bgBlur = 50;  // % (also softens the image background)
    std::string bgImage;
    double workStart = 0, workEnd = 10;
    double time = 0;
    float shutterAngle = 180;
    int mbSamples = 8;
    bool motionBlur = true;
    uint64_t folder = 0;
    std::vector<LayerPtr> layers;  // [0] = top
    std::vector<Marker> markers;
    // ui
    float tlZoom = 0;  // pixels per second (0 = fit)
    double tlScroll = 0;

    Layer* find(uint64_t id);
    int indexOf(uint64_t id) const;
    Json save() const;
    void load(const Json& j);
};
using CompPtr = std::shared_ptr<Composition>;

enum class FootageKind : int { Video = 0, Image, Sequence, Audio };

struct Footage {
    uint64_t id = 0;
    std::string name, path;
    FootageKind kind = FootageKind::Video;
    int w = 0, h = 0;
    double fps = 30, duration = 0;
    bool hasVideo = false, hasAudio = false;
    std::string codec, audioCodec;
    int sampleRate = 0, channels = 0;
    int64_t bytes = 0;
    int seqStart = 0, seqCount = 0;
    std::string seqDir, seqPrefix, seqSuffix;
    int seqDigits = 4;
    uint64_t folder = 0;
    // runtime
    std::shared_ptr<MediaSource> media;
    std::shared_ptr<AudioData> audio;
    TexPtr thumb;
    bool missing = false;
    Json save() const;
    void load(const Json& j);
};
using FootagePtr = std::shared_ptr<Footage>;

struct Folder {
    uint64_t id = 0;
    std::string name = "Folder";
    uint64_t parent = 0;
    bool open = true;
};

struct OutputSettings {
    std::string path;
    int format = 0;    // see kFormatNames
    int encoder = 0;   // 0 CPU, 1 NVIDIA NVENC
    int quality = 18;  // CRF / CQ
    int bitrate = 0;   // Mbps, 0 = constant quality
    bool audio = true;
    int audioKbps = 320;
    int scalePct = 100;
    int range = 0;     // 0 work area, 1 whole comp
    Json save() const;
    void load(const Json& j);
};
static const char* const kFormatNames[] = {"H.264 (MP4)", "H.265 / HEVC (MP4)", "Apple ProRes 422 HQ (MOV)",
                                           "Apple ProRes 4444 + Alpha (MOV)", "PNG Sequence + Alpha", "VP9 + Alpha (WebM)"};
constexpr int kFormatCount = 6;

struct RenderItem {
    uint64_t id = 0;
    uint64_t comp = 0;
    bool enabled = true;
    OutputSettings out;
    int status = 0;  // 0 queued 1 rendering 2 done 3 failed 4 stopped
    float progress = 0;
    std::string message;
};

struct Project {
    std::string path;
    std::vector<FootagePtr> footage;
    std::vector<CompPtr> comps;
    std::vector<Folder> folders;
    std::vector<RenderItem> queue;
    double createdAt = 0;

    Footage* findFootage(uint64_t id) const;
    Composition* findComp(uint64_t id) const;
    std::string displayName() const;
    std::string uniqueCompName(const std::string& base) const;
    ImVec2 layerSize(const Layer& l, const Composition& c) const;
    // layer px -> comp px. For 3D layers this is the perspective (homography) through the active camera.
    Mat3 layerMatrix(const Composition& c, const Layer& l, double t, int depth = 0) const;
    // 3D world matrix: layer px (z = 0 plane) -> world
    Mat4 layerWorld(const Composition& c, const Layer& l, double t, int depth = 0) const;
    struct Camera3D {
        Vec3 pos;
        Mat4 rot;         // camera axes in world (columns: right, down, forward)
        float zoom = 0;   // px
        bool dof = false;
        float focus = 0, aperture = 0, blur = 0;
        const Layer* layer = nullptr;  // nullptr = the default comp camera
        Vec3 toCamera(const Vec3& w) const;  // world -> camera space (z = depth)
    };
    Camera3D camera(const Composition& c, double t) const;
    bool compUsesComp(uint64_t outer, uint64_t inner, int depth = 0) const;
    Json save() const;
    bool load(const Json& j, std::string& err);
};

LayerPtr cloneLayer(const Layer& l, bool newIds);
