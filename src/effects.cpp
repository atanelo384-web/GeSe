#include "effects.h"
#include "gfx.h"

const char* const kCategoryOrder[] = {"Blur & Sharpen", "Channel", "Color Correction", "Distort", "Generate", "Glitch",
                                      "Keying", "Light & Lens", "Motion", "Noise & Grain", "Perspective", "Stylize",
                                      "Time", "Transition", nullptr};

std::string EffectRegistry::prelude() {
    return R"HLSL(
Texture2D gSrc  : register(t0);
Texture2D gOrig : register(t1);
SamplerState gLin  : register(s0);
SamplerState gPt   : register(s1);
SamplerState gWrap : register(s2);
cbuffer GeSeGlobals : register(b0) {
    float2 RES; float2 TEXEL;
    float TIME; float INTENSITY; float SPEED; float PHASE;
    float RSCALE; int gPassIndex; float SEED; float FPS;
    float2 COMPRES; float ASPECT; float FRAME;
    float LIN; float LOUT; float2 _g1;
};
#define PASS gPassIndex
static const float PI  = 3.14159265;
// Transition helper: 0 = untouched, 1 = fully transitioned. Modes: Manual (Completion) | In | Out | In & Out
float transitionAmount(float completion, int tmode, float dur) {
    float c = completion / 100.0;
    float d = max(dur, 1e-3);
    if (tmode == 1) c = 1.0 - saturate((TIME - LIN) / d);
    else if (tmode == 2) c = saturate((TIME - (LOUT - d)) / d);
    else if (tmode == 3) c = max(1.0 - saturate((TIME - LIN) / d), saturate((TIME - (LOUT - d)) / d));
    return saturate(c);
}
static const float TAU = 6.28318531;

float4 unpremul(float4 c) { return c.a > 1e-5 ? float4(c.rgb / c.a, c.a) : float4(0, 0, 0, 0); }
float4 premul(float4 c)   { return float4(c.rgb * c.a, c.a); }
float  inside(float2 uv)  { float2 s = step(0.0, uv) * step(uv, 1.0); return s.x * s.y; }
float4 SRCP(float2 uv)    { return gSrc.SampleLevel(gLin, uv, 0) * inside(uv); }
float4 SRC(float2 uv)     { return unpremul(SRCP(uv)); }
float4 ORIGP(float2 uv)   { return gOrig.SampleLevel(gLin, uv, 0) * inside(uv); }
float4 ORIG(float2 uv)    { return unpremul(ORIGP(uv)); }
float2 edgeUV(float2 uv, int mode) {
    if (mode == 1) return frac(uv);
    if (mode == 2) { float2 f = fmod(abs(uv), 2.0); return float2(f.x > 1.0 ? 2.0 - f.x : f.x, f.y > 1.0 ? 2.0 - f.y : f.y); }
    if (mode == 3) return saturate(uv);
    return uv;
}
float4 SRCEP(float2 uv, int mode) { return mode == 0 ? SRCP(uv) : SRCP(edgeUV(uv, mode)); }
float4 SRCE(float2 uv, int mode)  { return unpremul(SRCEP(uv, mode)); }
float  PX(float v)   { return v * RSCALE; }
float2 PXUV(float2 v) { return v * RSCALE * TEXEL; }
float2 toPx(float2 uv) { return uv * RES; }
float2 toUV(float2 p)  { return p / RES; }
float2 rot2(float2 v, float a) { float c = cos(a), s = sin(a); return float2(c * v.x - s * v.y, s * v.x + c * v.y); }
float2 rotUV(float2 uv, float2 c, float a) { return toUV(rot2(toPx(uv - c), a)) + c; }
float  luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float  hash11(float p) { p = frac(p * 0.1031); p *= p + 33.33; p *= p + p; return frac(p); }
float  hash12(float2 p) { float3 p3 = frac(float3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
float2 hash22(float2 p) { float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.xx + p3.yz) * p3.zy); }
float  hash13(float3 p3) { p3 = frac(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return frac((p3.x + p3.y) * p3.z); }
float  vnoise(float2 p) {
    float2 i = floor(p), f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + float2(1, 0)), c = hash12(i + float2(0, 1)), d = hash12(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float  vnoise3(float3 p) {
    float3 i = floor(p), f = frac(p); float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i), n100 = hash13(i + float3(1,0,0)), n010 = hash13(i + float3(0,1,0)), n110 = hash13(i + float3(1,1,0));
    float n001 = hash13(i + float3(0,0,1)), n101 = hash13(i + float3(1,0,1)), n011 = hash13(i + float3(0,1,1)), n111 = hash13(i + float3(1,1,1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y), lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}
float  fbm(float2 p, int oct) { float s = 0, a = 0.5; [loop] for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; } return s; }
float  fbm3(float3 p, int oct) { float s = 0, a = 0.5; [loop] for (int i = 0; i < oct; i++) { s += a * vnoise3(p); p = p * 2.03 + 17.1; a *= 0.5; } return s; }
float  snoise1(float x) { float i = floor(x), f = frac(x); float u = f * f * (3.0 - 2.0 * f); return lerp(hash11(i), hash11(i + 1.0), u) * 2.0 - 1.0; }
float  wave(float x, int type) {
    float f = frac(x);
    if (type == 1) return 1.0 - 4.0 * abs(f - 0.5);
    if (type == 2) return f < 0.5 ? 1.0 : -1.0;
    if (type == 3) return f * 2.0 - 1.0;
    if (type == 4) return abs(sin(x * PI)) * 2.0 - 1.0;
    if (type == 5) return snoise1(x * 2.0);
    return sin(x * TAU);
}
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y); float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float3 hsv2rgb(float3 c) {
    float3 p = abs(frac(c.xxx + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(float3(1, 1, 1), saturate(p - 1.0), c.y);
}
float3 blendScreen(float3 a, float3 b)  { return 1.0 - (1.0 - a) * (1.0 - b); }
float3 blendOverlay(float3 a, float3 b) { return lerp(2.0 * a * b, 1.0 - 2.0 * (1.0 - a) * (1.0 - b), step(0.5, a)); }
float3 blendSoft(float3 a, float3 b)    { return lerp(2.0 * a * b + a * a * (1.0 - 2.0 * b), sqrt(max(a, 0)) * (2.0 * b - 1.0) + 2.0 * a * (1.0 - b), step(0.5, b)); }
float  gauss(float x, float s) { return exp(-(x * x) / (2.0 * s * s + 1e-6)); }
float4 blur1D(float2 uv, float2 dir, float radius) {
    float r = max(PX(radius), 0.0);
    if (r < 0.5) return SRCP(uv);
    int n = (int)clamp(r, 1.0, 40.0);
    float stepPx = r / n; float sigma = r * 0.5;
    float4 acc = 0; float wsum = 0;
    [loop] for (int i = -n; i <= n; i++) {
        float x = i * stepPx; float w = gauss(x, sigma);
        acc += SRCP(uv + dir * x * TEXEL) * w; wsum += w;
    }
    return acc / wsum;
}
float4 blur1DEdge(float2 uv, float2 dir, float radius, int edge) {
    float r = max(PX(radius), 0.0);
    if (r < 0.5) return SRCEP(uv, edge);
    int n = (int)clamp(r, 1.0, 40.0);
    float stepPx = r / n; float sigma = r * 0.5;
    float4 acc = 0; float wsum = 0;
    [loop] for (int i = -n; i <= n; i++) {
        float x = i * stepPx; float w = gauss(x, sigma);
        acc += SRCEP(uv + dir * x * TEXEL, edge) * w; wsum += w;
    }
    return acc / wsum;
}
)HLSL";
}

static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> r;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            size_t e = s.find('"', i + 1);
            if (e == std::string::npos) e = s.size();
            r.push_back(s.substr(i + 1, e - i - 1));
            i = e + 1;
        } else {
            size_t e = i;
            while (e < s.size() && !isspace((unsigned char)s[e])) e++;
            r.push_back(s.substr(i, e - i));
            i = e;
        }
    }
    return r;
}

