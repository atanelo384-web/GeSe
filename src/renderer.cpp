#include "renderer.h"
#include "media.h"
#include "textrender.h"

static const char* kShaders = R"HLSL(
Texture2D gLayer : register(t0);
Texture2D gDst   : register(t1);
Texture2D gMatte : register(t2);
SamplerState sLin : register(s0);
SamplerState sPt  : register(s1);

cbuffer CompositeCB : register(b0) {
    float4 invA[16]; float4 invB[16];
    float2 compSize; float2 layerSize;
    float opacity; int blend; int samples; int matte;
    int maskCount; int adjust; float seed; float mbBlend;
    float4 mA[4]; float4 mB[4]; float4 mC[4];
    int curSample; int3 _pad1;
    // 3D: third row of the inverse homography, lighting
    float4 invC[16];
    int lit; int lightCount; int2 _pad2;
    float4 lw0; float4 lw1; float4 lw2;  // layer px (u, v, 1) -> world xyz
    float4 lightN;                        // layer normal in world space
    float4 camPos;
    float4 mat;                           // ambient, diffuse, specular, shininess
    float4 lPos[8];                       // xyz, kind (0 parallel 1 spot 2 point 3 ambient)
    float4 lDir[8];                       // direction the light travels, cos(outer cone)
    float4 lCol[8];                       // colour * intensity, cos(inner cone)
    float4 lFall[8];                      // radius, falloff distance
};
float3 shade(float3 rgb, float a, float2 lp) {
    float3 hp = float3(lp, 1);
    float3 wp = float3(dot(lw0.xyz, hp), dot(lw1.xyz, hp), dot(lw2.xyz, hp));
    float3 N = normalize(lightN.xyz);
    float3 V = normalize(camPos.xyz - wp);
    if (dot(N, V) < 0) N = -N;  // layers are two-sided
    float3 acc = 0, spec = 0;
    [loop] for (int k = 0; k < lightCount; k++) {
        int kind = (int)lPos[k].w;
        float3 col = lCol[k].rgb;
        if (kind == 3) { acc += col * mat.x; continue; }
        float3 L;
        float att = 1;
        if (kind == 0) L = -lDir[k].xyz;
        else {
            float3 d = lPos[k].xyz - wp;
            float dist = length(d);
            L = d / max(dist, 1e-3);
            if (lFall[k].y > 0) att = 1.0 - saturate((dist - lFall[k].x) / lFall[k].y);
            if (kind == 1) att *= smoothstep(lDir[k].w, lCol[k].w, dot(-L, lDir[k].xyz));
        }
        float ndl = max(dot(N, L), 0);
        acc += col * ndl * mat.y * att;
        float3 Hh = normalize(L + V);
        spec += col * pow(max(dot(N, Hh), 0), max(mat.w, 1.0)) * mat.z * att * step(0.0, ndl - 1e-4);
    }
    return rgb * acc + spec * a;
}

