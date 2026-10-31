#include "audiofx.h"
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}
#include <condition_variable>
#include <deque>
#include <map>

const VoicePreset kVoicePresets[] = {
    {"None", 0, false, ""},
    {"Deep Voice", -5, false, ""},
    {"Lower Voice", -3, true, ""},
    {"Higher Voice", 4, true, ""},
    {"Chipmunk", 7, false, ""},
    {"Robot", 0, false, "afftfilt=real='hypot(re,im)*sin(0)':imag='hypot(re,im)*cos(0)':win_size=512:overlap=0.75"},
    {"Monster", -8, false, "lowpass=f=3500,aecho=0.8:0.7:22:0.35"},
    {"Echo", 0, false, "aecho=0.8:0.8:180|360:0.45|0.25"},
    {"Hall", 0, false, "aecho=0.8:0.85:43|71|97|131:0.35|0.28|0.2|0.14"},
    {"Telephone", 0, false, "highpass=f=320,lowpass=f=3200,acrusher=bits=8:mix=0.35:mode=log:aa=1,volume=1.6"},
    {"Megaphone", 0, false, "highpass=f=550,lowpass=f=3000,volume=3,asoftclip=type=tanh"},
    {"Radio", 0, false, "highpass=f=180,lowpass=f=4800,acompressor=threshold=0.08:ratio=6:makeup=3"},
    {"Underwater", -1, false, "lowpass=f=420,vibrato=f=1.2:d=0.35,aecho=0.8:0.7:35:0.3"},
    {"Chorus", 0, false, "chorus=0.6:0.9:50|60|40:0.4|0.32|0.3:0.25|0.4|0.3:2|2.3|1.3"},
    {"Vibrato", 0, false, "vibrato=f=6.5:d=0.55"},
    {"Alien", 3, false, "aphaser=type=t:speed=2:decay=0.6,vibrato=f=8:d=0.3"},
};
const int kVoicePresetCount = (int)(sizeof(kVoicePresets) / sizeof(kVoicePresets[0]));

bool AudioFxParams::active() const { return fabs(tempo - 1) > 1e-4 || fabsf(semitones) > 1e-3 || voice > 0 || denoise || enhance; }

static std::string averr(int e) {
    char b[128];
    av_strerror(e, b, sizeof(b));
    return b;
}

