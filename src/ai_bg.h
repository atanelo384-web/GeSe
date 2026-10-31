// AI background removal: built-in neural network (BiRefNet, free & offline) or HTTP APIs
// (remove.bg, Photoroom, local rembg server, custom).
#pragma once
#include "core.h"
#include "project.h"

static const char* const kAiProviders[] = {"GeSe AI: built-in BiRefNet (free, offline, GPU)", "remove.bg (cloud)", "Photoroom (cloud)",
                                           "rembg: local server", "Custom endpoint"};
constexpr int kAiProviderCount = 5;

struct AiJobDesc {
    int provider = 0;
    bool gpu = true;
    std::string apiKey, endpoint;
    std::shared_ptr<Footage> footage;  // source
    std::vector<int64_t> frames;       // frame indices to process (single entry for stills)
    std::string outDir, baseName;
    uint64_t layerId = 0, compId = 0;
};

class AiBackgroundRemover {
  public:
    bool start(const AiJobDesc& d, std::string& err);
    bool running() const { return active.load(); }
    void cancel() { stopFlag = true; }
    float progress() const { return total ? (float)done.load() / total : 0.f; }
    std::string status();
    // set when a job ends; the UI imports the result and clears it
    bool takeResult(AiJobDesc& desc, std::vector<std::string>& files, std::string& error);
    ~AiBackgroundRemover();

  private:
    void runLocal();
    bool runLocalProcess();
    std::thread th;
    std::atomic<bool> active{false}, stopFlag{false}, finished{false};
    std::atomic<int> done{0};
    int total = 0;
    std::mutex mx;
    std::string msg, error;
    AiJobDesc desc;
    std::vector<std::string> outFiles;
};

// child-process entry point: GeSe.exe --matte-job <job.json>
int aiRunMatteJob(const std::string& jobPath);

bool httpPostMultipart(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
                       const std::string& fileField, const std::string& fileName, const std::vector<uint8_t>& fileData,
                       const std::vector<std::pair<std::string, std::string>>& fields, int& status, std::vector<uint8_t>& response,
                       std::string& err);
