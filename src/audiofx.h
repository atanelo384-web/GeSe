// Offline voice & speed processing of clip audio with FFmpeg filters:
// Rubber Band time-stretch (speed without changing the pitch) and pitch shift, plus voice presets.
#pragma once
#include "core.h"
#include "media.h"

struct VoicePreset {
    const char* name;
    float semitones;      // added to the clip pitch
    bool keepFormant;     // natural timbre (higher/lower voice) instead of the cartoon shift
    const char* filters;  // extra FFmpeg audio filter chain after the pitch stage, may be empty
};
extern const VoicePreset kVoicePresets[];
extern const int kVoicePresetCount;

struct AudioFxParams {
    double tempo = 1;    // > 1 plays faster with the same pitch
    float semitones = 0;
    int voice = 0;
    bool denoise = false;  // FFT noise reduction
    bool enhance = false;  // clearer voice: low cut, presence boost, gentle compression
    bool active() const;
};

// Processed audio for the source range [srcA, srcB) seconds. Returns nullptr while a background job makes it
// (audioFxGeneration() changes when it is ready); sync = process now and wait (export).
std::shared_ptr<AudioData> audioFxGet(const std::shared_ptr<AudioData>& src, double srcA, double srcB, const AudioFxParams& p, bool sync);
uint64_t audioFxGeneration();
bool audioFxBusy();

double energyCenter(const std::vector<float>& stereo);  // sample position of a click (energy-weighted)

// stereo 48 kHz float interleaved in/out
bool audioFxRender(const float* in, int64_t frames, const AudioFxParams& p, std::vector<float>& out, std::string& err);

// write stereo 48 kHz float audio to .wav (16-bit PCM), .mp3, .m4a (AAC) or .flac
bool saveAudioFile(const std::string& path, const float* samples, int64_t frames, std::string& err);