int EffectDef::paramIndex(const std::string& n) const {
    for (size_t i = 0; i < params.size(); i++) if (params[i].name == n) return (int)i;
    return -1;
}

std::string EffectRegistry::builtinDir() const { return resPath("Effects"); }
std::string EffectRegistry::pluginDir() const { return joinPath(exeDir(), "plugins"); }
std::string EffectRegistry::userPluginDir() const { return joinPath(appDataDir(), "Plugins"); }

void EffectRegistry::beginLoad() {
    defs.clear();
    files.clear();
    pending.clear();
    for (auto& f : listFiles(builtinDir(), ".gsfx", true)) pending.push_back({f, false});
    makeDirs(userPluginDir());
    for (auto& f : listFiles(pluginDir(), ".gsfx", true)) pending.push_back({f, true});
    for (auto& f : listFiles(userPluginDir(), ".gsfx", true)) pending.push_back({f, true});
    std::reverse(pending.begin(), pending.end());
}

bool EffectRegistry::loadStep(std::string& currentFile) {
    if (pending.empty()) { rebuildCategories(); return false; }
    auto p = pending.back();
    pending.pop_back();
    currentFile = p.first;
    parseFile(p.first, p.second);
    if (pending.empty()) rebuildCategories();
    return !pending.empty();
}

