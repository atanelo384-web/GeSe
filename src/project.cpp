#include "project.h"

// ---------------------------------------------------------------------------
// keyframe interpolation
// ---------------------------------------------------------------------------
static inline float bez(float s, float a, float b) {
    float is = 1 - s;
    return 3 * is * is * s * a + 3 * is * s * s * b + s * s * s;
}
static inline float bezd(float s, float a, float b) {
    float is = 1 - s;
    return 3 * is * is * a + 6 * is * s * (b - a) + 3 * s * s * (1 - b);
}
static float easeCurve(float u, float x1, float y1, float x2, float y2) {
    float s = u;
    for (int i = 0; i < 10; i++) {
        float x = bez(s, x1, x2) - u;
        if (fabsf(x) < 1e-5f) break;
        float d = bezd(s, x1, x2);
        if (fabsf(d) < 1e-6f) break;
        s = clampv(s - x / d, 0.f, 1.f);
    }
    // bisection refine for flat regions
    float lo = 0, hi = 1;
    if (fabsf(bez(s, x1, x2) - u) > 1e-3f) {
        for (int i = 0; i < 24; i++) {
            s = (lo + hi) * 0.5f;
            if (bez(s, x1, x2) < u) lo = s; else hi = s;
        }
    }
    return bez(s, y1, y2);
}

float curveValue(Curve type, const float bz[4], float u) {
    u = clampv(u, 0.f, 1.f);
    switch (type) {
    case Curve::Bounce: {  // bounce-out
        const float n = 7.5625f, d = 2.75f;
        if (u < 1 / d) return n * u * u;
        if (u < 2 / d) { u -= 1.5f / d; return n * u * u + 0.75f; }
        if (u < 2.5f / d) { u -= 2.25f / d; return n * u * u + 0.9375f; }
        u -= 2.625f / d;
        return n * u * u + 0.984375f;
    }
    case Curve::Elastic:  // elastic-out
        if (u <= 0 || u >= 1) return u;
        return powf(2.f, -10.f * u) * sinf((u * 10.f - 0.75f) * (2.f * kPi / 3.f)) + 1.f;
    default:
        return easeCurve(u, clampv(bz[0], 0.f, 1.f), bz[1], clampv(bz[2], 0.f, 1.f), bz[3]);
    }
}

void Prop::eval(double t, float out[4]) const {
    if (!animated || keys.empty()) { memcpy(out, v, sizeof(v)); return; }
    if (t <= keys.front().t) { memcpy(out, keys.front().v, sizeof(v)); return; }
    if (t >= keys.back().t) { memcpy(out, keys.back().v, sizeof(v)); return; }
    size_t i = 1;
    while (i < keys.size() && keys[i].t < t) i++;
    const Keyframe& a = keys[i - 1];
    const Keyframe& b = keys[i];
    if (stepped || a.out == Interp::Hold || b.t - a.t < 1e-9) { memcpy(out, a.v, sizeof(v)); return; }
    float u = (float)((t - a.t) / (b.t - a.t));
    if (a.curve != Curve::Classic) {
        u = curveValue(a.curve, a.bz, u);
    } else if (a.out == Interp::Ease || b.in == Interp::Ease) {
        float x1 = a.out == Interp::Ease ? a.infOut / 100.f : 1.f / 3.f;
        float y1 = a.out == Interp::Ease ? 0.f : 1.f / 3.f;
        float x2 = b.in == Interp::Ease ? 1.f - b.infIn / 100.f : 2.f / 3.f;
        float y2 = b.in == Interp::Ease ? 1.f : 2.f / 3.f;
        u = easeCurve(u, x1, y1, x2, y2);
    }
    for (int k = 0; k < 4; k++) out[k] = a.v[k] + (b.v[k] - a.v[k]) * u;
}

