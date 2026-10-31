// Built-in background removal: BiRefNet (MIT) run locally with ONNX Runtime — DirectML on any DX12 GPU, CPU otherwise.
// Free, offline, no limits. Not thread-safe: used from the AI worker thread only.
#pragma once
#include "core.h"

std::string localModelPath();
bool localModelAvailable();
// rgba: straight (not premultiplied) 8-bit; alpha: w*h matte 0..255
bool localMatte(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& alpha, bool gpu, std::string& err);
std::string localDeviceName();  // "GPU (DirectML)" / "CPU" after the first run
void localNewJob();             // a new job tries the GPU again after a failure
void localRelease();            // frees the network (several GB on the GPU) when a job is done