void EffectRegistry::reload() {
    beginLoad();
    std::string f;
    while (loadStep(f)) {}
}

void EffectRegistry::parseFile(const std::string& path, bool plugin) {
    EffectFileInfo info;
    info.path = path;
    info.plugin = plugin;
    info.enabled = !(plugin && disabledPlugins.count(toLower(path)));
    std::string text;
    if (!readFile(path, text)) {
        info.errors = 1;
        info.firstError = "cannot read file";
        files.push_back(info);
        return;
    }
    if (!info.enabled) { files.push_back(info); return; }
    std::vector<std::string> lines = split(text, '\n');
    std::unique_ptr<EffectDef> cur;
    bool inCode = false;
    auto finish = [&]() {
        if (!cur) return;
        if (cur->id.empty()) {
            std::string id = (plugin ? "plugin." : "gese.") + toLower(cur->category + "." + cur->name);
            for (auto& c : id) if (c == ' ' || c == '&' || c == '/') c = '_';
            cur->id = id;
        }
        cur->file = path;
        cur->plugin = plugin;
        compile(*cur);
        info.effects++;
        if (!cur->ok) {
            info.errors++;
            if (info.firstError.empty()) info.firstError = cur->name + ": " + cur->error;
        }
        // a later definition with the same id overrides (plugins can replace built-ins)
        for (auto& d : defs)
            if (d->id == cur->id) { d = std::move(cur); return; }
        defs.push_back(std::move(cur));
    };
    for (auto& rawLine : lines) {
        std::string line = rawLine;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (inCode) {
            if (t == "@end") { inCode = false; continue; }
            cur->code += line + "\n";
            continue;
        }
        if (t.empty() || startsWith(t, "//") || startsWith(t, "#")) continue;
        if (t[0] != '@') continue;
        auto tok = tokenize(t);
        std::string dir = tok[0];
        if (dir == "@effect") {
            finish();
            cur = std::make_unique<EffectDef>();
            std::string nm;
            for (size_t i = 1; i < tok.size(); i++) nm += (i > 1 ? " " : "") + tok[i];
            cur->name = nm;
            cur->category = plugin ? "Plugins" : "Other";
            continue;
        }
        if (!cur) continue;
        if (dir == "@id" && tok.size() > 1) cur->id = tok[1];
        else if (dir == "@category") {
            std::string c;
            for (size_t i = 1; i < tok.size(); i++) c += (i > 1 ? " " : "") + tok[i];
            cur->category = c;
        } else if (dir == "@desc") {
            cur->description = trim(t.substr(5));
        } else if (dir == "@mix" && tok.size() > 1) cur->autoMix = tok[1] != "manual";
        else if (dir == "@speed") cur->animated = true;
        else if (dir == "@passes" && tok.size() > 1) cur->passes = clampv(atoi(tok[1].c_str()), 1, 8);
        else if (dir == "@timeop" && tok.size() > 1) cur->timeop = tok[1];
        else if (dir == "@code") { inCode = true; cur->code.clear(); }
        else if (dir == "@param" && tok.size() >= 4) {
            ParamDef p;
            std::string ty = tok[1];
            p.name = tok[2];
            p.label = tok[3];
            size_t k = 4;
            auto num = [&](float def) { return k < tok.size() ? (float)atof(tok[k++].c_str()) : def; };
            if (ty == "float" || ty == "angle") {
                p.type = ty == "angle" ? ParamType::Angle : ParamType::Float;
                p.def[0] = num(0);
                if (k + 1 < tok.size() + 1 && k < tok.size() && (isdigit((unsigned char)tok[k][0]) || tok[k][0] == '-' || tok[k][0] == '.')) {
                    p.minv = num(0);
                    p.maxv = num(100);
                    p.hasRange = true;
                }
                if (k < tok.size()) p.unit = tok[k++];
                if (p.type == ParamType::Angle) { p.unit = "°"; if (!p.hasRange) { p.minv = -3600; p.maxv = 3600; } }
            } else if (ty == "point") {
                p.type = ParamType::Point;
                p.def[0] = num(0.5f);
                p.def[1] = num(0.5f);
            } else if (ty == "color") {
                p.type = ParamType::Color;
                p.def[0] = num(1);
                p.def[1] = num(1);
                p.def[2] = num(1);
                p.def[3] = num(1);
            } else if (ty == "bool") {
                p.type = ParamType::Bool;
                p.def[0] = num(0);
            } else if (ty == "choice") {
                p.type = ParamType::Choice;
                p.def[0] = num(0);
                if (k < tok.size()) p.choices = split(tok[k++], '|');
            }
            cur->params.push_back(p);
        }
    }
    finish();
    files.push_back(info);
}

