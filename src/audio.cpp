#include "audio.h"
#include "audiofx.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

static const CLSID kCLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID kIID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID kIID_IAudioClient = __uuidof(IAudioClient);
static const IID kIID_IAudioRenderClient = __uuidof(IAudioRenderClient);

static void collect(const Project& p, const Composition& c, double outerStart, double outerSpeed, double winIn, double winOut,
                    float extraDb, int depth, bool sync, uint64_t onlyLayer, std::vector<AudioClip>& out) {
    if (depth > 8) return;
    bool anySolo = false;
    for (auto& l : c.layers) if (l->solo && l->visible) anySolo = true;
    for (auto& lp : c.layers) {
        const Layer& l = *lp;
        if (onlyLayer) {
            if (l.id != onlyLayer) continue;
        } else if (!l.audioOn || (anySolo && !l.solo)) {
            continue;
        }
        // map layer timing into the outer timeline
        auto mapT = [&](double tn) { return outerStart + tn / outerSpeed; };
        double in = std::max(winIn, mapT(l.inPoint)), outT = std::min(winOut, mapT(l.outPoint));
        if (outT <= in) continue;
        if (l.type == LayerType::Footage) {
            Footage* f = p.findFootage(l.source);
            if (!f || !f->hasAudio || !f->audio) continue;
            AudioClip a;
            a.data = f->audio;
            a.start = mapT(l.startTime);
            a.in = in;
            a.out = outT;
            a.speed = l.speed * outerSpeed;
            a.reversed = l.reversed;
            a.volume = l.volume;
            a.volStart = a.start;
            a.extraDb = extraDb;
            // voice & speed: pitch apart from speed, speed without the pitch change, voice presets
            AudioFxParams fp;
            fp.tempo = l.keepPitch ? a.speed : 1.0;
            fp.semitones = l.pitch;
            fp.voice = l.voice;
            fp.denoise = l.denoise;
            fp.enhance = l.enhanceVoice;
            a.fadeIn = l.audioFadeIn;
            a.fadeOut = l.audioFadeOut;
            if (fp.active() && a.data->ready.load() && a.speed > 0) {
                // source seconds the clip plays, widened to 10 s steps so small trims don't re-process
                double s0 = (in - a.start) * a.speed, s1 = (outT - a.start) * a.speed;
                double srcA = std::max(0.0, std::floor((std::min(s0, s1) - 0.5) / 10.0) * 10.0);
                double srcB = std::min(a.data->duration(), std::ceil((std::max(s0, s1) + 0.5) / 10.0) * 10.0);
                if (auto pd = audioFxGet(a.data, srcA, srcB, fp, sync)) {
                    a.data = pd;
                    if (l.keepPitch) { a.dataOffset = srcA / a.speed; a.speed = 1; }
                    else a.dataOffset = srcA;
                }
            }
            out.push_back(a);
        } else if (l.type == LayerType::Precomp) {
            Composition* nc = p.findComp(l.source);
            if (!nc) continue;
            collect(p, *nc, mapT(l.startTime), l.speed * outerSpeed, in, outT, extraDb + l.volume.v[0], depth + 1, sync, 0, out);
        }
    }
}

std::vector<AudioClip> collectAudioClips(const Project& p, const Composition& c, bool sync, uint64_t onlyLayer) {
    std::vector<AudioClip> r;
    collect(p, c, 0, 1, -1e9, 1e9, 0, 0, sync, onlyLayer, r);
    return r;
}

void mixAudio(const std::vector<AudioClip>& clips, double t0, int frames, int rate, float* out, float master) {
    memset(out, 0, sizeof(float) * frames * 2);
    const int block = 64;
    for (auto& c : clips) {
        if (!c.data || !c.data->ready.load()) continue;
        const std::vector<float>& s = c.data->samples;
        int64_t total = (int64_t)s.size() / 2;
        if (total <= 0) continue;
        for (int b = 0; b < frames; b += block) {
            double tb = t0 + (double)b / rate;
            float db = c.volume.f(tb - c.volStart) + c.extraDb;
            float g = db <= -96.f ? 0.f : powf(10.f, db / 20.f) * master;
            int e = std::min(frames, b + block);
            for (int i = b; i < e; i++) {
                double ct = t0 + (double)i / rate;
                if (ct < c.in || ct >= c.out) continue;
                double tt = c.reversed ? (c.in + c.out - ct) : ct;
                double st = (tt - c.start) * c.speed - c.dataOffset;
                double pos = st * AudioData::kRate;
                if (pos < 0) continue;
                int64_t i0 = (int64_t)pos;
                if (i0 + 1 >= total) continue;
                float fr = (float)(pos - i0);
                float l = s[i0 * 2] + (s[i0 * 2 + 2] - s[i0 * 2]) * fr;
                float r = s[i0 * 2 + 1] + (s[i0 * 2 + 3] - s[i0 * 2 + 1]) * fr;
                float gf = g;
                if (c.fadeIn > 0 && ct - c.in < c.fadeIn) gf *= (float)((ct - c.in) / c.fadeIn);
                if (c.fadeOut > 0 && c.out - ct < c.fadeOut) gf *= (float)((c.out - ct) / c.fadeOut);
                out[i * 2] += l * gf;
                out[i * 2 + 1] += r * gf;
            }
        }
    }
}