float lumv(float3 c) { return dot(c, float3(0.3, 0.59, 0.11)); }
float3 clipColor(float3 c) {
    float l = lumv(c); float n = min(c.r, min(c.g, c.b)); float x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-5);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-5);
    return c;
}
float3 setLum(float3 c, float l) { return clipColor(c + (l - lumv(c))); }
float satv(float3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
float3 setSat(float3 c, float s) {
    float mn = min(c.r, min(c.g, c.b)), mx = max(c.r, max(c.g, c.b));
    return mx > mn ? (c - mn) * s / (mx - mn) : float3(0, 0, 0);
}
float burn1(float s, float d)  { return d >= 1.0 ? 1.0 : (s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - d) / s)); }
float dodge1(float s, float d) { return d <= 0.0 ? 0.0 : (s >= 1.0 ? 1.0 : min(1.0, d / (1.0 - s))); }
float3 burn(float3 s, float3 d)  { return float3(burn1(s.r, d.r), burn1(s.g, d.g), burn1(s.b, d.b)); }
float3 dodge(float3 s, float3 d) { return float3(dodge1(s.r, d.r), dodge1(s.g, d.g), dodge1(s.b, d.b)); }
float3 multiply(float3 s, float3 d) { return s * d; }
float3 screen(float3 s, float3 d) { return s + d - s * d; }
float3 hardlight(float3 s, float3 d) { return lerp(multiply(2.0 * s, d), screen(2.0 * s - 1.0, d), step(0.5, s)); }
float3 softlight(float3 s, float3 d) {
    float3 D = lerp(((16.0 * d - 12.0) * d + 4.0) * d, sqrt(max(d, 0)), step(0.25, d));
    return lerp(d - (1.0 - 2.0 * s) * d * (1.0 - d), d + (2.0 * s - 1.0) * (D - d), step(0.5, s));
}
float3 blendFn(float3 s, float3 d, int m) {
    switch (m) {
    case 2:  return min(s, d);
    case 3:  return s * d;
    case 4:  return burn(s, d);
    case 5:  return max(s + d - 1.0, 0.0);
    case 6:  return max(s, d);
    case 7:  return screen(s, d);
    case 8:  return dodge(s, d);
    case 9:  return s + d;
    case 10: return hardlight(d, s);
    case 11: return softlight(s, d);
    case 12: return hardlight(s, d);
    case 13: return lerp(burn(2.0 * s, d), dodge(2.0 * (s - 0.5), d), step(0.5, s));
    case 14: return saturate(d + 2.0 * s - 1.0);
    case 15: return lerp(min(d, 2.0 * s), max(d, 2.0 * s - 1.0), step(0.5, s));
    case 16: return step(1.0, s + d);
    case 17: return abs(d - s);
    case 18: return s + d - 2.0 * s * d;
    case 19: return max(d - s, 0.0);
    case 20: return saturate(d / max(s, 1e-4));
    case 21: return setLum(setSat(s, satv(d)), lumv(d));
    case 22: return setLum(setSat(d, satv(s)), lumv(d));
    case 23: return setLum(s, lumv(d));
    case 24: return setLum(d, lumv(s));
    }
    return s;
}
float sdRoundBox(float2 p, float2 b, float r) {
    r = min(r, min(b.x, b.y));
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float sdEllipse(float2 p, float2 ab) {
    ab = max(ab, 1e-3);
    float k = length(p / ab);
    return (k - 1.0) * min(ab.x, ab.y);
}
float maskCoverage(float2 lp) {
    float a = (mC[0].y == 0.0) ? 0.0 : 1.0;
    [loop] for (int i = 0; i < maskCount; i++) {
        if (mC[i].w < 0.5) continue;
        float2 c = mA[i].xy, hs = mA[i].zw * 0.5 + mB[i].w;
        float d = mC[i].x < 0.5 ? sdRoundBox(lp - c, max(hs, 0.0), mB[i].y) : sdEllipse(lp - c, max(hs, 0.0));
        float f = max(mB[i].x, 1.0);
        float cv = 1.0 - smoothstep(-f * 0.5, f * 0.5, d);
        cv *= mB[i].z;
        if (mC[i].z > 0.5) cv = 1.0 - cv;
        if (mC[i].y < 0.5) a = a + cv - a * cv;
        else if (mC[i].y < 1.5) a = a * (1.0 - cv);
        else a = a * cv;
    }
    return a;
}
float hashp(float2 p) { float3 p3 = frac(float3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }

float4 psComposite(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 p = uv * compSize;
    float4 S = 0, Scur = 0; float cov = 0, covCur = 0;
    [loop] for (int i = 0; i < samples; i++) {
        float3 hp = float3(p, 1);
        float w = dot(invC[i].xyz, hp);  // 1 for 2D layers; 1/depth for 3D ones (<= 0: behind the camera)
        float2 lp = float2(dot(invA[i].xyz, hp), dot(invB[i].xyz, hp)) / (abs(w) > 1e-12 ? w : 1e-12);
        float2 luv = lp / layerSize;
        float ins = step(0.0, luv.x) * step(luv.x, 1.0) * step(0.0, luv.y) * step(luv.y, 1.0) * step(1e-12, w);
        float m = maskCount > 0 ? maskCoverage(lp) : 1.0;
        float4 s = 0;
        if (adjust == 0) s = gLayer.SampleLevel(sLin, luv, 0) * ins * m;
        if (lit != 0 && s.a > 0) s.rgb = shade(s.rgb, s.a, lp);
        S += s;
        cov += ins * m;
        if (i == curSample) { Scur = s; covCur = ins * m; }
    }
    float n = max(samples, 1);
    S /= n; cov /= n;
    // motion blur blend: 0 = sharp frame only, 1 = fully blurred copies
    if (samples > 1) { S = lerp(Scur, S, mbBlend); cov = lerp(covCur, cov, mbBlend); }
    float4 D = gDst.Load(int3(pos.xy, 0));
    float mf = 1.0;
    if (matte > 0) {
        float4 M = gMatte.Load(int3(pos.xy, 0));
        float3 mc = M.a > 1e-5 ? M.rgb / M.a : float3(0, 0, 0);
        mf = matte <= 2 ? M.a : lumv(mc) * M.a;
        if (matte == 2 || matte == 4) mf = 1.0 - mf;
    }
    if (adjust != 0) {
        float4 R = gLayer.Load(int3(pos.xy, 0));
        return lerp(D, R, saturate(cov * opacity * mf));
    }
    S *= opacity * mf;
    if (blend == 1) {
        float r = hashp(pos.xy + seed * 17.0);
        S = r < S.a ? float4(S.rgb / max(S.a, 1e-5), 1) : float4(0, 0, 0, 0);
    }
    if (blend <= 1) return S + D * (1.0 - S.a);
    float3 s = S.a > 1e-5 ? S.rgb / S.a : 0;
    float3 d = D.a > 1e-5 ? D.rgb / D.a : 0;
    float3 B = blendFn(s, d, blend);
    if (blend != 9) B = saturate(B);
    float3 rgb = (1.0 - D.a) * S.rgb + (1.0 - S.a) * D.rgb + S.a * D.a * B;
    return float4(rgb, S.a + D.a * (1.0 - S.a));
}

cbuffer FillCB : register(b0) { float4 fillColor; };
float4 psFill(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return float4(fillColor.rgb * fillColor.a, fillColor.a); }

float4 psCopy(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return gLayer.SampleLevel(sLin, uv, 0); }

cbuffer MixCB : register(b0) { float mixW; float3 _pm; };
float4 psMix(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return lerp(gLayer.SampleLevel(sLin, uv, 0), gDst.SampleLevel(sLin, uv, 0), mixW); }

cbuffer FinalCB : register(b0) { float4 bgColor; int mode; int channel; float2 _pf; };
float4 psFinal(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = gLayer.SampleLevel(sLin, uv, 0);
    if (channel == 4) return float4(c.aaa, 1);
    float3 straight = c.a > 1e-5 ? c.rgb / c.a : 0;
    if (channel >= 1 && channel <= 3) {
        float v = channel == 1 ? straight.r : (channel == 2 ? straight.g : straight.b);
        straight = v;
    }
    if (mode == 0) {
        float3 o = straight * c.a + bgColor.rgb * (1.0 - c.a);
        return float4(saturate(o), 1);
    }
    return float4(saturate(straight), saturate(c.a));
}
)HLSL";

struct CompositeCB {
    float invA[16][4];
    float invB[16][4];
    float compSize[2], layerSize[2];
    float opacity;
    int blend, samples, matte;
    int maskCount, adjust;
    float seed, mbBlend;
    float mA[4][4], mB[4][4], mC[4][4];
    int curSample, pad1[3];
    float invC[16][4];
    int lit, lightCount, pad2[2];
    float lw0[4], lw1[4], lw2[4];
    float lightN[4];
    float camPos[4];
    float mat[4];
    float lPos[8][4], lDir[8][4], lCol[8][4], lFall[8][4];
};
struct FxGlobals {
    float res[2], texel[2];
    float time, intensity, speed, phase;
    float rscale;
    int pass;
    float seed, fps;
    float compres[2];
    float aspect, frame;
    float lin, lout, pad[2];
};
struct FinalCB {
    float bg[4];
    int mode, channel;
    float pad[2];
};

double integrateSpeed(const Prop& sp, double lt) {
    if (!sp.animated) return lt * sp.v[0];
    if (lt <= 0) return lt * sp.f(0);
    int n = clampv((int)(lt * 60.0), 1, 8000);
    double dt = lt / n, acc = 0;
    float prev = sp.f(0);
    for (int i = 1; i <= n; i++) {
        float cur = sp.f(i * dt);
        acc += (prev + cur) * 0.5 * dt;
        prev = cur;
    }
    return acc;
}

bool Renderer::init(EffectRegistry* reg) {
    fx = reg;
    std::string err;
    psComposite = gfx::compilePS(kShaders, &err, "psComposite");
    if (!psComposite) logf("composite shader: %s", err.c_str());
    psCopy = gfx::compilePS(kShaders, &err, "psCopy");
    psFill = gfx::compilePS(kShaders, &err, "psFill");
    psFinal = gfx::compilePS(kShaders, &err, "psFinal");
    psMix = gfx::compilePS(kShaders, &err, "psMix");
    return psComposite && psCopy && psFill && psFinal && psMix;
}

void Renderer::clearCaches() { frames.clear(); bgImages.clear(); }

double Renderer::phaseFor(const EffectInst& e, double lt) const { return integrateSpeed(e.speed, lt); }

int Renderer::maxW(const Footage& f, float scale) const {
    if (scale >= 0.999f) return 0;
    return std::max(16, (int)std::lround(f.w * scale));
}

TexPtr Renderer::toWork(TexPtr src) {
    if (!src) return nullptr;
    if (src->fmt == gfx::kWorkFormat && src->rtv) return src;
    auto r = gfx::rt(src->w, src->h);
    gfx::pass(*r, psCopy.Get(), {src->srv.Get()}, nullptr, 0);
    return r;
}

TexPtr Renderer::layerSource(Project& p, const Composition& c, Layer& l, double t, const RenderParams& rp, int depth, float& rscale) {
    double lt = l.localTime(t);
    double srcT = l.sourceTime(t);
    // time operators from effects (Posterize Time / Freeze Frame)
    if (rp.effects && l.fxOn) {
        for (auto& e : l.effects) {
            if (!e.enabled) continue;
            const EffectDef* d = fx->find(e.defId);
            if (!d || d->timeop.empty()) continue;
            float k = e.intensity.f(lt) / 100.f;
            if (k <= 0.001f) continue;
            if (d->timeop == "posterize" && !e.params.empty()) {
                double rate = std::max(0.5f, e.params[0].f(lt));
                srcT = std::floor(srcT * rate + 1e-6) / rate;
            } else if (d->timeop == "freeze" && !e.params.empty()) {
                srcT = e.params[0].f(lt) * l.speed;
            }
        }
    }
    ImVec2 ls = p.layerSize(l, c);
    switch (l.type) {
    case LayerType::Footage: {
        Footage* f = p.findFootage(l.source);
        if (!f || !f->media) return nullptr;
        int64_t idx = f->media->indexAt(srcT);
        int mw = maxW(*f, rp.scale);
        auto key = std::make_pair(f->id, f->media->still ? (int64_t)0 : idx);
        tick++;
        auto it = frames.find(key);
        bool needW = it != frames.end() && mw > 0 && it->second.w < mw;
        bool needFull = it != frames.end() && mw == 0 && it->second.w < f->w;
        if (it == frames.end() || !it->second.exact || needW || needFull) {
            FramePtr fr;
            bool exact = true;
            if (rp.sync) fr = f->media->decodeSync(idx, mw);
            else fr = f->media->get(idx, mw, &exact);
            if (!fr) {
                pending = true;
                // fall back to whatever was last uploaded for this footage
                TexPtr best;
                uint64_t bu = 0;
                for (auto& kv : frames)
                    if (kv.first.first == f->id && kv.second.used >= bu) { bu = kv.second.used; best = kv.second.tex; }
                if (best) rscale = (float)best->w / std::max(1, f->w);
                return best;
            }
            if (!exact) pending = true;
            GpuFrame g;
            g.tex = gfx::createTexture(fr->w, fr->h, DXGI_FORMAT_R8G8B8A8_UNORM, false, fr->rgba.data(), fr->w * 4);
            g.idx = fr->idx;
            g.w = fr->w;
            g.used = tick;
            g.exact = exact;
            frames[key] = g;
            if (frames.size() > 40) {
                auto oldest = frames.begin();
                for (auto i2 = frames.begin(); i2 != frames.end(); ++i2) if (i2->second.used < oldest->second.used) oldest = i2;
                if (oldest->first != key) frames.erase(oldest);
            }
            it = frames.find(key);
        }
        it->second.used = tick;
        rscale = (float)it->second.w / std::max(1, f->w);
        return it->second.tex;
    }
    case LayerType::Solid: {
        int w = std::max(1, (int)std::lround(ls.x * rp.scale)), h = std::max(1, (int)std::lround(ls.y * rp.scale));
        auto r = gfx::rt(w, h);
        gfx::pass(*r, psFill.Get(), {}, l.solidColor, sizeof(l.solidColor));
        rscale = rp.scale;
        return r;
    }
    case LayerType::Text: {
        TextStyle s;
        s.text = l.text.text;
        s.font = l.text.font;
        s.bold = l.text.bold;
        s.italic = l.text.italic;
        s.caps = l.text.allCaps;
        s.align = l.text.align;
        s.size = l.text.size.f(lt);
        s.tracking = l.text.tracking.f(lt);
        s.leading = l.text.leading.f(lt);
        s.strokeW = l.text.strokeWidth.f(lt);
        ImVec4 fc = l.text.fill.col(lt), sc = l.text.stroke.col(lt);
        memcpy(s.fill, &fc, sizeof(s.fill));
        memcpy(s.stroke, &sc, sizeof(s.stroke));
        int w = std::max(1, (int)std::lround(ls.x * rp.scale)), h = std::max(1, (int)std::lround(ls.y * rp.scale));
        rscale = rp.scale;
        return textr::render(s, w, h, rp.scale);
    }
    case LayerType::Precomp: {
        Composition* pc = p.findComp(l.source);
        if (!pc || depth > 12) return nullptr;
        rscale = rp.scale;
        return renderComp(p, *pc, srcT, rp, depth + 1);
    }
    default: return nullptr;
    }
}

TexPtr Renderer::applyEffects(const Composition& c, Layer& l, TexPtr src, double t, const RenderParams& rp, float rscale) {
    if (!src || !rp.effects || !l.fxOn || l.effects.empty()) return src;
    double lt = l.localTime(t);
    TexPtr cur = src;
    bool converted = false;
    for (auto& e : l.effects) {
        if (!e.enabled) continue;
        const EffectDef* d = fx->find(e.defId);
        if (!d || !d->ok || !d->ps) continue;
        float inten = e.intensity.f(lt) / 100.f;
        if (d->autoMix && inten <= 0.0001f) continue;
        if (!converted) { cur = toWork(cur); converted = true; }
        FxGlobals g = {};
        g.res[0] = (float)cur->w;
        g.res[1] = (float)cur->h;
        g.texel[0] = 1.f / cur->w;
        g.texel[1] = 1.f / cur->h;
        g.time = (float)lt;
        g.intensity = inten;
        g.speed = e.speed.f(lt);
        g.phase = d->animated ? (float)phaseFor(e, lt) : (float)lt;
        g.rscale = rscale;
        g.seed = (float)(e.id % 997) * 0.731f;
        g.fps = (float)c.fps;
        g.compres[0] = (float)c.w * rp.scale;
        g.compres[1] = (float)c.h * rp.scale;
        g.aspect = (float)cur->w / cur->h;
        g.frame = (float)timeToFrame(lt, c.fps);
        g.lin = (float)(l.inPoint - l.startTime);
        g.lout = (float)(l.outPoint - l.startTime);
        float P[24][4] = {};
        for (size_t i = 0; i < e.params.size() && i < 24; i++) e.params[i].eval(lt, P[i]);
        TexPtr input = cur;
        for (int ps = 0; ps < d->passes; ps++) {
            g.pass = ps;
            auto out = gfx::rt(cur->w, cur->h);
            gfx::pass(*out, d->ps.Get(), {cur->srv.Get(), input->srv.Get()}, &g, sizeof(g), P, sizeof(P));
            lastPasses++;
            cur = out;
        }
    }
    return cur;
}

void Renderer::composite(TexPtr& acc, const Project& p, const Composition& c, Layer& l, TexPtr layerTex, double t,
                         const RenderParams& rp, Texture* matte, bool adjust, ImVec2 layerPx, const Lighting* lights) {
    CompositeCB cb = {};
    double lt = l.localTime(t);
    float s = rp.scale;
    int samples = 1;
    bool mb = rp.motionBlur && c.motionBlur && l.motionBlur && !adjust && l.mbBlur > 0.01f;
    if (mb) samples = clampv(l.mbSamples, 2, 16);
    if (mb && l.mbDir == 0 && samples % 2 == 0) samples = samples < 16 ? samples + 1 : 15;  // odd: one copy sits exactly on the frame
    // blur amount = time span (100% = 2 frames), direction both / left (behind) / right (ahead)
    double span = l.mbBlur / 50.0 / c.fps;
    cb.curSample = l.mbDir == 1 ? samples - 1 : (l.mbDir == 2 ? 0 : samples / 2);
    cb.mbBlend = clampv(l.mbBlend / 100.f, 0.f, 1.f);
    Mat3 S = Mat3::scale(s, s), Si = Mat3::scale(1.f / s, 1.f / s);
    for (int i = 0; i < samples; i++) {
        double st = t;
        if (samples > 1) {
            double u = (double)i / (samples - 1);
            double off = l.mbDir == 1 ? u - 1.0 : (l.mbDir == 2 ? u : u - 0.5);
            st = t + off * span;
        }
        Mat3 m = S * p.layerMatrix(c, l, st) * Si;
        Mat3 inv = m.inverse();
        cb.invA[i][0] = inv.m[0]; cb.invA[i][1] = inv.m[1]; cb.invA[i][2] = inv.m[2];
        cb.invB[i][0] = inv.m[3]; cb.invB[i][1] = inv.m[4]; cb.invB[i][2] = inv.m[5];
        cb.invC[i][0] = inv.m[6]; cb.invC[i][1] = inv.m[7]; cb.invC[i][2] = inv.m[8];
    }
    // lights on a 3D layer (no lights in the comp = unlit)
    if (lights && lights->count > 0 && l.threeD && l.acceptsLights && !adjust) {
        Mat4 W = p.layerWorld(c, l, t) * Mat4::scale(1.f / s, 1.f / s, 1);  // the layer texture is at render scale
        cb.lit = 1;
        cb.lightCount = lights->count;
        float r0[4] = {W.m[0], W.m[1], W.m[3], 0}, r1[4] = {W.m[4], W.m[5], W.m[7], 0}, r2[4] = {W.m[8], W.m[9], W.m[11], 0};
        memcpy(cb.lw0, r0, sizeof(r0));
        memcpy(cb.lw1, r1, sizeof(r1));
        memcpy(cb.lw2, r2, sizeof(r2));
        Vec3 n = W.col(0).cross(W.col(1)).norm();
        cb.lightN[0] = n.x; cb.lightN[1] = n.y; cb.lightN[2] = n.z;
        cb.camPos[0] = lights->camPos.x; cb.camPos[1] = lights->camPos.y; cb.camPos[2] = lights->camPos.z;
        cb.mat[0] = l.matAmbient.f(lt) / 100.f;
        cb.mat[1] = l.matDiffuse.f(lt) / 100.f;
        cb.mat[2] = l.matSpecular.f(lt) / 100.f;
        cb.mat[3] = 1.f + std::max(0.f, l.matShininess.f(lt)) * 1.27f;
        memcpy(cb.lPos, lights->pos, sizeof(cb.lPos));
        memcpy(cb.lDir, lights->dir, sizeof(cb.lDir));
        memcpy(cb.lCol, lights->col, sizeof(cb.lCol));
        memcpy(cb.lFall, lights->fall, sizeof(cb.lFall));
    }
    cb.samples = samples;
    cb.compSize[0] = (float)acc->w;
    cb.compSize[1] = (float)acc->h;
    cb.layerSize[0] = std::max(1.f, layerPx.x * s);
    cb.layerSize[1] = std::max(1.f, layerPx.y * s);
    cb.opacity = clampv(l.opacity.f(lt) / 100.f, 0.f, 1.f);
    cb.blend = l.blend;
    cb.matte = matte ? l.matte : 0;
    cb.adjust = adjust ? 1 : 0;
    cb.seed = (float)(l.id % 1000);
    int mc = 0;
    for (auto& m : l.masks) {
        if (mc >= 4) break;
        ImVec2 ctr = m.center.v2(lt), sz = m.size.v2(lt);
        cb.mA[mc][0] = ctr.x * s; cb.mA[mc][1] = ctr.y * s; cb.mA[mc][2] = sz.x * s; cb.mA[mc][3] = sz.y * s;
        cb.mB[mc][0] = m.feather.f(lt) * s;
        cb.mB[mc][1] = m.roundness.f(lt) * s;
        cb.mB[mc][2] = clampv(m.opacity.f(lt) / 100.f, 0.f, 1.f);
        cb.mB[mc][3] = m.expansion.f(lt) * s;
        cb.mC[mc][0] = (float)m.shape;
        cb.mC[mc][1] = (float)m.mode;
        cb.mC[mc][2] = m.inverted ? 1.f : 0.f;
        cb.mC[mc][3] = m.enabled ? 1.f : 0.f;
        mc++;
    }
    cb.maskCount = mc;
    auto out = gfx::rt(acc->w, acc->h);
    gfx::pass(*out, psComposite.Get(), {layerTex ? layerTex->srv.Get() : nullptr, acc->srv.Get(), matte ? matte->srv.Get() : nullptr},
              &cb, sizeof(cb));
    acc = out;
}

// canvas: a blurred, cover-filled copy of the bottom clip, or an image, under all layers
void Renderer::drawBackground(TexPtr& acc, Project& p, const Composition& c, double t, const RenderParams& rp, int depth) {
    TexPtr src;
    ImVec2 srcPx;
    float rscale = rp.scale;
    if (c.bgMode == 1) {
        for (int i = (int)c.layers.size() - 1; i >= 0 && !src; i--) {
            Layer& l = *c.layers[i];
            if (!l.visible || !l.activeAt(t) || l.type != LayerType::Footage) continue;
            Footage* f = p.findFootage(l.source);
            if (!f || !f->hasVideo) continue;
            src = layerSource(p, c, l, t, rp, depth, rscale);
            srcPx = ImVec2((float)f->w, (float)f->h);
        }
    } else if (c.bgMode == 2 && !c.bgImage.empty()) {
        auto it = bgImages.find(c.bgImage);
        if (it == bgImages.end()) {
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            TexPtr tex;
            if (gfx::loadImageFile(c.bgImage, px, w, h)) {
                for (size_t i = 0; i + 3 < px.size(); i += 4)  // sources are premultiplied
                    for (int k = 0; k < 3; k++) px[i + k] = (uint8_t)(px[i + k] * px[i + 3] / 255);
                tex = gfx::createTexture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false, px.data(), w * 4);
            }
            it = bgImages.emplace(c.bgImage, tex).first;
        }
        src = it->second;
        if (src) srcPx = ImVec2((float)src->w, (float)src->h);
    }
    if (!src || srcPx.x <= 0 || srcPx.y <= 0) return;
    // cover the frame (a little larger when blurred so the soft edges stay outside)
    Layer bg;
    float blur = c.bgMode == 1 ? std::max(5.f, c.bgBlur) : c.bgBlur;
    float k = std::max(c.w / srcPx.x, c.h / srcPx.y) * (blur > 0.5f ? 1.08f : 1.f);
    bg.anchor = Prop(2, srcPx.x * 0.5f, srcPx.y * 0.5f);
    bg.position = Prop(2, c.w * 0.5f, c.h * 0.5f);
    bg.scale = Prop(2, k * 100.f, k * 100.f);
    composite(acc, p, c, bg, src, t, rp, nullptr, false, srcPx);
    if (blur > 0.5f) {
        if (const EffectDef* d = fx->find("gese.blur.gaussian")) {
            Layer adj;
            adj.type = LayerType::Adjustment;
            adj.inPoint = -1e9;
            adj.outPoint = 1e9;
            EffectInst e = fx->instantiate(*d);
            if (!e.params.empty()) e.params[0] = Prop(1, blur * 1.6f);
            if (e.params.size() > 2) e.params[2] = Prop(1, 1);  // repeat edge pixels
            adj.effects.push_back(e);
            RenderParams r2 = rp;
            r2.effects = true;
            acc = applyEffects(c, adj, acc, t, r2, rp.scale);
        }
    }
}

