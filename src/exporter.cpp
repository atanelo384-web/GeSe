#include "exporter.h"
#include "audio.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
}

const ExportPreset kExportPresets[] = {
    {"YouTube 1080p — H.264 High Quality", 0, 0, 18, 0, 100, true, 320},
    {"YouTube 4K — H.265", 1, 0, 22, 0, 100, true, 320},
    {"Instagram / TikTok — H.264 (fast upload)", 0, 0, 21, 0, 100, true, 256},
    {"NVIDIA NVENC — H.264 (GPU, fast)", 0, 1, 19, 0, 100, true, 320},
    {"NVIDIA NVENC — H.265 (GPU, fast)", 1, 1, 21, 0, 100, true, 320},
    {"Master — Apple ProRes 422 HQ", 2, 0, 0, 0, 100, true, 320},
    {"Master with Alpha — ProRes 4444", 3, 0, 0, 0, 100, true, 320},
    {"PNG Sequence with Alpha", 4, 0, 0, 0, 100, false, 0},
    {"Web with Alpha — VP9 WebM", 5, 0, 30, 0, 100, true, 192},
    {"Draft — H.264 Half Resolution", 0, 0, 26, 0, 50, true, 192},
};
const int kExportPresetCount = (int)(sizeof(kExportPresets) / sizeof(kExportPresets[0]));

std::string formatExtension(int f) {
    switch (f) {
    case 2:
    case 3: return ".mov";
    case 4: return ".png";
    case 5: return ".webm";
    default: return ".mp4";
    }
}

static std::string avErrStr(int e) {
    char b[256];
    av_strerror(e, b, sizeof(b));
    return b;
}

struct Exporter::Job {
    Project* proj = nullptr;
    Composition* comp = nullptr;
    Renderer* rend = nullptr;
    uint64_t itemId = 0;
    OutputSettings out;
    double t0 = 0, fps = 30;
    int64_t frames = 0, cur = 0;
    int w = 0, h = 0;
    float scale = 1;
    bool alpha = false;
    double startWall = 0;
    // ffmpeg
    AVFormatContext* fmt = nullptr;
    AVCodecContext* vc = nullptr;
    AVCodecContext* ac = nullptr;
    AVStream* vs = nullptr;
    AVStream* as = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* vframe = nullptr;
    AVFrame* aframe = nullptr;
    AVPacket* pkt = nullptr;
    int64_t audioDone = 0;
    std::vector<AudioClip> clips;
    std::vector<uint8_t> rgba;
    std::string pngBase;
    std::string note;

    ~Job() {
        if (sws) sws_freeContext(sws);
        if (vframe) av_frame_free(&vframe);
        if (aframe) av_frame_free(&aframe);
        if (pkt) av_packet_free(&pkt);
        if (vc) avcodec_free_context(&vc);
        if (ac) avcodec_free_context(&ac);
        if (fmt) {
            if (!(fmt->oformat->flags & AVFMT_NOFILE) && fmt->pb) avio_closep(&fmt->pb);
            avformat_free_context(fmt);
        }
    }
    bool writePackets(AVCodecContext* cc, AVStream* st, std::string& err) {
        while (true) {
            int r = avcodec_receive_packet(cc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) { err = "encode: " + avErrStr(r); return false; }
            av_packet_rescale_ts(pkt, cc->time_base, st->time_base);
            pkt->stream_index = st->index;
            r = av_interleaved_write_frame(fmt, pkt);
            if (r < 0) { err = "write: " + avErrStr(r); return false; }
        }
    }
    bool encodeAudioUntil(double tEnd, std::string& err, bool flush) {
        if (!ac) return true;
        int fs = ac->frame_size > 0 ? ac->frame_size : 1024;
        std::vector<float> mix((size_t)fs * 2);
        int64_t target = (int64_t)(tEnd * ac->sample_rate);
        while (audioDone < target) {
            mixAudio(clips, t0 + (double)audioDone / ac->sample_rate, fs, ac->sample_rate, mix.data());
            av_frame_make_writable(aframe);
            aframe->nb_samples = fs;
            if (ac->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                float* l = (float*)aframe->data[0];
                float* r = (float*)aframe->data[1];
                for (int i = 0; i < fs; i++) { l[i] = clampv(mix[i * 2], -1.f, 1.f); r[i] = clampv(mix[i * 2 + 1], -1.f, 1.f); }
            } else if (ac->sample_fmt == AV_SAMPLE_FMT_FLT) {
                float* d = (float*)aframe->data[0];
                for (int i = 0; i < fs * 2; i++) d[i] = clampv(mix[i], -1.f, 1.f);
            } else {
                int16_t* d = (int16_t*)aframe->data[0];
                for (int i = 0; i < fs * 2; i++) d[i] = (int16_t)(clampv(mix[i], -1.f, 1.f) * 32767);
            }
            aframe->pts = audioDone;
            audioDone += fs;
            if (avcodec_send_frame(ac, aframe) < 0) { err = "audio encode failed"; return false; }
            if (!writePackets(ac, as, err)) return false;
        }
        if (flush) {
            avcodec_send_frame(ac, nullptr);
            return writePackets(ac, as, err);
        }
        return true;
    }
};