int Prop::keyAt(double t, double eps) const {
    for (size_t i = 0; i < keys.size(); i++)
        if (fabs(keys[i].t - t) <= eps) return (int)i;
    return -1;
}
void Prop::sortKeys() {
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
}
int Prop::addKey(double t, const float* val) {
    int k = keyAt(t, 1e-4);
    if (k >= 0) { memcpy(keys[k].v, val, sizeof(v)); return k; }
    Keyframe kf;
    kf.t = t;
    memcpy(kf.v, val, sizeof(v));
    if (stepped) kf.in = kf.out = Interp::Hold;
    keys.push_back(kf);
    sortKeys();
    return keyAt(t, 1e-4);
}
void Prop::set(double t, const float* val) {
    if (animated) addKey(t, val);
    else memcpy(v, val, sizeof(v));
}
void Prop::setAnimated(bool on, double t) {
    if (on == animated) return;
    if (on) {
        animated = true;
        keys.clear();
        addKey(t, v);
    } else {
        float cur[4];
        eval(t, cur);
        memcpy(v, cur, sizeof(v));
        animated = false;
        keys.clear();
    }
}
bool Prop::hasSelectedKeys() const {
    for (auto& k : keys) if (k.sel) return true;
    return false;
}

Json Prop::save() const {
    Json j = Json::object();
    Json vv = Json::array();
    for (int i = 0; i < dim; i++) vv.push(Json(v[i]));
    j["v"] = vv;
    if (animated) {
        Json ks = Json::array();
        for (auto& k : keys) {
            Json kj = Json::object();
            kj["t"] = k.t;
            Json kv = Json::array();
            for (int i = 0; i < dim; i++) kv.push(Json(k.v[i]));
            kj["v"] = kv;
            kj["i"] = (int)k.in;
            kj["o"] = (int)k.out;
            if (k.in == Interp::Ease) kj["ii"] = k.infIn;
            if (k.out == Interp::Ease) kj["io"] = k.infOut;
            if (k.curve != Curve::Classic) {
                kj["cv"] = (int)k.curve;
                Json bj = Json::array();
                for (int i = 0; i < 4; i++) bj.push(Json(k.bz[i]));
                kj["bz"] = bj;
            }
            ks.push(kj);
        }
        j["k"] = ks;
    }
    return j;
}
void Prop::load(const Json& j) {
    if (auto* vv = j.get("v"))
        for (size_t i = 0; i < vv->a.size() && i < 4; i++) v[i] = (float)vv->a[i].n;
    keys.clear();
    animated = false;
    if (auto* ks = j.get("k")) {
        animated = true;
        for (auto& kj : ks->a) {
            Keyframe k;
            k.t = kj.num("t");
            if (auto* kv = kj.get("v"))
                for (size_t i = 0; i < kv->a.size() && i < 4; i++) k.v[i] = (float)kv->a[i].n;
            k.in = (Interp)(int)kj.num("i");
            k.out = (Interp)(int)kj.num("o");
            k.infIn = (float)kj.num("ii", 33.333);
            k.infOut = (float)kj.num("io", 33.333);
            k.curve = (Curve)clampv((int)kj.num("cv", 0), 0, 3);
            if (auto* bj = kj.get("bz"))
                for (size_t i = 0; i < bj->a.size() && i < 4; i++) k.bz[i] = (float)bj->a[i].n;
            keys.push_back(k);
        }
        sortKeys();
        if (keys.empty()) animated = false;
    }
}

// ---------------------------------------------------------------------------
Json Mask::save() const {
    Json j = Json::object();
    j["id"] = id;
    j["name"] = name;
    j["shape"] = shape;
    j["mode"] = mode;
    j["inv"] = inverted;
    j["on"] = enabled;
    j["center"] = center.save();
    j["size"] = size.save();
    j["feather"] = feather.save();
    j["round"] = roundness.save();
    j["opacity"] = opacity.save();
    j["expand"] = expansion.save();
    return j;
}
void Mask::load(const Json& j) {
    id = j.id("id");
    if (!id) id = newId();
    name = j.str("name", "Mask");
    shape = (int)j.num("shape");
    mode = (int)j.num("mode");
    inverted = j.boolean("inv");
    enabled = j.boolean("on", true);
    if (auto* p = j.get("center")) center.load(*p);
    if (auto* p = j.get("size")) size.load(*p);
    if (auto* p = j.get("feather")) feather.load(*p);
    if (auto* p = j.get("round")) roundness.load(*p);
    if (auto* p = j.get("opacity")) opacity.load(*p);
    if (auto* p = j.get("expand")) expansion.load(*p);
}