// motion blur of the video itself: neighbouring source frames are averaged
// (Blur = how many frames, Direction = before / around / after, Times = copies, Blend = mix with the sharp frame)
TexPtr Renderer::contentBlur(Project& p, const Composition& c, Layer& l, double t, const RenderParams& rp, int depth, TexPtr cur, float rscale) {
    Footage* f = p.findFootage(l.source);
    if (!cur || !f || !f->media || f->media->still || f->fps <= 0 || !psMix) return cur;
    double frame = 1.0 / f->fps / std::max(0.01, fabs(l.speed));  // comp seconds per source frame
    double span = l.mbBlur / 100.0 * 6.0 * frame;                   // 100% = six frames
    if (span < frame * 0.5) return cur;
    int n = clampv(l.mbSamples, 2, 16);
    TexPtr acc;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        double u = (double)i / (n - 1);
        double off = l.mbDir == 1 ? u - 1.0 : (l.mbDir == 2 ? u : u - 0.5);
        double tt = clampv(t + off * span, l.inPoint, l.outPoint - 1e-6);
        float rs = rscale;
        TexPtr s = layerSource(p, c, l, tt, rp, depth, rs);
        if (!s || s->w != cur->w || s->h != cur->h) s = cur;
        cnt++;
        if (!acc) {
            acc = gfx::rt(cur->w, cur->h);
            gfx::pass(*acc, psCopy.Get(), {s->srv.Get()}, nullptr, 0);
            continue;
        }
        auto out = gfx::rt(cur->w, cur->h);
        float w[4] = {1.f / cnt, 0, 0, 0};  // running average
        gfx::pass(*out, psMix.Get(), {acc->srv.Get(), s->srv.Get()}, w, sizeof(w));
        acc = out;
    }
    auto out = gfx::rt(cur->w, cur->h);
    float b[4] = {clampv(l.mbBlend / 100.f, 0.f, 1.f), 0, 0, 0};
    gfx::pass(*out, psMix.Get(), {cur->srv.Get(), acc->srv.Get()}, b, sizeof(b));
    return out;
}