static std::vector<const AVCodec*> pickVideoCodecs(int format, int encoder) {
    std::vector<const char*> names;
    switch (format) {
    case 0: names = encoder == 1 ? std::vector<const char*>{"h264_nvenc", "h264_amf", "h264_qsv", "libx264"} : std::vector<const char*>{"libx264", "h264_mf"}; break;
    case 1: names = encoder == 1 ? std::vector<const char*>{"hevc_nvenc", "hevc_amf", "hevc_qsv", "libx265"} : std::vector<const char*>{"libx265"}; break;
    case 2:
    case 3: names = {"prores_ks", "prores"}; break;
    case 5: names = {"libvpx-vp9"}; break;
    }
    std::vector<const AVCodec*> r;
    for (auto* n : names) if (const AVCodec* c = avcodec_find_encoder_by_name(n)) r.push_back(c);
    return r;
}

bool Exporter::start(Project& p, RenderItem& item, Renderer& r, std::string& err) {
    Composition* c = p.findComp(item.comp);
    if (!c) { err = "composition not found"; return false; }
    if (item.out.path.empty()) { err = "no output file"; return false; }
    auto j = new Job();
    j->proj = &p;
    j->comp = c;
    j->rend = &r;
    j->itemId = item.id;
    j->out = item.out;
    j->fps = c->fps;
    double a = item.out.range == 0 ? c->workStart : 0, b = item.out.range == 0 ? c->workEnd : c->duration;
    if (b <= a) { a = 0; b = c->duration; }
    j->t0 = a;
    j->frames = std::max<int64_t>(1, (int64_t)std::llround((b - a) * c->fps));
    j->scale = clampv(item.out.scalePct, 10, 400) / 100.f;
    j->w = std::max(2, (int)std::lround(c->w * j->scale)) & ~1;
    j->h = std::max(2, (int)std::lround(c->h * j->scale)) & ~1;
    j->alpha = item.out.format == 3 || item.out.format == 4 || item.out.format == 5;
    j->pkt = av_packet_alloc();
    j->startWall = nowSec();
    makeDirs(parentDir(item.out.path));

    if (item.out.format == 4) {
        std::string base = item.out.path;
        if (fileExt(base) == ".png") base = base.substr(0, base.size() - 4);
        j->pngBase = base;
        job = j;
        return true;
    }

    std::vector<const AVCodec*> candidates = pickVideoCodecs(item.out.format, item.out.encoder);
    if (candidates.empty()) { err = "video encoder not available in this FFmpeg build"; delete j; return false; }
    int r0 = avformat_alloc_output_context2(&j->fmt, nullptr, nullptr, item.out.path.c_str());
    if (r0 < 0 || !j->fmt) { err = "cannot create container: " + avErrStr(r0); delete j; return false; }
    j->vs = avformat_new_stream(j->fmt, nullptr);
    AVRational fr = av_d2q(c->fps, 100000);
    AVPixelFormat pf = AV_PIX_FMT_YUV420P;
    const AVCodec* codec = nullptr;
    std::string openErr;
    // try encoders in order (e.g. NVENC first, then x264 when the GPU driver is too old)
    for (const AVCodec* cand : candidates) {
    if (j->vc) avcodec_free_context(&j->vc);
    codec = cand;
    j->vc = avcodec_alloc_context3(codec);
    j->vc->thread_count = 0;
    j->vc->width = j->w;
    j->vc->height = j->h;
    j->vc->framerate = fr;
    j->vc->time_base = av_inv_q(fr);
    j->vc->gop_size = (int)std::lround(c->fps * 2);
    j->vc->color_range = AVCOL_RANGE_MPEG;
    j->vc->colorspace = AVCOL_SPC_BT709;
    j->vc->color_primaries = AVCOL_PRI_BT709;
    j->vc->color_trc = AVCOL_TRC_BT709;
    pf = AV_PIX_FMT_YUV420P;
    std::string cname = codec->name;
    char q[32];
    snprintf(q, sizeof(q), "%d", item.out.quality);
    if (item.out.format == 2) { pf = AV_PIX_FMT_YUV422P10LE; av_opt_set(j->vc->priv_data, "profile", "3", 0); j->vc->codec_tag = MKTAG('a', 'p', 'c', 'h'); }
    else if (item.out.format == 3) { pf = AV_PIX_FMT_YUVA444P10LE; av_opt_set(j->vc->priv_data, "profile", "4", 0); j->vc->codec_tag = MKTAG('a', 'p', '4', 'h'); }
    else if (item.out.format == 5) {
        pf = AV_PIX_FMT_YUVA420P;
        av_opt_set(j->vc->priv_data, "crf", q, 0);
        av_opt_set(j->vc->priv_data, "deadline", "good", 0);
        av_opt_set(j->vc->priv_data, "cpu-used", "5", 0);
        av_opt_set(j->vc->priv_data, "row-mt", "1", 0);
        av_opt_set(j->vc->priv_data, "tile-columns", "2", 0);
        j->vc->bit_rate = item.out.bitrate > 0 ? (int64_t)item.out.bitrate * 1000000 : 0;
    } else {
        if (cname.find("nvenc") != std::string::npos) {
            av_opt_set(j->vc->priv_data, "preset", "p6", 0);
            av_opt_set(j->vc->priv_data, "tune", "hq", 0);
            if (item.out.bitrate > 0) { j->vc->bit_rate = (int64_t)item.out.bitrate * 1000000; av_opt_set(j->vc->priv_data, "rc", "vbr", 0); }
            else { av_opt_set(j->vc->priv_data, "rc", "vbr", 0); av_opt_set(j->vc->priv_data, "cq", q, 0); j->vc->bit_rate = 0; }
        } else if (cname == "libx264" || cname == "libx265") {
            av_opt_set(j->vc->priv_data, "preset", "medium", 0);
            if (item.out.bitrate > 0) j->vc->bit_rate = (int64_t)item.out.bitrate * 1000000;
            else av_opt_set(j->vc->priv_data, "crf", q, 0);
            if (cname == "libx264") av_opt_set(j->vc->priv_data, "profile", "high", 0);
        } else {
            j->vc->bit_rate = (int64_t)std::max(8, item.out.bitrate) * 1000000;
        }
        if (item.out.format == 1) j->vc->codec_tag = MKTAG('h', 'v', 'c', '1');
    }
    j->vc->pix_fmt = pf;
    if (j->fmt->oformat->flags & AVFMT_GLOBALHEADER) j->vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int r1 = avcodec_open2(j->vc, codec, nullptr);
    if (r1 >= 0) break;
    openErr += std::string(openErr.empty() ? "" : "; ") + codec->name + ": " + avErrStr(r1);
    logf("encoder %s unavailable (%s), trying next", codec->name, avErrStr(r1).c_str());
    avcodec_free_context(&j->vc);
    }
    if (!j->vc) { err = "cannot open encoder - " + openErr; delete j; return false; }
    if (!openErr.empty()) j->note = "used " + std::string(codec->name) + " (" + openErr + ")";
    avcodec_parameters_from_context(j->vs->codecpar, j->vc);
    j->vs->time_base = j->vc->time_base;
    j->vs->avg_frame_rate = fr;

    if (item.out.audio) {
        j->clips = collectAudioClips(p, *c, true);  // voice / speed processing finished before encoding
        const AVCodec* acodec = item.out.format == 5 ? avcodec_find_encoder_by_name("libopus") : avcodec_find_encoder_by_name("aac");
        if (acodec) {
            j->as = avformat_new_stream(j->fmt, nullptr);
            j->ac = avcodec_alloc_context3(acodec);
            j->ac->sample_rate = 48000;
            av_channel_layout_default(&j->ac->ch_layout, 2);
            j->ac->sample_fmt = item.out.format == 5 ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;
            j->ac->bit_rate = (int64_t)std::max(64, item.out.audioKbps) * 1000;
            j->ac->time_base = AVRational{1, 48000};
            if (j->fmt->oformat->flags & AVFMT_GLOBALHEADER) j->ac->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(j->ac, acodec, nullptr) < 0) {
                avcodec_free_context(&j->ac);
                j->as = nullptr;
            } else {
                avcodec_parameters_from_context(j->as->codecpar, j->ac);
                j->as->time_base = j->ac->time_base;
                j->aframe = av_frame_alloc();
                j->aframe->format = j->ac->sample_fmt;
                av_channel_layout_copy(&j->aframe->ch_layout, &j->ac->ch_layout);
                j->aframe->sample_rate = 48000;
                j->aframe->nb_samples = j->ac->frame_size > 0 ? j->ac->frame_size : 1024;
                av_frame_get_buffer(j->aframe, 0);
            }
        }
    }
    if (!(j->fmt->oformat->flags & AVFMT_NOFILE)) {
        int r2 = avio_open(&j->fmt->pb, item.out.path.c_str(), AVIO_FLAG_WRITE);
        if (r2 < 0) { err = "cannot open output file: " + avErrStr(r2); delete j; return false; }
    }
    int r3 = avformat_write_header(j->fmt, nullptr);
    if (r3 < 0) { err = "write header: " + avErrStr(r3); delete j; return false; }
    j->vframe = av_frame_alloc();
    j->vframe->format = pf;
    j->vframe->width = j->w;
    j->vframe->height = j->h;
    av_frame_get_buffer(j->vframe, 0);
    j->sws = sws_getContext(j->w, j->h, AV_PIX_FMT_RGBA, j->w, j->h, pf, SWS_BICUBIC, nullptr, nullptr, nullptr);
    job = j;
    logf("export: %s %dx%d %lld frames via %s", item.out.path.c_str(), j->w, j->h, (long long)j->frames, codec->name);
    return true;
}