// run stereo 48 kHz float audio through an FFmpeg filter chain
static bool runGraph(const float* in, int64_t frames, const std::string& chain, std::vector<float>& out, std::string& err) {
    out.clear();
    AVFilterGraph* g = avfilter_graph_alloc();
    if (!g) { err = "out of memory"; return false; }
    AVFilterContext *src = nullptr, *sink = nullptr;
    int r = avfilter_graph_create_filter(&src, avfilter_get_by_name("abuffer"), "in",
                                         "time_base=1/48000:sample_rate=48000:sample_fmt=flt:channel_layout=stereo", nullptr, g);
    if (r >= 0) r = avfilter_graph_create_filter(&sink, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, g);
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    std::string desc = chain + (chain.empty() ? "" : ",") + "aformat=sample_fmts=flt:sample_rates=48000:channel_layouts=stereo";
    if (r >= 0) r = avfilter_graph_parse_ptr(g, desc.c_str(), &inputs, &outputs, nullptr);
    if (r >= 0) r = avfilter_graph_config(g, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (r < 0) {
        err = "audio filter " + chain + ": " + averr(r);
        avfilter_graph_free(&g);
        return false;
    }
    AVFrame* fr = av_frame_alloc();
    AVFrame* of = av_frame_alloc();
    out.reserve((size_t)(frames * 2 + 96000));
    auto drain = [&] {
        while (av_buffersink_get_frame(sink, of) >= 0) {
            const float* p = (const float*)of->data[0];
            out.insert(out.end(), p, p + (size_t)of->nb_samples * 2);
            av_frame_unref(of);
        }
    };
    const int chunk = 8192;
    for (int64_t pos = 0; pos < frames && r >= 0; pos += chunk) {
        int n = (int)std::min<int64_t>(chunk, frames - pos);
        fr->nb_samples = n;
        fr->format = AV_SAMPLE_FMT_FLT;
        fr->sample_rate = AudioData::kRate;
        av_channel_layout_default(&fr->ch_layout, 2);
        fr->pts = pos;
        r = av_frame_get_buffer(fr, 0);
        if (r < 0) break;
        memcpy(fr->data[0], in + pos * 2, sizeof(float) * n * 2);
        r = av_buffersrc_add_frame(src, fr);  // takes over the frame's buffers
        drain();
    }
    if (r >= 0) {
        r = av_buffersrc_add_frame(src, nullptr);  // EOF: flush the filters
        drain();
    }
    av_frame_free(&fr);
    av_frame_free(&of);
    avfilter_graph_free(&g);
    if (r < 0) { err = "audio filter: " + averr(r); return false; }
    return true;
}

// centre of energy around the loudest point (a stretched click is smeared over a few ms, its peak wanders)
double energyCenter(const std::vector<float>& s) {
    int64_t frames = (int64_t)s.size() / 2, best = -1;
    float bv = 0;
    for (int64_t i = 0; i < frames; i++)
        if (fabsf(s[i * 2]) > bv) { bv = fabsf(s[i * 2]); best = i; }
    if (best < 0) return -1;
    double sw = 0, sp = 0;
    for (int64_t i = std::max<int64_t>(0, best - 12000); i < std::min(frames, best + 12000); i++) {
        double e = (double)s[i * 2] * s[i * 2];
        sw += e;
        sp += e * i;
    }
    return sw > 0 ? sp / sw : (double)best;
}

// Rubber Band in FFmpeg runs in real-time mode and delays its output; measure the delay once per setting
// with a click so the processed voice stays in sync with the picture.
static int64_t rubberbandDelay(const std::string& rb, double tempo) {
    static std::mutex mx;
    static std::map<std::string, int64_t> cache;
    {
        std::lock_guard<std::mutex> lk(mx);
        auto it = cache.find(rb);
        if (it != cache.end()) return it->second;
    }
    const int64_t n = 96000, at = 24000;
    std::vector<float> imp((size_t)n * 2, 0.f);
    for (int k = 0; k < 4; k++) imp[(at + k) * 2] = imp[(at + k) * 2 + 1] = 0.9f - k * 0.2f;
    std::vector<float> o;
    std::string e;
    int64_t d = 0;
    if (runGraph(imp.data(), n, rb, o, e)) {
        double pos = energyCenter(o);
        if (pos >= 0) d = (int64_t)llround(pos - at / tempo);
        if (d < 0 || d > 24000) d = 0;
    }
    logf("audiofx: %s -> delay %lld samples", rb.c_str(), (long long)d);
    std::lock_guard<std::mutex> lk(mx);
    cache[rb] = d;
    return d;
}

bool audioFxRender(const float* in, int64_t frames, const AudioFxParams& p, std::vector<float>& out, std::string& err) {
    const VoicePreset& vp = kVoicePresets[clampv(p.voice, 0, kVoicePresetCount - 1)];
    float semis = p.semitones + vp.semitones;
    double tempo = clampv(p.tempo, 0.05, 20.0);
    std::vector<float> cur, clean;
    const float* data = in;
    int64_t n = frames;
    // clean-up first, so the voice effects work on the clear signal
    std::string pre;
    if (p.denoise) pre = "afftdn=nr=18:nf=-35:tn=1";
    if (p.enhance) pre += std::string(pre.empty() ? "" : ",") + "highpass=f=90,equalizer=f=3200:t=q:w=1.2:g=3,acompressor=threshold=0.12:ratio=3:attack=8:release=120:makeup=2";
    if (!pre.empty()) {
        if (!runGraph(in, frames, pre, clean, err)) return false;
        clean.resize((size_t)frames * 2, 0.f);
        data = clean.data();
    }
    if (fabs(tempo - 1) > 1e-4 || fabsf(semis) > 1e-3) {
        std::string rb = strf("rubberband=tempo=%.6f:pitch=%.6f:pitchq=quality:channels=together:formant=%s", tempo,
                              pow(2.0, semis / 12.0), vp.keepFormant ? "preserved" : "shifted");
        int64_t delay = rubberbandDelay(rb, tempo);
        // one second of silence pushes the last real samples out of the stretcher
        std::vector<float> padded(data, data + frames * 2);
        padded.resize((size_t)(frames + AudioData::kRate) * 2, 0.f);
        std::vector<float> o;
        if (!runGraph(padded.data(), frames + AudioData::kRate, rb, o, err)) return false;
        int64_t expect = (int64_t)llround(frames / tempo);
        cur.assign((size_t)expect * 2, 0.f);
        int64_t avail = (int64_t)o.size() / 2 - delay;
        if (avail > 0) memcpy(cur.data(), o.data() + delay * 2, sizeof(float) * 2 * std::min(avail, expect));
        data = cur.data();
        n = expect;
    }
    if (vp.filters && *vp.filters) {
        std::vector<float> o;
        if (!runGraph(data, n, vp.filters, o, err)) return false;
        o.resize((size_t)n * 2, 0.f);  // same length: an echo tail past the clip end is cut like in the timeline
        out = std::move(o);
    } else if (data == in) {
        out.assign(in, in + frames * 2);
    } else if (data == clean.data()) {
        out = std::move(clean);
    } else {
        out = std::move(cur);
    }
    return true;
}

// ---------------------------------------------------------------------------
// background cache: one processed buffer per (source, range, settings)
// ---------------------------------------------------------------------------
namespace {
struct Entry {
    std::weak_ptr<AudioData> src;
    double a = 0, b = 0;
    AudioFxParams p;
    std::shared_ptr<AudioData> data;
    bool ready = false;
    uint64_t lastUse = 0;
};
std::mutex g_mx;
std::condition_variable g_cv;
std::map<std::string, Entry> g_cache;
std::deque<std::string> g_queue;
bool g_workerStarted = false;
std::atomic<uint64_t> g_gen{1};
std::atomic<int> g_busy{0};
uint64_t g_tick = 0;

std::string keyOf(const AudioData* s, double a, double b, const AudioFxParams& p) {
    return strf("%p|%.3f|%.3f|%.6f|%.3f|%d|%d%d", (const void*)s, a, b, p.tempo, p.semitones, p.voice, (int)p.denoise, (int)p.enhance);
}

std::shared_ptr<AudioData> processRange(const AudioData& src, double a, double b, const AudioFxParams& p) {
    int64_t total = (int64_t)src.samples.size() / 2;
    int64_t i0 = clampv((int64_t)llround(a * AudioData::kRate), (int64_t)0, total);
    int64_t i1 = clampv((int64_t)llround(b * AudioData::kRate), i0, total);
    if (i1 <= i0) return nullptr;
    auto d = std::make_shared<AudioData>();
    std::string err;
    double t0 = nowSec();
    if (!audioFxRender(src.samples.data() + i0 * 2, i1 - i0, p, d->samples, err)) {
        logf("audiofx: %s", err.c_str());
        return nullptr;
    }
    d->ready = true;
    logf("audiofx: %.1f s processed in %.2f s", (i1 - i0) / (double)AudioData::kRate, nowSec() - t0);
    return d;
}

void evictLocked() {
    while (g_cache.size() > 24) {
        auto victim = g_cache.end();
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
            if (it->second.ready && (victim == g_cache.end() || it->second.lastUse < victim->second.lastUse)) victim = it;
        if (victim == g_cache.end()) break;
        g_cache.erase(victim);
    }
}

void workerLoop() {
    while (true) {
        std::string key;
        std::shared_ptr<AudioData> src;
        double a, b;
        AudioFxParams p;
        {
            std::unique_lock<std::mutex> lk(g_mx);
            g_cv.wait(lk, [] { return !g_queue.empty(); });
            key = g_queue.front();
            g_queue.pop_front();
            auto it = g_cache.find(key);
            if (it == g_cache.end() || it->second.ready) continue;
            src = it->second.src.lock();
            if (!src) { g_cache.erase(it); continue; }
            a = it->second.a;
            b = it->second.b;
            p = it->second.p;
            g_busy++;
        }
        auto d = processRange(*src, a, b, p);
        {
            std::lock_guard<std::mutex> lk(g_mx);
            auto it = g_cache.find(key);
            if (it != g_cache.end()) {
                it->second.data = d;
                it->second.ready = true;
            }
            g_busy--;
        }
        g_gen++;
    }
}
}  // namespace

uint64_t audioFxGeneration() { return g_gen.load(); }
bool audioFxBusy() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_busy.load() > 0 || !g_queue.empty();
}

