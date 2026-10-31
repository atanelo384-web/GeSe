// Audio playback (WASAPI shared mode) and the offline mixer used for export.
#pragma once
#include "core.h"
#include "project.h"
#include "media.h"

struct AudioClip {
    std::shared_ptr<AudioData> data;
    double start = 0, in = 0, out = 0, speed = 1;
    double dataOffset = 0;  // seconds: data sample 0 = (t - start) * speed == dataOffset (processed voice ranges)
    bool reversed = false;
    Prop volume;          // dB, evaluated at (compT - volStart)
    double volStart = 0;
    float extraDb = 0;
    float fadeIn = 0, fadeOut = 0;  // seconds at the clip start / end
};

// sync = wait for voice / speed processing (export); onlyLayer = just that top-level layer (ignores mute/solo)
std::vector<AudioClip> collectAudioClips(const Project& p, const Composition& c, bool sync = false, uint64_t onlyLayer = 0);
void mixAudio(const std::vector<AudioClip>& clips, double t0, int frames, int rate, float* stereoOut, float master = 1.f);

class AudioEngine {
  public:
    bool init();
    void shutdown();
    void setClips(std::vector<AudioClip> clips);
    void play(double from, double loopStart, double loopEnd, bool loop);
    void stop();
    bool isPlaying() const { return playing.load(); }
    double position();
    bool deviceOk() const { return ok; }
    std::string deviceName;
    std::atomic<float> peakL{0}, peakR{0};
    std::atomic<float> master{1.0f};
    std::atomic<bool> mute{false};

  private:
    void run();
    bool ok = false;
    std::thread th;
    std::atomic<bool> quit{false}, playing{false};
    std::mutex mx;
    std::vector<AudioClip> clips;
    double t0 = 0, loopA = 0, loopB = 0;
    bool loop = false;
    std::atomic<int64_t> written{0}, writtenAtStart{0}, padding{0};
    int rate = 48000, channels = 2;
    double qpcStart = 0;
    void* client = nullptr;
    void* render = nullptr;
    void* evt = nullptr;
    unsigned bufferFrames = 0;
};
