#include "media.h"
#include "gfx.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <objbase.h>

static std::atomic<int64_t> g_cacheBudget{1024LL * 1024 * 1024};
static std::atomic<int> g_openSources{0};
void MediaSource::setCacheBudget(int64_t b) { g_cacheBudget = std::max<int64_t>(b, 64LL * 1024 * 1024); }

bool isImageExt(const std::string& e) {
    static const char* k[] = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".gif", ".webp", ".heic", ".jxr", ".ico", ".dds"};
    for (auto* x : k) if (e == x) return true;
    return false;
}
bool isMediaExt(const std::string& e) {
    static const char* k[] = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".wmv", ".flv", ".mts", ".m2ts", ".ts", ".mxf",
                              ".mpg", ".mpeg", ".3gp", ".gifv", ".mp3", ".wav", ".aac", ".m4a", ".flac", ".ogg", ".opus", ".wma",
                              ".aif", ".aiff"};
    for (auto* x : k) if (e == x) return true;
    return isImageExt(e);
}

static std::string avErr(int e) {
    char b[256];
    av_strerror(e, b, sizeof(b));
    return b;
}

// ---------------------------------------------------------------------------
bool probeMedia(const std::string& path, MediaInfo& info) {
    info = MediaInfo();
    std::string ext = fileExt(path);
    if (isImageExt(ext) && ext != ".gif") {
        std::vector<uint8_t> px;
        int w, h;
        if (!gfx::loadImageFile(path, px, w, h)) return false;
        info.ok = info.hasVideo = info.still = true;
        info.w = w;
        info.h = h;
        info.frames = 1;
        info.codec = toLower(ext.substr(1));
        return true;
    }
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return false; }
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (vs >= 0 && (fmt->streams[vs]->disposition & AV_DISPOSITION_ATTACHED_PIC)) vs = -1;
    if (vs >= 0) {
        AVStream* st = fmt->streams[vs];
        info.hasVideo = true;
        info.w = st->codecpar->width;
        info.h = st->codecpar->height;
        AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
        info.fps = fr.num && fr.den ? av_q2d(fr) : 30.0;
        if (info.fps <= 0 || info.fps > 1000) info.fps = 30;
        const AVCodecDescriptor* cd = avcodec_descriptor_get(st->codecpar->codec_id);
        info.codec = cd ? cd->name : "video";
        if (st->duration != AV_NOPTS_VALUE) info.duration = st->duration * av_q2d(st->time_base);
    }
    if (as >= 0) {
        AVStream* st = fmt->streams[as];
        info.hasAudio = true;
        info.sampleRate = st->codecpar->sample_rate;
        info.channels = st->codecpar->ch_layout.nb_channels;
        const AVCodecDescriptor* cd = avcodec_descriptor_get(st->codecpar->codec_id);
        info.audioCodec = cd ? cd->name : "audio";
        if (info.duration <= 0 && st->duration != AV_NOPTS_VALUE) info.duration = st->duration * av_q2d(st->time_base);
    }
    if (info.duration <= 0 && fmt->duration != AV_NOPTS_VALUE) info.duration = fmt->duration / (double)AV_TIME_BASE;
    if (info.hasVideo) {
        int64_t nb = fmt->streams[vs]->nb_frames;
        info.frames = nb > 0 ? nb : (int64_t)std::llround(info.duration * info.fps);
        if (info.frames <= 1 && info.duration <= 0) { info.still = true; info.frames = 1; }
        if (info.duration <= 0) info.duration = info.frames / info.fps;
    }
    info.ok = info.hasVideo || info.hasAudio;
    avformat_close_input(&fmt);
    return info.ok;
}

// ---------------------------------------------------------------------------
// FFmpeg sequential/seeking decoder
// ---------------------------------------------------------------------------
class FFDecoder : public FrameDecoder {
  public:
    explicit FFDecoder(const std::string& p) : path(p) {}
    ~FFDecoder() override { close(); }
    bool decode(int64_t idx, int maxW, VideoFrame& out) override {
        if (!fmt && !openFile()) return false;
        if (idx < 0) idx = 0;
        bool needSeek = !(idx >= nextIdx && idx < nextIdx + (int64_t)(fps * 2.5) + 2);
        if (needSeek) seekTo(idx);
        int guard = 0;
        while (guard++ < 20000) {
            int r = receive();
            if (r == 1) {
                int64_t fi = frameIndex(frame);
                nextIdx = fi + 1;
                if (fi >= idx || eof) {
                    convert(frame, maxW, out);
                    out.idx = fi;
                    av_frame_unref(frame);
                    return true;
                }
                lastGood = fi;
                av_frame_unref(frame);
                continue;
            }
            if (r < 0) {
                // end of stream: return last frame we could get by re-seeking slightly earlier
                if (idx > 0 && lastSeekRetry != idx) {
                    lastSeekRetry = idx;
                    int64_t back = std::max<int64_t>(0, idx - (int64_t)fps);
                    seekTo(back);
                    idx = std::max<int64_t>(back, std::min(idx, lastGood));
                    continue;
                }
                return false;
            }
        }
        return false;
    }

