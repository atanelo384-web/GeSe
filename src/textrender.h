// Text layers rasterized with Direct2D / DirectWrite into D3D11 textures.
#pragma once
#include "core.h"
#include "gfx.h"

struct TextStyle {
    std::string text, font;
    float size = 100, tracking = 0, leading = 1.15f, strokeW = 0;
    float fill[4] = {1, 1, 1, 1}, stroke[4] = {0, 0, 0, 1};
    bool bold = true, italic = false, caps = false;
    int align = 1;
    uint64_t hash() const;
};

namespace textr {
bool init();
void shutdown();
// Renders text centered in a w*h premultiplied BGRA texture (cached by style hash).
TexPtr render(const TextStyle& s, int w, int h, float scale);
std::vector<std::string> systemFonts();
bool hasFamily(const std::string& family);
bool measure(const TextStyle& s, float& w, float& h, float& capHeight, float* baseline = nullptr);
// first app-bundled (then system) family whose name contains `part`, e.g. "akira" -> "Akira Super"
std::string appFamilyLike(const std::string& part, const std::string& fallback);
}
