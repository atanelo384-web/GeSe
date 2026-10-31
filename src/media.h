// Media decoding: FFmpeg video/audio, WIC stills and image sequences.
#pragma once
#include "core.h"
#include "project.h"

struct VideoFrame {
    std::vector<uint8_t> rgba;  // premultiplied RGBA8
    int w = 0, h = 0;
    int64_t idx = -1;
};
using FramePtr = std::shared_ptr<VideoFrame>;

class FrameDecoder {
  public:
    virtual ~FrameDecoder() {}
    virtual bool decode(int64_t idx, int maxW, VideoFrame& out) = 0;
};

struct MediaInfo {
    bool ok = false, hasVideo = false, hasAudio = false, still = false;
    int w = 0, h = 0;
    double fps = 30, duration = 0;
    int64_t frames = 0;
    std::string codec, audioCodec;
    int sampleRate = 0, channels = 0;
};
bool probeMedia(const std::string& path, MediaInfo& info);

struct MediaSource {
    std::string path;
    int w = 0, h = 0;
    double fps = 30;
    int64_t frameCount = 1;
    bool still = false;

    static std::shared_ptr<MediaSource> open(const Footage& f);
    ~MediaSource();
    int64_t indexAt(double srcTime) const;
    // async (preview): returns best cached frame (may differ from idx), schedules decoding of idx
    FramePtr get(int64_t idx, int maxW, bool* exact);
    void prefetch(int64_t fromIdx, int count, int maxW);
    // sync (export, AI tools)
    FramePtr decodeSync(int64_t idx, int maxW);
    bool busy() const { return pendingWork.load(); }
    static void setCacheBudget(int64_t bytes);

  private:
    std::unique_ptr<FrameDecoder> preview, sync;
    std::mutex mx, syncMx;
    std::condition_variable cv;
    std::thread worker;
    bool quit = false;
    int64_t want = -1;
    int wantW = 0;
    int64_t prefFrom = -1;
    int prefCount = 0;
    std::atomic<bool> pendingWork{false};
    std::map<int64_t, FramePtr> cache;
    std::deque<int64_t> lru;
    FramePtr stillFrame;
    void run();
    void insert(const FramePtr& f);
    FramePtr lookup(int64_t idx, int minW);
    std::function<std::unique_ptr<FrameDecoder>()> makeDecoder;
};

// Fully decoded audio track, 48 kHz stereo float interleaved
struct AudioData {
    static constexpr int kRate = 48000;
    std::vector<float> samples;
    std::vector<float> peaks;  // max |amplitude| per kPeakBlock frames
    static constexpr int kPeakBlock = 256;
    std::atomic<bool> ready{false};
    double duration() const { return samples.size() / 2.0 / kRate; }
    static std::shared_ptr<AudioData> decodeAsync(const std::string& path);
};

bool detectSequence(const std::string& path, Footage& f);
std::string sequenceFrameName(const Footage& f, int64_t idx);
bool isImageExt(const std::string& ext);
bool isMediaExt(const std::string& ext);