Json EffectInst::save() const {
    Json j = Json::object();
    j["id"] = id;
    j["def"] = defId;
    j["name"] = name;
    j["on"] = enabled;
    j["open"] = expanded;
    j["intensity"] = intensity.save();
    j["speed"] = speed.save();
    Json ps = Json::object();
    for (size_t i = 0; i < params.size(); i++) {
        std::string n = i < paramNames.size() ? paramNames[i] : strf("p%d", (int)i);
        Json pj = params[i].save();
        pj["dim"] = params[i].dim;
        pj["step"] = params[i].stepped;
        ps[n] = pj;
    }
    j["params"] = ps;
    return j;
}
void EffectInst::load(const Json& j) {
    id = j.id("id");
    if (!id) id = newId();
    defId = j.str("def");
    name = j.str("name", defId);
    enabled = j.boolean("on", true);
    expanded = j.boolean("open", true);
    if (auto* p = j.get("intensity")) intensity.load(*p);
    if (auto* p = j.get("speed")) speed.load(*p);
    params.clear();
    paramNames.clear();
    if (auto* ps = j.get("params")) {
        for (auto& kv : ps->o) {
            Prop p;
            p.dim = (int)kv.second.num("dim", 1);
            p.stepped = kv.second.boolean("step");
            p.load(kv.second);
            params.push_back(p);
            paramNames.push_back(kv.first);
        }
    }
}

Json TextData::save() const {
    Json j = Json::object();
    j["text"] = text;
    j["font"] = font;
    j["bold"] = bold;
    j["italic"] = italic;
    j["caps"] = allCaps;
    j["align"] = align;
    j["size"] = size.save();
    j["fill"] = fill.save();
    j["stroke"] = stroke.save();
    j["strokeW"] = strokeWidth.save();
    j["tracking"] = tracking.save();
    j["leading"] = leading.save();
    return j;
}
void TextData::load(const Json& j) {
    text = j.str("text", "Text");
    font = j.str("font", "Arial");
    bold = j.boolean("bold", true);
    italic = j.boolean("italic");
    allCaps = j.boolean("caps");
    align = (int)j.num("align", 1);
    if (auto* p = j.get("size")) size.load(*p);
    if (auto* p = j.get("fill")) fill.load(*p);
    if (auto* p = j.get("stroke")) stroke.load(*p);
    if (auto* p = j.get("strokeW")) strokeWidth.load(*p);
    if (auto* p = j.get("tracking")) tracking.load(*p);
    if (auto* p = j.get("leading")) leading.load(*p);
}