bool Exporter::step(double budget) {
    if (!job) return false;
    Job& j = *job;
    double st = nowSec();
    std::string err;
    while (j.cur < j.frames && nowSec() - st < budget) {
        double t = j.t0 + j.cur / j.fps;
        RenderParams rp;
        rp.scale = (float)j.w / j.comp->w;
        rp.sync = true;
        TexPtr img = j.rend->renderComp(*j.proj, *j.comp, t, rp);
        if (!img) { finish(false, "render failed"); return false; }
        TexPtr out8 = j.rend->finalizeRGBA8(*img, j.comp->bg, j.alpha);
        if (!gfx::readback(*out8, j.rgba)) { finish(false, "GPU readback failed"); return false; }
        int iw = out8->w, ih = out8->h;
        if (!j.pngBase.empty()) {
            std::string fn = strf("%s_%05lld.png", j.pngBase.c_str(), (long long)j.cur);
            if (!gfx::savePNG(fn, j.rgba.data(), iw, ih)) { finish(false, "cannot write " + fn); return false; }
        } else {
            av_frame_make_writable(j.vframe);
            const uint8_t* src[4] = {j.rgba.data(), nullptr, nullptr, nullptr};
            int sl[4] = {iw * 4, 0, 0, 0};
            if (iw != j.w || ih != j.h) {
                sws_freeContext(j.sws);
                j.sws = sws_getContext(iw, ih, AV_PIX_FMT_RGBA, j.w, j.h, (AVPixelFormat)j.vframe->format, SWS_BICUBIC, nullptr, nullptr, nullptr);
            }
            sws_scale(j.sws, src, sl, 0, ih, j.vframe->data, j.vframe->linesize);
            j.vframe->pts = j.cur;
            if (avcodec_send_frame(j.vc, j.vframe) < 0) { finish(false, "video encode failed"); return false; }
            if (!j.writePackets(j.vc, j.vs, err)) { finish(false, err); return false; }
            if (!j.encodeAudioUntil((j.cur + 1) / j.fps, err, false)) { finish(false, err); return false; }
        }
        j.cur++;
    }
    if (j.cur >= j.frames) {
        if (j.fmt) {
            avcodec_send_frame(j.vc, nullptr);
            if (!j.writePackets(j.vc, j.vs, err)) { finish(false, err); return false; }
            if (!j.encodeAudioUntil(j.frames / j.fps, err, true)) { finish(false, err); return false; }
            av_write_trailer(j.fmt);
        }
        finish(true, strf("Done in %.1f s", nowSec() - j.startWall) + (j.note.empty() ? "" : " - " + j.note));
        return false;
    }
    return true;
}

