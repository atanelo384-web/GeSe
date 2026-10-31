// Render queue / export through FFmpeg encoders.
#pragma once
#include "core.h"
#include "project.h"
#include "renderer.h"

struct ExportPreset {
    const char* name;
    int format, encoder, quality, bitrate, scalePct;
    bool audio;
    int audioKbps;
};
extern const ExportPreset kExportPresets[];
extern const int kExportPresetCount;
std::string formatExtension(int format);

class Exporter {
  public:
    bool active() const { return job != nullptr; }
    bool start(Project& p, RenderItem& item, Renderer& r, std::string& err);
    // renders frames for up to `budgetSec`; returns false when finished
    bool step(double budgetSec);
    void cancel();
    float progress() const;
    std::string status() const;
    double eta() const;
    uint64_t itemId() const;
    bool lastOk = false;
    std::string lastMessage;

  private:
    struct Job;
    Job* job = nullptr;
    void finish(bool ok, const std::string& msg);
};