Json Layer::save() const {
    Json j = Json::object();
    j["id"] = id;
    j["name"] = name;
    j["type"] = (int)type;
    j["source"] = source;
    j["start"] = startTime;
    j["in"] = inPoint;
    j["out"] = outPoint;
    j["speed"] = speed;
    j["reversed"] = reversed;
    j["visible"] = visible;
    j["audioOn"] = audioOn;
    j["solo"] = solo;
    j["locked"] = locked;
    j["shy"] = shy;
    j["fxOn"] = fxOn;
    j["mb"] = motionBlur;
    j["mbBlur"] = mbBlur;
    j["mbBlend"] = mbBlend;
    j["mbDir"] = mbDir;
    j["mbSamples"] = mbSamples;
    j["keepPitch"] = keepPitch;
    j["pitch"] = pitch;
    j["voice"] = voice;
    j["denoise"] = denoise;
    j["enhanceVoice"] = enhanceVoice;
    j["fadeIn"] = audioFadeIn;
    j["fadeOut"] = audioFadeOut;
    if (threeD || type == LayerType::Camera || type == LayerType::Light) {
        j["threeD"] = threeD;
        j["posZ"] = posZ.save();
        j["rotX"] = rotX.save();
        j["rotY"] = rotY.save();
        j["acceptsLights"] = acceptsLights;
        j["matAmbient"] = matAmbient.save();
        j["matDiffuse"] = matDiffuse.save();
        j["matSpecular"] = matSpecular.save();
        j["matShininess"] = matShininess.save();
    }
    if (type == LayerType::Camera || type == LayerType::Light) {
        j["useTarget"] = useTarget;
        j["target"] = target.save();
        j["targetZ"] = targetZ.save();
    }
    if (type == LayerType::Camera) {
        j["camZoom"] = camZoom.save();
        j["dof"] = dof;
        j["focusDist"] = focusDist.save();
        j["aperture"] = aperture.save();
        j["blurLevel"] = blurLevel.save();
    }
    if (type == LayerType::Light) {
        j["lightKind"] = lightKind;
        j["lightColor"] = lightColor.save();
        j["lightIntensity"] = lightIntensity.save();
        j["coneAngle"] = coneAngle.save();
        j["coneFeather"] = coneFeather.save();
        j["lightRadius"] = lightRadius.save();
        j["lightFalloff"] = lightFalloff.save();
    }
    j["blend"] = blend;
    j["matte"] = matte;
    j["parent"] = parent;
    j["anchor"] = anchor.save();
    j["position"] = position.save();
    j["scale"] = scale.save();
    j["rotation"] = rotation.save();
    j["opacity"] = opacity.save();
    j["volume"] = volume.save();
    Json fx = Json::array();
    for (auto& e : effects) fx.push(e.save());
    j["effects"] = fx;
    Json ms = Json::array();
    for (auto& m : masks) ms.push(m.save());
    j["masks"] = ms;
    if (type == LayerType::Text) j["text"] = text.save();
    if (type == LayerType::Solid) {
        Json c = Json::array();
        for (float f : solidColor) c.push(Json(f));
        j["solidColor"] = c;
        j["solidW"] = solidW;
        j["solidH"] = solidH;
    }
    return j;
}
void Layer::load(const Json& j) {
    id = j.id("id");
    if (!id) id = newId();
    name = j.str("name");
    type = (LayerType)(int)j.num("type");
    source = j.id("source");
    startTime = j.num("start");
    inPoint = j.num("in");
    outPoint = j.num("out", 10);
    speed = j.num("speed", 1);
    reversed = j.boolean("reversed");
    visible = j.boolean("visible", true);
    audioOn = j.boolean("audioOn", true);
    solo = j.boolean("solo");
    locked = j.boolean("locked");
    shy = j.boolean("shy");
    fxOn = j.boolean("fxOn", true);
    motionBlur = j.boolean("mb");
    mbBlur = (float)j.num("mbBlur", 30);
    mbBlend = (float)j.num("mbBlend", 100);
    mbDir = (int)j.num("mbDir", 0);
    mbSamples = clampv((int)j.num("mbSamples", 8), 2, 16);
    keepPitch = j.boolean("keepPitch");
    pitch = clampv((float)j.num("pitch", 0), -24.f, 24.f);
    voice = (int)j.num("voice", 0);
    denoise = j.boolean("denoise");
    enhanceVoice = j.boolean("enhanceVoice");
    audioFadeIn = std::max(0.f, (float)j.num("fadeIn", 0));
    audioFadeOut = std::max(0.f, (float)j.num("fadeOut", 0));
    threeD = j.boolean("threeD");
    auto ld = [&](const char* k, Prop& p) { if (auto* v = j.get(k)) p.load(*v); };
    ld("posZ", posZ);
    ld("rotX", rotX);
    ld("rotY", rotY);
    acceptsLights = j.boolean("acceptsLights", true);
    ld("matAmbient", matAmbient);
    ld("matDiffuse", matDiffuse);
    ld("matSpecular", matSpecular);
    ld("matShininess", matShininess);
    useTarget = j.boolean("useTarget", true);
    ld("target", target);
    ld("targetZ", targetZ);
    ld("camZoom", camZoom);
    dof = j.boolean("dof");
    ld("focusDist", focusDist);
    ld("aperture", aperture);
    ld("blurLevel", blurLevel);
    lightKind = clampv((int)j.num("lightKind", LK_Point), 0, 3);
    ld("lightColor", lightColor);
    ld("lightIntensity", lightIntensity);
    ld("coneAngle", coneAngle);
    ld("coneFeather", coneFeather);
    ld("lightRadius", lightRadius);
    ld("lightFalloff", lightFalloff);
    blend = (int)j.num("blend");
    matte = (int)j.num("matte");
    parent = j.id("parent");
    if (auto* p = j.get("anchor")) anchor.load(*p);
    if (auto* p = j.get("position")) position.load(*p);
    if (auto* p = j.get("scale")) scale.load(*p);
    if (auto* p = j.get("rotation")) rotation.load(*p);
    if (auto* p = j.get("opacity")) opacity.load(*p);
    if (auto* p = j.get("volume")) volume.load(*p);
    effects.clear();
    if (auto* fx = j.get("effects"))
        for (auto& e : fx->a) { EffectInst ei; ei.load(e); effects.push_back(ei); }
    masks.clear();
    if (auto* ms = j.get("masks"))
        for (auto& e : ms->a) { Mask m; m.load(e); masks.push_back(m); }
    if (auto* t = j.get("text")) text.load(*t);
    if (auto* c = j.get("solidColor"))
        for (size_t i = 0; i < c->a.size() && i < 4; i++) solidColor[i] = (float)c->a[i].n;
    solidW = (int)j.num("solidW", 1920);
    solidH = (int)j.num("solidH", 1080);
}

