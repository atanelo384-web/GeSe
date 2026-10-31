// Effect registry. Every effect (built-in or plugin) is described in a .gsfx file:
//
//   @effect Twirl                       display name
//   @id gese.distort.twirl              unique id (optional, generated from category+name)
//   @category Distort
//   @mix manual|auto                    auto: result is cross-faded with the input by Intensity
//   @speed                              effect is animated -> exposes a Speed parameter
//   @passes 2                           multi-pass (PASS = 0..n-1, gSrc = previous pass)
//   @timeop posterize                   special time operator (handled by the renderer)
//   @param float  radius "Radius" 50 0 500 px
//   @param angle  angle  "Angle" 90
//   @param point  center "Center" 0.5 0.5
//   @param color  tint   "Color" 1 0.5 0 1
//   @param bool   inv    "Invert" 0
//   @param choice edge   "Edge Mode" 2 "Crop|Repeat|Mirror|Stretch"
//   @code
//   float4 effect(float2 uv) { ... }    HLSL, returns straight (non-premultiplied) RGBA
//   @end
#pragma once
#include "core.h"
#include "project.h"

enum class ParamType { Float, Angle, Point, Color, Bool, Choice };

struct ParamDef {
    ParamType type = ParamType::Float;
    std::string name, label, unit;
    float def[4] = {0, 0, 0, 0};
    float minv = 0, maxv = 100;
    bool hasRange = false;
    std::vector<std::string> choices;
    int dim() const { return type == ParamType::Point ? 2 : (type == ParamType::Color ? 4 : 1); }
};

struct EffectDef {
    std::string id, name, category, file, description;
    bool autoMix = true;
    bool animated = false;
    int passes = 1;
    std::string timeop;
    bool plugin = false;
    bool ok = false;
    std::string error;
    std::vector<ParamDef> params;
    std::string code;
    ComPtr<ID3D11PixelShader> ps;
    int paramIndex(const std::string& n) const;
};

struct EffectFileInfo {
    std::string path;
    bool plugin = false;
    bool enabled = true;
    int effects = 0;
    int errors = 0;
    std::string firstError;
};

struct EffectRegistry {
    std::vector<std::unique_ptr<EffectDef>> defs;
    std::vector<EffectFileInfo> files;
    std::vector<std::string> categories;  // ordered
    std::set<std::string> disabledPlugins;

    // loading happens in steps so the splash can animate
    void beginLoad();
    bool loadStep(std::string& currentFile);  // returns false when done
    int totalFiles() const { return (int)pending.size() + (int)files.size(); }
    int loadedFiles() const { return (int)files.size(); }
    void reload();

    const EffectDef* find(const std::string& id) const;
    std::vector<const EffectDef*> inCategory(const std::string& cat) const;
    EffectInst instantiate(const EffectDef& d) const;
    void sync(EffectInst& e) const;  // re-align params after reload
    static std::string prelude();
    std::string builtinDir() const;
    std::string pluginDir() const;
    std::string userPluginDir() const;

  private:
    std::vector<std::pair<std::string, bool>> pending;
    void parseFile(const std::string& path, bool plugin);
    void compile(EffectDef& d);
    void rebuildCategories();
};

// Built-in categories in display order
extern const char* const kCategoryOrder[];