std::shared_ptr<AudioData> audioFxGet(const std::shared_ptr<AudioData>& src, double srcA, double srcB, const AudioFxParams& p, bool sync) {
    if (!src || !src->ready.load() || !p.active()) return nullptr;
    std::string key = keyOf(src.get(), srcA, srcB, p);
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_cache.find(key);
        if (it != g_cache.end() && it->second.src.lock() != src) { g_cache.erase(it); it = g_cache.end(); }
        if (it != g_cache.end()) {
            it->second.lastUse = ++g_tick;
            if (it->second.ready) return it->second.data;  // nullptr if processing failed: plain audio plays
            if (!sync) return nullptr;
        } else if (!sync) {
            Entry e;
            e.src = src;
            e.a = srcA;
            e.b = srcB;
            e.p = p;
            e.lastUse = ++g_tick;
            g_cache[key] = e;
            g_queue.push_back(key);
            evictLocked();
            if (!g_workerStarted) {
                g_workerStarted = true;
                std::thread(workerLoop).detach();
            }
            g_cv.notify_one();
            return nullptr;
        }
    }
    auto d = processRange(*src, srcA, srcB, p);
    std::lock_guard<std::mutex> lk(g_mx);
    Entry& e = g_cache[key];
    e.src = src;
    e.a = srcA;
    e.b = srcB;
    e.p = p;
    e.data = d;
    e.ready = true;
    e.lastUse = ++g_tick;
    evictLocked();
    return d;
}