LayerPtr cloneLayer(const Layer& l, bool newIds) {
    auto c = std::make_shared<Layer>();
    c->load(l.save());
    if (newIds) {
        c->id = newId();
        for (auto& e : c->effects) e.id = newId();
        for (auto& m : c->masks) m.id = newId();
    }
    return c;
}

Layer* Composition::find(uint64_t lid) {
    for (auto& l : layers) if (l->id == lid) return l.get();
    return nullptr;
}
int Composition::indexOf(uint64_t lid) const {
    for (size_t i = 0; i < layers.size(); i++) if (layers[i]->id == lid) return (int)i;
    return -1;
}
Json Composition::save() const {
    Json j = Json::object();
    j["id"] = id;
    j["name"] = name;
    j["w"] = w;
    j["h"] = h;
    j["fps"] = fps;
    j["duration"] = duration;
    Json b = Json::array();
    for (float f : bg) b.push(Json(f));
    j["bg"] = b;
    j["bgMode"] = bgMode;
    j["bgBlur"] = bgBlur;
    j["bgImage"] = bgImage;
    j["workStart"] = workStart;
    j["workEnd"] = workEnd;
    j["time"] = time;
    j["shutter"] = shutterAngle;
    j["mbSamples"] = mbSamples;
    j["mb"] = motionBlur;
    j["folder"] = folder;
    Json ls = Json::array();
    for (auto& l : layers) ls.push(l->save());
    j["layers"] = ls;
    Json mk = Json::array();
    for (auto& m : markers) { Json mj = Json::object(); mj["t"] = m.t; mj["label"] = m.label; mk.push(mj); }
    j["markers"] = mk;
    return j;
}
void Composition::load(const Json& j) {
    id = j.id("id");
    name = j.str("name", "Comp");
    w = (int)j.num("w", 1920);
    h = (int)j.num("h", 1080);
    fps = j.num("fps", 30);
    duration = j.num("duration", 10);
    if (auto* b = j.get("bg"))
        for (size_t i = 0; i < b->a.size() && i < 4; i++) bg[i] = (float)b->a[i].n;
    bgMode = clampv((int)j.num("bgMode", 0), 0, 2);
    bgBlur = (float)j.num("bgBlur", 50);
    bgImage = j.str("bgImage");
    workStart = j.num("workStart", 0);
    workEnd = j.num("workEnd", duration);
    time = j.num("time", 0);
    shutterAngle = (float)j.num("shutter", 180);
    mbSamples = (int)j.num("mbSamples", 8);
    motionBlur = j.boolean("mb", true);
    folder = j.id("folder");
    layers.clear();
    if (auto* ls = j.get("layers"))
        for (auto& lj : ls->a) { auto l = std::make_shared<Layer>(); l->load(lj); layers.push_back(l); }
    markers.clear();
    if (auto* mk = j.get("markers"))
        for (auto& mj : mk->a) markers.push_back({mj.num("t"), mj.str("label")});
}

