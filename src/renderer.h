// Composition renderer (GPU): layer sources -> effects -> masks/transform/motion blur -> blend.
#pragma once
#include "core.h"
#include "gfx.h"
#include "project.h"
#include "effects.h"

struct RenderParams {
    float scale = 1.0f;     // 1 = full, 0.5 = half ...
    bool sync = false;      // exact frames (export); otherwise async preview decoding
    bool effects = true;
    bool motionBlur = true;
};

// lights of a comp at one moment, packed for the composite shader
struct Lighting {
    int count = 0;
    float pos[8][4] = {}, dir[8][4] = {}, col[8][4] = {}, fall[8][4] = {};
    Vec3 camPos;
};

class Renderer {
  public:
    bool init(EffectRegistry* reg);
    // Returns premultiplied RGBA16F texture of size comp*scale (transparent background).
    TexPtr renderComp(Project& p, const Composition& c, double t, const RenderParams& rp, int depth = 0);
    // mode 0: composite over bg (opaque), 1: straight alpha (for checkerboard / alpha exports)
    // channel 0 RGB, 1 R, 2 G, 3 B, 4 Alpha
    void finalize(Texture& src, Texture& dst, const float bg[4], int mode, int channel = 0);
    TexPtr finalizeRGBA8(Texture& src, const float bg[4], bool keepAlpha);
    // true if the last render used placeholder frames (decoding still in progress)
    bool pending = false;
    void prefetch(Project& p, const Composition& c, double t, float scale);
    void clearCaches();
    double phaseFor(const EffectInst& e, double lt) const;
    int lastPasses = 0;

  private:
    EffectRegistry* fx = nullptr;
    ComPtr<ID3D11PixelShader> psComposite, psCopy, psFill, psFinal, psMix;
    TexPtr contentBlur(Project& p, const Composition& c, Layer& l, double t, const RenderParams& rp, int depth, TexPtr cur, float rscale);
    struct GpuFrame {
        TexPtr tex;
        int64_t idx = -1;
        int w = 0;
        uint64_t used = 0;
        bool exact = false;
    };
    std::map<std::pair<uint64_t, int64_t>, GpuFrame> frames;
    std::map<std::string, TexPtr> bgImages;
    uint64_t tick = 0;
    void drawBackground(TexPtr& acc, Project& p, const Composition& c, double t, const RenderParams& rp, int depth);
    TexPtr layerSource(Project& p, const Composition& c, Layer& l, double t, const RenderParams& rp, int depth, float& rscale);
    TexPtr applyEffects(const Composition& c, Layer& l, TexPtr src, double t, const RenderParams& rp, float rscale);
    TexPtr toWork(TexPtr src);
    void composite(TexPtr& acc, const Project& p, const Composition& c, Layer& l, TexPtr layerTex, double t,
                   const RenderParams& rp, Texture* matte, bool adjust, ImVec2 layerPx, const Lighting* lights = nullptr);
    TexPtr gaussian(const Composition& c, TexPtr src, float px, double t, const RenderParams& rp, float rscale);
    int maxW(const Footage& f, float scale) const;
};

double integrateSpeed(const Prop& speed, double lt);