bool AudioEngine::init() {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en))) return false;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { en->Release(); return false; }
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) deviceName = narrow(v.pwszVal);
        PropVariantClear(&v);
        ps->Release();
    }
    IAudioClient* ac = nullptr;
    HRESULT hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac);
    dev->Release();
    en->Release();
    if (FAILED(hr)) return false;
    WAVEFORMATEX* wf = nullptr;
    ac->GetMixFormat(&wf);
    bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                   (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT);
    if (!isFloat || wf->wBitsPerSample != 32) {
        logf("audio: unsupported mix format");
        CoTaskMemFree(wf);
        ac->Release();
        return false;
    }
    rate = (int)wf->nSamplesPerSec;
    channels = wf->nChannels;
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 600000, 0, wf, nullptr);
    CoTaskMemFree(wf);
    if (FAILED(hr)) { ac->Release(); return false; }
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ac->SetEventHandle(e);
    UINT32 bf = 0;
    ac->GetBufferSize(&bf);
    bufferFrames = bf;
    IAudioRenderClient* rc = nullptr;
    if (FAILED(ac->GetService(kIID_IAudioRenderClient, (void**)&rc))) { ac->Release(); CloseHandle(e); return false; }
    client = ac;
    render = rc;
    evt = e;
    ok = true;
    ac->Start();
    th = std::thread([this] { run(); });
    logf("audio: %s, %d Hz, %d ch", deviceName.c_str(), rate, channels);
    return true;
}

void AudioEngine::shutdown() {
    quit = true;
    if (evt) SetEvent((HANDLE)evt);
    if (th.joinable()) th.join();
    if (client) { ((IAudioClient*)client)->Stop(); ((IAudioClient*)client)->Release(); }
    if (render) ((IAudioRenderClient*)render)->Release();
    if (evt) CloseHandle((HANDLE)evt);
    client = render = evt = nullptr;
    ok = false;
}

void AudioEngine::setClips(std::vector<AudioClip> c) {
    std::lock_guard<std::mutex> lk(mx);
    clips = std::move(c);
}

void AudioEngine::play(double from, double a, double b, bool lp) {
    std::lock_guard<std::mutex> lk(mx);
    t0 = from;
    loopA = a;
    loopB = b;
    loop = lp;
    writtenAtStart = written.load();
    qpcStart = nowSec();
    playing = true;
}

void AudioEngine::stop() { playing = false; }

double AudioEngine::position() {
    std::lock_guard<std::mutex> lk(mx);
    double t;
    if (ok) {
        int64_t played = written.load() - writtenAtStart.load() - padding.load();
        t = t0 + std::max<int64_t>(0, played) / (double)rate;
    } else {
        t = t0 + (nowSec() - qpcStart);
    }
    if (loop && loopB > loopA && t >= loopB) t = loopA + fmod(t - loopA, loopB - loopA);
    return t;
}

void AudioEngine::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IAudioClient* ac = (IAudioClient*)client;
    IAudioRenderClient* rc = (IAudioRenderClient*)render;
    std::vector<float> mixbuf;
    while (!quit) {
        WaitForSingleObject((HANDLE)evt, 200);
        if (quit) break;
        UINT32 pad = 0;
        if (FAILED(ac->GetCurrentPadding(&pad))) continue;
        padding = pad;
        UINT32 avail = bufferFrames - pad;
        if (!avail) continue;
        BYTE* data = nullptr;
        if (FAILED(rc->GetBuffer(avail, &data))) continue;
        float* f = (float*)data;
        if (!playing || mute) {
            memset(f, 0, sizeof(float) * avail * channels);
            peakL = peakL * 0.8f;
            peakR = peakR * 0.8f;
        } else {
            mixbuf.resize((size_t)avail * 2);
            double startT;
            std::vector<AudioClip> local;
            {
                std::lock_guard<std::mutex> lk(mx);
                int64_t gen = written.load() - writtenAtStart.load();
                startT = t0 + gen / (double)rate;
                if (loop && loopB > loopA && startT >= loopB) startT = loopA + fmod(startT - loopA, loopB - loopA);
                local = clips;
            }
            mixAudio(local, startT, (int)avail, rate, mixbuf.data(), master.load());
            float pl = 0, pr = 0;
            for (UINT32 i = 0; i < avail; i++) {
                float l = clampv(mixbuf[i * 2], -1.f, 1.f), r = clampv(mixbuf[i * 2 + 1], -1.f, 1.f);
                pl = std::max(pl, fabsf(l));
                pr = std::max(pr, fabsf(r));
                for (int c = 0; c < channels; c++) f[i * channels + c] = c == 0 ? l : (c == 1 ? r : 0.f);
            }
            peakL = std::max(pl, peakL * 0.85f);
            peakR = std::max(pr, peakR * 0.85f);
        }
        rc->ReleaseBuffer(avail, 0);
        written += avail;
    }
    CoUninitialize();
}