Json Footage::save() const {
    Json j = Json::object();
    j["id"] = id;
    j["name"] = name;
    j["path"] = path;
    j["kind"] = (int)kind;
    j["w"] = w;
    j["h"] = h;
    j["fps"] = fps;
    j["duration"] = duration;
    j["folder"] = folder;
    if (kind == FootageKind::Sequence) {
        j["seqDir"] = seqDir;
        j["seqPrefix"] = seqPrefix;
        j["seqSuffix"] = seqSuffix;
        j["seqStart"] = seqStart;
        j["seqCount"] = seqCount;
        j["seqDigits"] = seqDigits;
    }
    return j;
}
void Footage::load(const Json& j) {
    id = j.id("id");
    name = j.str("name");
    path = j.str("path");
    kind = (FootageKind)(int)j.num("kind");
    w = (int)j.num("w");
    h = (int)j.num("h");
    fps = j.num("fps", 30);
    duration = j.num("duration");
    folder = j.id("folder");
    seqDir = j.str("seqDir");
    seqPrefix = j.str("seqPrefix");
    seqSuffix = j.str("seqSuffix");
    seqStart = (int)j.num("seqStart");
    seqCount = (int)j.num("seqCount");
    seqDigits = (int)j.num("seqDigits", 4);
}

Json OutputSettings::save() const {
    Json j = Json::object();
    j["path"] = path;
    j["format"] = format;
    j["encoder"] = encoder;
    j["quality"] = quality;
    j["bitrate"] = bitrate;
    j["audio"] = audio;
    j["audioKbps"] = audioKbps;
    j["scale"] = scalePct;
    j["range"] = range;
    return j;
}
void OutputSettings::load(const Json& j) {
    path = j.str("path");
    format = (int)j.num("format");
    encoder = (int)j.num("encoder");
    quality = (int)j.num("quality", 18);
    bitrate = (int)j.num("bitrate");
    audio = j.boolean("audio", true);
    audioKbps = (int)j.num("audioKbps", 320);
    scalePct = (int)j.num("scale", 100);
    range = (int)j.num("range");
}

// ---------------------------------------------------------------------------
Footage* Project::findFootage(uint64_t fid) const {
    for (auto& f : footage) if (f->id == fid) return f.get();
    return nullptr;
}
Composition* Project::findComp(uint64_t cid) const {
    for (auto& c : comps) if (c->id == cid) return c.get();
    return nullptr;
}
std::string Project::displayName() const { return path.empty() ? std::string("Untitled Project.gese") : fileName(path); }
std::string Project::uniqueCompName(const std::string& base) const {
    for (int i = 1; i < 10000; i++) {
        std::string n = strf("%s %d", base.c_str(), i);
        bool used = false;
        for (auto& c : comps) if (c->name == n) used = true;
        if (!used) return n;
    }
    return base;
}
ImVec2 Project::layerSize(const Layer& l, const Composition& c) const {
    switch (l.type) {
    case LayerType::Footage: {
        Footage* f = findFootage(l.source);
        if (f && f->hasVideo) return ImVec2((float)f->w, (float)f->h);
        return ImVec2(0, 0);
    }
    case LayerType::Solid: return ImVec2((float)l.solidW, (float)l.solidH);
    case LayerType::Precomp: {
        Composition* pc = findComp(l.source);
        return pc ? ImVec2((float)pc->w, (float)pc->h) : ImVec2(0, 0);
    }
    case LayerType::Null: return ImVec2(100, 100);
    case LayerType::Camera:
    case LayerType::Light: return ImVec2(0, 0);
    default: return ImVec2((float)c.w, (float)c.h);
    }
}