void Exporter::finish(bool ok, const std::string& msg) {
    lastOk = ok;
    lastMessage = msg;
    if (job && job->proj) {
        for (auto& it : job->proj->queue)
            if (it.id == job->itemId) {
                it.status = ok ? 2 : 3;
                it.message = msg;
                it.progress = ok ? 1.f : it.progress;
            }
    }
    logf("export finished: %s", msg.c_str());
    delete job;
    job = nullptr;
}

void Exporter::cancel() {
    if (!job) return;
    uint64_t id = job->itemId;
    Project* p = job->proj;
    std::string path = job->out.path;
    bool png = !job->pngBase.empty();
    delete job;
    job = nullptr;
    if (!png) DeleteFileW(widen(path).c_str());
    for (auto& it : p->queue) if (it.id == id) { it.status = 4; it.message = "Stopped by user"; }
    lastOk = false;
    lastMessage = "Stopped";
}

float Exporter::progress() const { return job ? (float)job->cur / std::max<int64_t>(1, job->frames) : 0.f; }
uint64_t Exporter::itemId() const { return job ? job->itemId : 0; }
std::string Exporter::status() const {
    if (!job) return "";
    return strf("Frame %lld / %lld", (long long)job->cur, (long long)job->frames);
}
double Exporter::eta() const {
    if (!job || job->cur == 0) return -1;
    double el = nowSec() - job->startWall;
    return el / job->cur * (job->frames - job->cur);
}