// ---------------------------------------------------------------------------
// audio files
// ---------------------------------------------------------------------------
static bool saveWav(const std::string& path, const float* s, int64_t frames, std::string& err) {
    FILE* f = _wfopen(widen(path).c_str(), L"wb");
    if (!f) { err = "cannot write " + path; return false; }
    uint32_t dataBytes = (uint32_t)std::min<int64_t>(frames * 4, 0xFFFFFFF0ll);
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);  // PCM
    u16(2);
    u32(AudioData::kRate);
    u32(AudioData::kRate * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::vector<int16_t> buf(65536);
    int64_t total = dataBytes / 2;
    for (int64_t i = 0; i < total; i += (int64_t)buf.size()) {
        int64_t n = std::min<int64_t>(buf.size(), total - i);
        for (int64_t k = 0; k < n; k++) buf[k] = (int16_t)lrintf(clampv(s[i + k], -1.f, 1.f) * 32767.f);
        fwrite(buf.data(), 2, (size_t)n, f);
    }
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) err = "disk write error";
    return ok;
}

bool saveAudioFile(const std::string& path, const float* s, int64_t frames, std::string& err) {
    std::string ext = toLower(fileExt(path));
    if (ext == ".wav" || ext.empty()) return saveWav(ext.empty() ? path + ".wav" : path, s, frames, err);
    bool mp3 = ext == ".mp3", flac = ext == ".flac";
    const AVCodec* codec = mp3 ? avcodec_find_encoder_by_name("libmp3lame") : flac ? avcodec_find_encoder(AV_CODEC_ID_FLAC) : avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec && mp3) codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!codec) { err = "no encoder for " + ext; return false; }
    AVFormatContext* fc = nullptr;
    if (avformat_alloc_output_context2(&fc, nullptr, nullptr, path.c_str()) < 0 || !fc) { err = "unsupported format " + ext; return false; }
    AVStream* st = avformat_new_stream(fc, nullptr);
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    cc->sample_rate = AudioData::kRate;
    av_channel_layout_default(&cc->ch_layout, 2);
    cc->sample_fmt = flac ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
    cc->bit_rate = mp3 ? 320000 : (flac ? 0 : 256000);
    cc->time_base = AVRational{1, AudioData::kRate};
    if (fc->oformat->flags & AVFMT_GLOBALHEADER) cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int r = avcodec_open2(cc, codec, nullptr);
    if (r >= 0) r = avcodec_parameters_from_context(st->codecpar, cc);
    st->time_base = cc->time_base;
    if (r >= 0 && !(fc->oformat->flags & AVFMT_NOFILE)) r = avio_open(&fc->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (r >= 0) r = avformat_write_header(fc, nullptr);
    AVFrame* fr = av_frame_alloc();
    AVPacket* pk = av_packet_alloc();
    auto drain = [&] {
        while (avcodec_receive_packet(cc, pk) == 0) {
            av_packet_rescale_ts(pk, cc->time_base, st->time_base);
            pk->stream_index = st->index;
            av_interleaved_write_frame(fc, pk);
        }
    };
    int fs = cc->frame_size > 0 ? cc->frame_size : 4096;
    for (int64_t pos = 0; pos < frames && r >= 0; pos += fs) {
        int n = (int)std::min<int64_t>(fs, frames - pos);
        fr->nb_samples = n;
        fr->format = cc->sample_fmt;
        fr->sample_rate = cc->sample_rate;
        av_channel_layout_copy(&fr->ch_layout, &cc->ch_layout);
        if ((r = av_frame_get_buffer(fr, 0)) < 0) break;
        const float* src = s + pos * 2;
        if (flac) {
            int16_t* d = (int16_t*)fr->data[0];
            for (int i = 0; i < n * 2; i++) d[i] = (int16_t)lrintf(clampv(src[i], -1.f, 1.f) * 32767.f);
        } else {
            float* L = (float*)fr->data[0];
            float* R = (float*)fr->data[1];
            for (int i = 0; i < n; i++) { L[i] = clampv(src[i * 2], -1.f, 1.f); R[i] = clampv(src[i * 2 + 1], -1.f, 1.f); }
        }
        fr->pts = pos;
        r = avcodec_send_frame(cc, fr);
        av_frame_unref(fr);
        drain();
    }
    if (r >= 0) {
        avcodec_send_frame(cc, nullptr);
        drain();
        av_write_trailer(fc);
    } else {
        err = "audio encoding failed: " + averr(r);
    }
    av_frame_free(&fr);
    av_packet_free(&pk);
    avcodec_free_context(&cc);
    if (fc->pb && !(fc->oformat->flags & AVFMT_NOFILE)) avio_closep(&fc->pb);
    avformat_free_context(fc);
    return r >= 0;
}