// ---------------------------------------------------------------------------
// 3D: world matrices, the comp camera, perspective
// ---------------------------------------------------------------------------
static Mat4 rotXYZ(const Layer& l, double lt) {
    return Mat4::rotZ(l.rotation.f(lt)) * Mat4::rotY(l.rotY.f(lt)) * Mat4::rotX(l.rotX.f(lt));
}
Mat4 Project::layerWorld(const Composition& c, const Layer& l, double t, int depth) const {
    double lt = l.localTime(t);
    ImVec2 a = l.anchor.v2(lt), p = l.position.v2(lt), s = l.scale.v2(lt);
    Mat4 m;
    if (l.threeD)
        m = Mat4::translate(p.x, p.y, l.posZ.f(lt)) * rotXYZ(l, lt) * Mat4::scale(s.x / 100.f, s.y / 100.f, 1) * Mat4::translate(-a.x, -a.y, 0);
    else
        m = Mat4::from2D(Mat3::translate(p.x, p.y) * Mat3::rotate(l.rotation.f(lt)) * Mat3::scale(s.x / 100.f, s.y / 100.f) * Mat3::translate(-a.x, -a.y));
    if (l.parent && depth < 16)
        for (auto& pl : c.layers)
            if (pl->id == l.parent) return layerWorld(c, *pl, t, depth + 1) * m;
    return m;
}

Vec3 Project::Camera3D::toCamera(const Vec3& w) const {
    Vec3 d = w - pos;  // rot is orthonormal: inverse = transpose
    return Vec3(rot.col(0).dot(d), rot.col(1).dot(d), rot.col(2).dot(d));
}

// the topmost visible camera layer, or the default camera that shows z = 0 at 100%
Project::Camera3D Project::camera(const Composition& c, double t) const {
    Camera3D cam;
    for (auto& lp : c.layers) {
        const Layer& l = *lp;
        if (l.type != LayerType::Camera || !l.visible || !l.activeAt(t)) continue;
        double lt = l.localTime(t);
        ImVec2 p = l.position.v2(lt);
        cam.pos = Vec3(p.x, p.y, l.posZ.f(lt));
        Mat4 look;
        if (l.useTarget) {
            // look at the point of interest; "down" on screen is +y
            ImVec2 tg = l.target.v2(lt);
            Vec3 f = (Vec3(tg.x, tg.y, l.targetZ.f(lt)) - cam.pos).norm();
            Vec3 x = Vec3(0, 1, 0).cross(f);
            x = x.len() < 1e-5f ? Vec3(1, 0, 0) : x.norm();
            look = Mat4::basis(x, f.cross(x).norm(), f);
        }
        cam.rot = look * rotXYZ(l, lt);
        cam.zoom = std::max(1.f, l.camZoom.f(lt));
        cam.dof = l.dof;
        cam.focus = l.focusDist.f(lt);
        cam.aperture = std::max(0.f, l.aperture.f(lt));
        cam.blur = std::max(0.f, l.blurLevel.f(lt));
        cam.layer = &l;
        return cam;
    }
    cam.zoom = c.w * 50.f / 36.f;  // a 50 mm lens
    cam.pos = Vec3(c.w * 0.5f, c.h * 0.5f, -cam.zoom);
    return cam;
}

