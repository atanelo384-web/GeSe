// Direct3D 11 helpers: device, textures, pooled render targets, shader compilation.
#pragma once
#include "core.h"

struct Texture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    int w = 0, h = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    bool pooled = false;
    ImTextureID id() const { return (ImTextureID)(intptr_t)srv.Get(); }
};
using TexPtr = std::shared_ptr<Texture>;

namespace gfx {
constexpr DXGI_FORMAT kWorkFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

bool init(HWND hwnd);
void shutdown();
void resize(int w, int h);
void beginBackbuffer(float r, float g, float b);
void present(bool vsync);
ID3D11Device* dev();
ID3D11DeviceContext* ctx();
std::string adapterName();
int64_t adapterVramMB();

TexPtr createTexture(int w, int h, DXGI_FORMAT fmt, bool renderTarget, const void* data = nullptr, int pitch = 0);
void upload(Texture& t, const void* data, int pitch);
// Pooled render target. Released automatically back to the pool.
TexPtr rt(int w, int h, DXGI_FORMAT fmt = kWorkFormat);
void trimPool(size_t keepBytes);
size_t poolBytes();
void clear(Texture& t, float r, float g, float b, float a);

// Shader compilation (d3dcompiler_47 loaded at runtime, disk cache in %LOCALAPPDATA%).
bool compile(const std::string& src, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& err);
ComPtr<ID3D11PixelShader> compilePS(const std::string& src, std::string* err = nullptr, const char* entry = "main");

// Fullscreen pass: binds srvs to t0.., cb0/cb1 to b0/b1, draws into target.
void pass(Texture& target, ID3D11PixelShader* ps, std::initializer_list<ID3D11ShaderResourceView*> srvs,
          const void* cb0, size_t cb0Size, const void* cb1 = nullptr, size_t cb1Size = 0, bool blendOver = false);
// RGBA8 readback (src must be R8G8B8A8_UNORM)
bool readback(Texture& src, std::vector<uint8_t>& out);
bool readPixel(Texture& src, int x, int y, float rgba[4]);
// image helpers through WIC
bool loadImageFile(const std::string& path, std::vector<uint8_t>& rgba, int& w, int& h);  // straight RGBA8
bool savePNG(const std::string& path, const uint8_t* rgba, int w, int h);
bool encodePNG(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out);
bool decodeImageMemory(const void* data, size_t n, std::vector<uint8_t>& rgba, int& w, int& h);
TexPtr loadTextureFile(const std::string& path, bool premultiply = true);
void premultiply(std::vector<uint8_t>& rgba);
}  // namespace gfx