  private:
    std::string path;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    int sw = 0, sh = 0, sfmt = -1, dw = 0, dh = 0;
    int si = -1;
    double fps = 30, tb = 0, startSec = 0;
    int64_t nextIdx = 0, lastGood = 0, lastSeekRetry = -1;
    bool eof = false, flushed = false;

    bool openFile() {
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { fmt = nullptr; return false; }
        if (avformat_find_stream_info(fmt, nullptr) < 0) { close(); return false; }
        si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (si < 0) { close(); return false; }
        AVStream* st = fmt->streams[si];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) { close(); return false; }
        cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        cc->thread_count = 0;
        cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(cc, codec, nullptr) < 0) { close(); return false; }
        AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
        fps = fr.num && fr.den ? av_q2d(fr) : 30.0;
        if (fps <= 0 || fps > 1000) fps = 30;
        tb = av_q2d(st->time_base);
        startSec = st->start_time != AV_NOPTS_VALUE ? st->start_time * tb : 0.0;
        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        nextIdx = 0;
        return true;
    }
    void close() {
        if (sws) sws_freeContext(sws);
        sws = nullptr;
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (cc) avcodec_free_context(&cc);
        if (fmt) avformat_close_input(&fmt);
        fmt = nullptr;
    }
    int64_t frameIndex(AVFrame* f) {
        int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (pts == AV_NOPTS_VALUE) return nextIdx;
        return (int64_t)std::llround((pts * tb - startSec) * fps);
    }
    void seekTo(int64_t idx) {
        double sec = idx / fps + startSec;
        int64_t ts = (int64_t)(sec / tb);
        av_seek_frame(fmt, si, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(cc);
        eof = false;
        flushed = false;
        nextIdx = -1000000;  // unknown until first frame
    }
    // returns 1 frame ready, 0 need more, -1 end
    int receive() {
        while (true) {
            int r = avcodec_receive_frame(cc, frame);
            if (r == 0) return 1;
            if (r == AVERROR_EOF) { eof = true; return -1; }
            if (r != AVERROR(EAGAIN)) return -1;
            if (flushed) return -1;
            int rr = av_read_frame(fmt, pkt);
            if (rr < 0) {
                avcodec_send_packet(cc, nullptr);
                flushed = true;
                continue;
            }
            if (pkt->stream_index == si) avcodec_send_packet(cc, pkt);
            av_packet_unref(pkt);
        }
    }
    void convert(AVFrame* f, int maxW, VideoFrame& out) {
        int w = f->width, h = f->height;
        int ow = w, oh = h;
        if (maxW > 0 && maxW < w) {
            ow = maxW & ~1;
            oh = std::max(2, (int)std::lround((double)h * ow / w));
        }
        if (!sws || sw != w || sh != h || sfmt != f->format || dw != ow || dh != oh) {
            if (sws) sws_freeContext(sws);
            sws = sws_getContext(w, h, (AVPixelFormat)f->format, ow, oh, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            sw = w;
            sh = h;
            sfmt = f->format;
            dw = ow;
            dh = oh;
        }
        out.w = ow;
        out.h = oh;
        out.rgba.resize((size_t)ow * oh * 4);
        uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        int ls[4] = {ow * 4, 0, 0, 0};
        if (sws) sws_scale(sws, f->data, f->linesize, 0, h, dst, ls);
        bool alpha = f->format == AV_PIX_FMT_YUVA420P || f->format == AV_PIX_FMT_RGBA || f->format == AV_PIX_FMT_BGRA ||
                     f->format == AV_PIX_FMT_ARGB || f->format == AV_PIX_FMT_YUVA444P10LE || f->format == AV_PIX_FMT_YUVA444P ||
                     f->format == AV_PIX_FMT_YUVA422P || f->format == AV_PIX_FMT_PAL8 || f->format == AV_PIX_FMT_RGBA64LE;
        if (alpha) gfx::premultiply(out.rgba);
        else for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    }
};

static void scaleRGBA(const std::vector<uint8_t>& src, int w, int h, int maxW, VideoFrame& out) {
    if (maxW <= 0 || maxW >= w) {
        out.rgba = src;
        out.w = w;
        out.h = h;
        return;
    }
    int ow = maxW, oh = std::max(1, (int)std::lround((double)h * ow / w));
    out.w = ow;
    out.h = oh;
    out.rgba.resize((size_t)ow * oh * 4);
    SwsContext* s = sws_getContext(w, h, AV_PIX_FMT_RGBA, ow, oh, AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr);
    const uint8_t* sp[4] = {src.data(), nullptr, nullptr, nullptr};
    int sl[4] = {w * 4, 0, 0, 0};
    uint8_t* dp[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
    int dl[4] = {ow * 4, 0, 0, 0};
    sws_scale(s, sp, sl, 0, h, dp, dl);
    sws_freeContext(s);
}

class StillDecoder : public FrameDecoder {
  public:
    explicit StillDecoder(const std::string& p) : path(p) {}
    bool decode(int64_t, int maxW, VideoFrame& out) override {
        if (px.empty()) {
            if (!gfx::loadImageFile(path, px, w, h)) return false;
            gfx::premultiply(px);
        }
        scaleRGBA(px, w, h, maxW, out);
        out.idx = 0;
        return true;
    }

  private:
    std::string path;
    std::vector<uint8_t> px;
    int w = 0, h = 0;
};

class SeqDecoder : public FrameDecoder {
  public:
    explicit SeqDecoder(const Footage& f) : foot(f) {}
    bool decode(int64_t idx, int maxW, VideoFrame& out) override {
        idx = clampv<int64_t>(idx, 0, std::max(0, foot.seqCount - 1));
        std::vector<uint8_t> px;
        int w, h;
        if (!gfx::loadImageFile(sequenceFrameName(foot, idx), px, w, h)) return false;
        gfx::premultiply(px);
        scaleRGBA(px, w, h, maxW, out);
        out.idx = idx;
        return true;
    }

  private:
    Footage foot;
};

std::string sequenceFrameName(const Footage& f, int64_t idx) {
    char num[32];
    snprintf(num, sizeof(num), "%0*lld", f.seqDigits, (long long)(f.seqStart + idx));
    return joinPath(f.seqDir, f.seqPrefix + num + f.seqSuffix);
}

bool detectSequence(const std::string& path, Footage& f) {
    std::string name = fileName(path), ext = fileExt(path);
    std::string stem = name.substr(0, name.size() - ext.size());
    int e = (int)stem.size();
    int s = e;
    while (s > 0 && isdigit((unsigned char)stem[s - 1])) s--;
    if (s == e) return false;
    std::string prefix = stem.substr(0, s);
    int digits = e - s;
    std::string dir = parentDir(path);
    int minN = INT32_MAX, maxN = -1;
    for (auto& file : listFiles(dir, ext, false)) {
        std::string n = fileName(file);
        if (!startsWith(n, prefix) || n.size() != prefix.size() + digits + ext.size()) continue;
        std::string mid = n.substr(prefix.size(), digits);
        bool allDigits = std::all_of(mid.begin(), mid.end(), [](char c) { return isdigit((unsigned char)c) != 0; });
        if (!allDigits) continue;
        int v = atoi(mid.c_str());
        minN = std::min(minN, v);
        maxN = std::max(maxN, v);
    }
    if (maxN <= minN) return false;
    f.kind = FootageKind::Sequence;
    f.seqDir = dir;
    f.seqPrefix = prefix;
    f.seqSuffix = ext;
    f.seqDigits = digits;
    f.seqStart = minN;
    f.seqCount = maxN - minN + 1;
    f.duration = f.seqCount / f.fps;
    f.name = prefix + "[" + std::string(digits, '#') + "]" + ext;
    return true;
}

// ---------------------------------------------------------------------------
std::shared_ptr<MediaSource> MediaSource::open(const Footage& f) {
    if (!f.hasVideo) return nullptr;
    auto m = std::make_shared<MediaSource>();
    m->path = f.path;
    m->w = f.w;
    m->h = f.h;
    m->fps = f.fps > 0 ? f.fps : 30;
    if (f.kind == FootageKind::Image) {
        m->still = true;
        m->frameCount = 1;
        std::string p = f.path;
        m->makeDecoder = [p]() { return std::unique_ptr<FrameDecoder>(new StillDecoder(p)); };
    } else if (f.kind == FootageKind::Sequence) {
        m->frameCount = std::max(1, f.seqCount);
        Footage copy = f;
        copy.media.reset();
        copy.audio.reset();
        copy.thumb.reset();
        m->makeDecoder = [copy]() { return std::unique_ptr<FrameDecoder>(new SeqDecoder(copy)); };
    } else {
        m->frameCount = std::max<int64_t>(1, (int64_t)std::llround(f.duration * m->fps));
        std::string p = f.path;
        m->makeDecoder = [p]() { return std::unique_ptr<FrameDecoder>(new FFDecoder(p)); };
    }
    m->preview = m->makeDecoder();
    m->sync = m->makeDecoder();
    g_openSources++;
    MediaSource* raw = m.get();
    m->worker = std::thread([raw] { raw->run(); });
    return m;
}

MediaSource::~MediaSource() {
    {
        std::lock_guard<std::mutex> lk(mx);
        quit = true;
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    g_openSources--;
}

int64_t MediaSource::indexAt(double t) const {
    if (still) return 0;
    int64_t i = (int64_t)std::floor(t * fps + 1e-4);
    return clampv<int64_t>(i, 0, frameCount - 1);
}

FramePtr MediaSource::lookup(int64_t idx, int minW) {
    auto it = cache.find(idx);
    if (it != cache.end() && (minW <= 0 ? it->second->w >= w : it->second->w >= std::min(minW, w))) return it->second;
    return nullptr;
}

void MediaSource::insert(const FramePtr& f) {
    std::lock_guard<std::mutex> lk(mx);
    cache[f->idx] = f;
    lru.erase(std::remove(lru.begin(), lru.end(), f->idx), lru.end());
    lru.push_back(f->idx);
    int64_t frameBytes = (int64_t)f->w * f->h * 4;
    int64_t perSource = g_cacheBudget / std::max(1, g_openSources.load());
    size_t maxFrames = (size_t)std::max<int64_t>(6, perSource / std::max<int64_t>(1, frameBytes));
    while (cache.size() > maxFrames && !lru.empty()) {
        int64_t old = lru.front();
        lru.pop_front();
        if (old == want) { lru.push_back(old); if (lru.size() <= 1) break; continue; }
        cache.erase(old);
    }
}

FramePtr MediaSource::get(int64_t idx, int maxW, bool* exact) {
    if (still) idx = 0;
    idx = clampv<int64_t>(idx, 0, frameCount - 1);
    std::unique_lock<std::mutex> lk(mx);
    if (FramePtr f = lookup(idx, maxW)) {
        if (exact) *exact = true;
        return f;
    }
    want = idx;
    wantW = maxW;
    pendingWork = true;
    lk.unlock();
    cv.notify_all();
    lk.lock();
    if (exact) *exact = false;
    // nearest cached frame for a responsive preview
    FramePtr best;
    int64_t bestD = INT64_MAX;
    for (auto& kv : cache) {
        int64_t d = std::llabs(kv.first - idx);
        if (d < bestD) { bestD = d; best = kv.second; }
    }
    return best;
}

void MediaSource::prefetch(int64_t fromIdx, int count, int maxW) {
    if (still) return;
    std::lock_guard<std::mutex> lk(mx);
    prefFrom = fromIdx;
    prefCount = count;
    wantW = maxW;
    pendingWork = true;
    cv.notify_all();
}

FramePtr MediaSource::decodeSync(int64_t idx, int maxW) {
    if (still) idx = 0;
    idx = clampv<int64_t>(idx, 0, frameCount - 1);
    {
        std::lock_guard<std::mutex> lk(mx);
        if (FramePtr f = lookup(idx, maxW)) if (f->idx == idx) return f;
    }
    std::lock_guard<std::mutex> lk(syncMx);
    auto f = std::make_shared<VideoFrame>();
    if (!sync->decode(idx, maxW, *f)) return nullptr;
    return f;
}

void MediaSource::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        int64_t target = -1;
        int tw = 0;
        {
            std::unique_lock<std::mutex> lk(mx);
            auto pick = [&]() -> bool {
                if (quit) return true;
                if (want >= 0) {
                    if (!lookup(want, wantW)) { target = want; tw = wantW; return true; }
                    want = -1;
                }
                if (prefFrom >= 0) {
                    for (int i = 0; i < prefCount; i++) {
                        int64_t k = prefFrom + i;
                        if (k >= frameCount) break;
                        if (!lookup(k, wantW)) { target = k; tw = wantW; return true; }
                    }
                    prefFrom = -1;
                }
                pendingWork = false;
                return false;
            };
            cv.wait(lk, [&] { return pick(); });
            if (quit) break;
        }
        auto f = std::make_shared<VideoFrame>();
        if (preview->decode(target, tw, *f)) {
            if (f->idx != target) f->idx = target;  // treat nearest decodable frame as the requested one
            insert(f);
        } else {
            // undecodable frame: store a black frame so we do not spin
            f->w = std::max(2, tw > 0 ? std::min(tw, w) : w);
            f->h = std::max(2, (int)((double)h * f->w / std::max(1, w)));
            f->rgba.assign((size_t)f->w * f->h * 4, 0);
            f->idx = target;
            insert(f);
        }
        std::lock_guard<std::mutex> lk(mx);
        if (want == target) want = -1;
    }
    CoUninitialize();
}

// ---------------------------------------------------------------------------
std::shared_ptr<AudioData> AudioData::decodeAsync(const std::string& path) {
    auto a = std::make_shared<AudioData>();
    std::thread([a, path] {
        AVFormatContext* fmt = nullptr;
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { a->ready = true; return; }
        avformat_find_stream_info(fmt, nullptr);
        int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (si < 0) { avformat_close_input(&fmt); a->ready = true; return; }
        AVStream* st = fmt->streams[si];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext* cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        if (!codec || avcodec_open2(cc, codec, nullptr) < 0) {
            avcodec_free_context(&cc);
            avformat_close_input(&fmt);
            a->ready = true;
            return;
        }
        SwrContext* swr = nullptr;
        AVChannelLayout outL;
        av_channel_layout_default(&outL, 2);
        AVChannelLayout inL;
        if (cc->ch_layout.nb_channels > 0 && cc->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_copy(&inL, &cc->ch_layout);
        else av_channel_layout_default(&inL, std::max(1, cc->ch_layout.nb_channels));
        swr_alloc_set_opts2(&swr, &outL, AV_SAMPLE_FMT_FLT, kRate, &inL, cc->sample_fmt, cc->sample_rate, 0, nullptr);
        swr_init(swr);
        AVPacket* pkt = av_packet_alloc();
        AVFrame* fr = av_frame_alloc();
        std::vector<float> out;
        double startSec = st->start_time != AV_NOPTS_VALUE ? st->start_time * av_q2d(st->time_base) : 0;
        (void)startSec;
        auto drain = [&]() {
            while (avcodec_receive_frame(cc, fr) == 0) {
                int maxOut = swr_get_out_samples(swr, fr->nb_samples) + 32;
                size_t base = out.size();
                out.resize(base + (size_t)maxOut * 2);
                uint8_t* dst = (uint8_t*)(out.data() + base);
                int n = swr_convert(swr, &dst, maxOut, (const uint8_t**)fr->extended_data, fr->nb_samples);
                out.resize(base + (size_t)std::max(0, n) * 2);
                av_frame_unref(fr);
            }
        };
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == si) {
                avcodec_send_packet(cc, pkt);
                drain();
            }
            av_packet_unref(pkt);
        }
        avcodec_send_packet(cc, nullptr);
        drain();
        av_frame_free(&fr);
        av_packet_free(&pkt);
        swr_free(&swr);
        av_channel_layout_uninit(&inL);
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        size_t frames = out.size() / 2;
        std::vector<float> peaks((frames + kPeakBlock - 1) / kPeakBlock);
        for (size_t i = 0; i < peaks.size(); i++) {
            float m = 0;
            size_t e = std::min(frames, (i + 1) * (size_t)kPeakBlock);
            for (size_t k = i * kPeakBlock; k < e; k++) m = std::max(m, std::max(fabsf(out[k * 2]), fabsf(out[k * 2 + 1])));
            peaks[i] = m;
        }
        a->samples = std::move(out);
        a->peaks = std::move(peaks);
        a->ready = true;
    }).detach();
    return a;
}