TexPtr Renderer::gaussian(const Composition& c, TexPtr src, float px, double t, const RenderParams& rp, float rscale) {
    const EffectDef* d = fx->find("gese.blur.gaussian");
    if (!d || px < 0.5f) return src;
    Layer tmp;
    tmp.type = LayerType::Adjustment;
    tmp.inPoint = -1e9;
    tmp.outPoint = 1e9;
    EffectInst e = fx->instantiate(*d);
    if (!e.params.empty()) e.params[0] = Prop(1, std::min(600.f, px));
    tmp.effects.push_back(e);
    RenderParams r2 = rp;
    r2.effects = true;
    return applyEffects(c, tmp, src, t, r2, rscale);
}

TexPtr Renderer::renderComp(Project& p, const Composition& c, double t, const RenderParams& rp, int depth) {
    if (depth == 0) lastPasses = 0;
    int cw = std::max(1, (int)std::lround(c.w * rp.scale)), ch = std::max(1, (int)std::lround(c.h * rp.scale));
    TexPtr acc = gfx::rt(cw, ch);
    gfx::clear(*acc, 0, 0, 0, 0);
    if (c.bgMode != 0) drawBackground(acc, p, c, t, rp, depth);
    bool anySolo = false;
    for (auto& l : c.layers) if (l->solo && l->visible) anySolo = true;
    int n = (int)c.layers.size();
    // drawing order: bottom to top; runs of 3D layers are drawn far to near
    std::vector<int> order;
    for (int i = n - 1; i >= 0; i--) order.push_back(i);
    bool any3D = false;
    for (auto& l : c.layers) if (l->threeD) any3D = true;
    Project::Camera3D cam;
    Lighting lights;
    auto depthOf = [&](int i) {
        Layer& l = *c.layers[i];
        ImVec2 sz = p.layerSize(l, c);
        return cam.toCamera(p.layerWorld(c, l, t).point(Vec3(sz.x * 0.5f, sz.y * 0.5f, 0))).z;
    };
    if (any3D) {
        cam = p.camera(c, t);
        auto sortable = [&](int i) {
            Layer& l = *c.layers[i];
            return l.threeD && l.matte == 0 && !(i + 1 < n && c.layers[i + 1]->matte != 0) && l.type != LayerType::Adjustment;
        };
        for (size_t k = 0; k < order.size();) {
            if (!sortable(order[k])) { k++; continue; }
            size_t e = k;
            while (e < order.size() && sortable(order[e])) e++;
            std::vector<std::pair<float, int>> d;
            for (size_t q = k; q < e; q++) d.push_back({depthOf(order[q]), order[q]});
            std::stable_sort(d.begin(), d.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
            for (size_t q = k; q < e; q++) order[q] = d[q - k].second;
            k = e;
        }
        // lights
        lights.camPos = cam.pos;
        for (auto& lp : c.layers) {
            const Layer& L = *lp;
            if (L.type != LayerType::Light || !L.visible || !L.activeAt(t) || lights.count >= 8) continue;
            double lt = L.localTime(t);
            ImVec2 pp = L.position.v2(lt), tg = L.target.v2(lt);
            Vec3 pos(pp.x, pp.y, L.posZ.f(lt));
            Vec3 dir = L.useTarget ? (Vec3(tg.x, tg.y, L.targetZ.f(lt)) - pos).norm()
                                   : (Mat4::rotZ(L.rotation.f(lt)) * Mat4::rotY(L.rotY.f(lt)) * Mat4::rotX(L.rotX.f(lt))).vec(Vec3(0, 0, 1)).norm();
            ImVec4 col = L.lightColor.col(lt);
            float k = std::max(0.f, L.lightIntensity.f(lt)) / 100.f;
            float outer = clampv(L.coneAngle.f(lt), 1.f, 179.f) * 0.5f * kPi / 180.f;
            float inner = outer * (1.f - clampv(L.coneFeather.f(lt), 0.f, 100.f) / 100.f);
            int i = lights.count++;
            float P[4] = {pos.x, pos.y, pos.z, (float)L.lightKind}, D[4] = {dir.x, dir.y, dir.z, cosf(outer)};
            float C[4] = {col.x * k, col.y * k, col.z * k, cosf(inner) + 1e-4f}, F[4] = {std::max(0.f, L.lightRadius.f(lt)), std::max(0.f, L.lightFalloff.f(lt)), 0, 0};
            memcpy(lights.pos[i], P, sizeof(P));
            memcpy(lights.dir[i], D, sizeof(D));
            memcpy(lights.col[i], C, sizeof(C));
            memcpy(lights.fall[i], F, sizeof(F));
        }
    }
    for (int i : order) {
        Layer& l = *c.layers[i];
        // a layer directly above a track-matted layer acts as its matte and is not rendered on its own
        bool isMatteSource = i + 1 < n && c.layers[i + 1]->matte != 0;
        if (isMatteSource) continue;
        if (!l.visible || !l.activeAt(t)) continue;
        if (anySolo && !l.solo) continue;
        if (l.type == LayerType::Null || l.type == LayerType::Camera || l.type == LayerType::Light) continue;
        if (l.type == LayerType::Adjustment) {
            if (!rp.effects || !l.fxOn || l.effects.empty()) continue;
            TexPtr res = applyEffects(c, l, acc, t, rp, rp.scale);
            if (res == acc) continue;
            composite(acc, p, c, l, res, t, rp, nullptr, true, ImVec2((float)c.w, (float)c.h));
            continue;
        }
        float rscale = rp.scale;
        TexPtr src = layerSource(p, c, l, t, rp, depth, rscale);
        if (!src) continue;
        if (l.type == LayerType::Footage && l.motionBlur && c.motionBlur && rp.motionBlur && l.mbBlur > 0.01f)
            src = contentBlur(p, c, l, t, rp, depth, src, rscale);
        src = applyEffects(c, l, src, t, rp, rscale);
        // camera depth of field: layers away from the focus distance get blurred
        if (l.threeD && cam.dof && cam.aperture > 0 && cam.blur > 0) {
            float z = depthOf(i);
            if (z > 1) {
                double lt = l.localTime(t);
                ImVec2 sc = l.scale.v2(lt);
                float screenBlur = cam.aperture * cam.blur / 100.f * fabsf(z - cam.focus) / z;
                float k = cam.zoom / z * std::max(0.01f, (fabsf(sc.x) + fabsf(sc.y)) * 0.005f);
                src = gaussian(c, src, screenBlur / std::max(k, 0.01f), t, rp, rscale);
            }
        }
        TexPtr matteTex;
        if (l.matte != 0 && i > 0) {
            Layer& ml = *c.layers[i - 1];
            if (ml.activeAt(t)) {
                float ms = rp.scale;
                TexPtr msrc = layerSource(p, c, ml, t, rp, depth, ms);
                if (msrc) {
                    msrc = applyEffects(c, ml, msrc, t, rp, ms);
                    matteTex = gfx::rt(cw, ch);
                    gfx::clear(*matteTex, 0, 0, 0, 0);
                    int keepBlend = ml.blend;
                    int keepMatte = ml.matte;
                    ml.blend = BM_Normal;
                    ml.matte = 0;
                    composite(matteTex, p, c, ml, msrc, t, rp, nullptr, false, p.layerSize(ml, c));
                    ml.blend = keepBlend;
                    ml.matte = keepMatte;
                }
            }
            if (!matteTex) {  // matte layer not present at this time: normal mattes show nothing
                if (l.matte == 1 || l.matte == 3) continue;
            }
        }
        composite(acc, p, c, l, src, t, rp, matteTex.get(), false, p.layerSize(l, c), any3D ? &lights : nullptr);
    }
    return acc;
}

void Renderer::finalize(Texture& src, Texture& dst, const float bg[4], int mode, int channel) {
    FinalCB cb = {};
    memcpy(cb.bg, bg, sizeof(cb.bg));
    cb.mode = mode;
    cb.channel = channel;
    gfx::pass(dst, psFinal.Get(), {src.srv.Get()}, &cb, sizeof(cb));
}

TexPtr Renderer::finalizeRGBA8(Texture& src, const float bg[4], bool keepAlpha) {
    auto out = gfx::rt(src.w, src.h, DXGI_FORMAT_R8G8B8A8_UNORM);
    finalize(src, *out, bg, keepAlpha ? 1 : 0, 0);
    return out;
}

void Renderer::prefetch(Project& p, const Composition& c, double t, float scale) {
    for (auto& l : c.layers) {
        if (l->type != LayerType::Footage || !l->visible) continue;
        if (t > l->outPoint || t + 2 < l->inPoint) continue;
        Footage* f = p.findFootage(l->source);
        if (!f || !f->media || f->media->still) continue;
        int64_t idx = f->media->indexAt(l->sourceTime(std::max(t, l->inPoint)));
        f->media->prefetch(idx, (int)std::max(8.0, f->fps * 0.75), maxW(*f, scale));
    }
}