Mat3 Project::layerMatrix(const Composition& c, const Layer& l, double t, int depth) const {
    if (l.threeD) {
        // homography of the layer plane through the camera: (u, v, 1) -> (x * w, y * w, w), w = depth
        Mat4 W = layerWorld(c, l, t, depth);
        Camera3D cam = camera(c, t);
        Vec3 A = cam.toCamera(cam.pos + W.col(0)), B = cam.toCamera(cam.pos + W.col(1)), C = cam.toCamera(W.col(3));
        float cx = c.w * 0.5f, cy = c.h * 0.5f, z = cam.zoom;
        Mat3 h;
        h.m[0] = z * A.x + cx * A.z; h.m[1] = z * B.x + cx * B.z; h.m[2] = z * C.x + cx * C.z;
        h.m[3] = z * A.y + cy * A.z; h.m[4] = z * B.y + cy * B.z; h.m[5] = z * C.y + cy * C.z;
        h.m[6] = A.z; h.m[7] = B.z; h.m[8] = C.z;
        return h;
    }
    double lt = l.localTime(t);
    ImVec2 a = l.anchor.v2(lt), p = l.position.v2(lt), s = l.scale.v2(lt);
    float r = l.rotation.f(lt);
    Mat3 m = Mat3::translate(p.x, p.y) * Mat3::rotate(r) * Mat3::scale(s.x / 100.f, s.y / 100.f) * Mat3::translate(-a.x, -a.y);
    if (l.parent && depth < 16) {
        for (auto& pl : c.layers)
            if (pl->id == l.parent) return layerMatrix(c, *pl, t, depth + 1) * m;
    }
    return m;
}
bool Project::compUsesComp(uint64_t outer, uint64_t inner, int depth) const {
    if (outer == inner) return true;
    if (depth > 32) return true;
    Composition* c = findComp(outer);
    if (!c) return false;
    for (auto& l : c->layers)
        if (l->type == LayerType::Precomp && compUsesComp(l->source, inner, depth + 1)) return true;
    return false;
}

Json Project::save() const {
    Json j = Json::object();
    j["app"] = GESE_FULLNAME;
    j["format"] = 1;
    j["created"] = createdAt;
    Json fs = Json::array();
    for (auto& f : footage) fs.push(f->save());
    j["footage"] = fs;
    Json cs = Json::array();
    for (auto& c : comps) cs.push(c->save());
    j["comps"] = cs;
    Json fo = Json::array();
    for (auto& f : folders) {
        Json fj = Json::object();
        fj["id"] = f.id;
        fj["name"] = f.name;
        fj["parent"] = f.parent;
        fj["open"] = f.open;
        fo.push(fj);
    }
    j["folders"] = fo;
    Json q = Json::array();
    for (auto& r : queue) {
        Json rj = Json::object();
        rj["id"] = r.id;
        rj["comp"] = r.comp;
        rj["enabled"] = r.enabled;
        rj["out"] = r.out.save();
        q.push(rj);
    }
    j["queue"] = q;
    return j;
}

bool Project::load(const Json& j, std::string& err) {
    if (!j.isObj() || !j.get("comps")) { err = "Not a GeSe project file"; return false; }
    footage.clear();
    comps.clear();
    folders.clear();
    queue.clear();
    uint64_t maxId = 0;
    createdAt = j.num("created");
    if (auto* fs = j.get("footage"))
        for (auto& fj : fs->a) { auto f = std::make_shared<Footage>(); f->load(fj); maxId = std::max(maxId, f->id); footage.push_back(f); }
    if (auto* cs = j.get("comps"))
        for (auto& cj : cs->a) {
            auto c = std::make_shared<Composition>();
            c->load(cj);
            maxId = std::max(maxId, c->id);
            for (auto& l : c->layers) {
                maxId = std::max(maxId, l->id);
                for (auto& e : l->effects) maxId = std::max(maxId, e.id);
                for (auto& m : l->masks) maxId = std::max(maxId, m.id);
            }
            comps.push_back(c);
        }
    if (auto* fo = j.get("folders"))
        for (auto& fj : fo->a) {
            Folder f;
            f.id = fj.id("id");
            f.name = fj.str("name", "Folder");
            f.parent = fj.id("parent");
            f.open = fj.boolean("open", true);
            maxId = std::max(maxId, f.id);
            folders.push_back(f);
        }
    if (auto* q = j.get("queue"))
        for (auto& rj : q->a) {
            RenderItem r;
            r.id = rj.id("id");
            r.comp = rj.id("comp");
            r.enabled = rj.boolean("enabled", true);
            if (auto* o = rj.get("out")) r.out.load(*o);
            maxId = std::max(maxId, r.id);
            queue.push_back(r);
        }
    seedIds(maxId + 1);
    return true;
}