void EffectRegistry::compile(EffectDef& d) {
    if (d.params.size() > 24) { d.error = "too many parameters (max 24)"; return; }
    if (trim(d.code).empty()) {
        d.ok = !d.timeop.empty();
        if (!d.ok) d.error = "no @code block";
        return;
    }
    std::string src = prelude();
    src += "cbuffer GeSeParams : register(b1) { float4 _P[24]; };\n";
    for (size_t i = 0; i < d.params.size(); i++) {
        auto& p = d.params[i];
        const char* sw = "x";
        std::string expr;
        switch (p.type) {
        case ParamType::Point: expr = strf("(_P[%d].xy)", (int)i); break;
        case ParamType::Color: expr = strf("(_P[%d])", (int)i); break;
        case ParamType::Bool: expr = strf("(_P[%d].x > 0.5)", (int)i); break;
        case ParamType::Choice: expr = strf("((int)(_P[%d].x + 0.5))", (int)i); break;
        default: expr = strf("(_P[%d].%s)", (int)i, sw);
        }
        src += "#define " + p.name + " " + expr + "\n";
    }
    src += strf("#define NPASSES %d\n#define AUTOMIX %d\n", d.passes, d.autoMix ? 1 : 0);
    src += "#line 1 \"" + d.name + "\"\n";
    src += d.code;
    src += R"HLSL(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 o = effect(uv);
    o.a = saturate(o.a);
    o.rgb = max(o.rgb, 0.0) * o.a;
#if AUTOMIX
    if (PASS == NPASSES - 1) { float4 b = gOrig.SampleLevel(gLin, uv, 0); o = lerp(b, o, saturate(INTENSITY)); }
#endif
    return o;
}
)HLSL";
    std::string err;
    d.ps = gfx::compilePS(src, &err);
    d.ok = d.ps != nullptr;
    if (!d.ok) {
        // keep only the first error line for the UI
        auto ls = split(err, '\n');
        d.error = ls.empty() ? err : ls[0];
        logf("effect '%s' failed: %s", d.name.c_str(), err.c_str());
    }
}

void EffectRegistry::rebuildCategories() {
    std::set<std::string> cats;
    for (auto& d : defs) cats.insert(d->category);
    categories.assign(cats.begin(), cats.end());
    std::sort(categories.begin(), categories.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
    std::stable_sort(defs.begin(), defs.end(), [](const std::unique_ptr<EffectDef>& a, const std::unique_ptr<EffectDef>& b) {
        if (a->category != b->category) return toLower(a->category) < toLower(b->category);
        return toLower(a->name) < toLower(b->name);
    });
}

const EffectDef* EffectRegistry::find(const std::string& id) const {
    for (auto& d : defs) if (d->id == id) return d.get();
    return nullptr;
}
std::vector<const EffectDef*> EffectRegistry::inCategory(const std::string& cat) const {
    std::vector<const EffectDef*> r;
    for (auto& d : defs) if (d->category == cat) r.push_back(d.get());
    return r;
}

EffectInst EffectRegistry::instantiate(const EffectDef& d) const {
    EffectInst e;
    e.id = newId();
    e.defId = d.id;
    e.name = d.name;
    for (auto& p : d.params) {
        Prop pr(p.dim(), p.def[0], p.def[1], p.def[2], p.def[3]);
        pr.stepped = p.type == ParamType::Bool || p.type == ParamType::Choice;
        e.params.push_back(pr);
        e.paramNames.push_back(p.name);
    }
    return e;
}

void EffectRegistry::sync(EffectInst& e) const {
    const EffectDef* d = find(e.defId);
    if (!d) return;
    std::vector<Prop> np;
    std::vector<std::string> nn;
    for (auto& p : d->params) {
        int found = -1;
        for (size_t i = 0; i < e.paramNames.size(); i++) if (e.paramNames[i] == p.name) found = (int)i;
        if (found >= 0 && found < (int)e.params.size()) {
            Prop pr = e.params[found];
            pr.dim = p.dim();
            pr.stepped = p.type == ParamType::Bool || p.type == ParamType::Choice;
            np.push_back(pr);
        } else {
            Prop pr(p.dim(), p.def[0], p.def[1], p.def[2], p.def[3]);
            pr.stepped = p.type == ParamType::Bool || p.type == ParamType::Choice;
            np.push_back(pr);
        }
        nn.push_back(p.name);
    }
    e.params = np;
    e.paramNames = nn;
}
