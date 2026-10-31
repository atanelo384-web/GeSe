// GeSe v2.4.0 — shared core utilities
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <map>
#include <unordered_map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <algorithm>
#include "imgui.h"

using Microsoft::WRL::ComPtr;

#define GESE_NAME        "GeSe"
#define GESE_VERSION     "v2.4.0"
#define GESE_FULLNAME    "GeSe v2.4.0"

// ---------------------------------------------------------------------------
// Palette: two brand colors (logo orange + logo dark) on neutral greys.
// No gradients anywhere in the UI.
// ---------------------------------------------------------------------------
namespace pal {
constexpr ImU32 Accent     = IM_COL32(0xB8, 0x55, 0x00, 0xFF);  // logo letters
constexpr ImU32 AccentDark = IM_COL32(0x27, 0x12, 0x00, 0xFF);  // logo background
constexpr ImU32 AccentA(int a) { return IM_COL32(0xB8, 0x55, 0x00, a); }
// neutrals (dark UI)
constexpr ImU32 Bg0   = IM_COL32(0x16, 0x16, 0x16, 0xFF);  // deepest (viewer pasteboard)
constexpr ImU32 Bg1   = IM_COL32(0x1D, 0x1D, 0x1D, 0xFF);  // panel body
constexpr ImU32 Bg2   = IM_COL32(0x23, 0x23, 0x23, 0xFF);  // app frame / headers
constexpr ImU32 Bg3   = IM_COL32(0x2B, 0x2B, 0x2B, 0xFF);  // rows / frames
constexpr ImU32 Bg4   = IM_COL32(0x33, 0x33, 0x33, 0xFF);  // hover
constexpr ImU32 Bg5   = IM_COL32(0x3E, 0x3E, 0x3E, 0xFF);  // active / borders strong
constexpr ImU32 Line  = IM_COL32(0x10, 0x10, 0x10, 0xFF);  // separators
constexpr ImU32 Text  = IM_COL32(0xDE, 0xDE, 0xDE, 0xFF);
constexpr ImU32 TextDim = IM_COL32(0x96, 0x96, 0x96, 0xFF);
constexpr ImU32 TextFaint = IM_COL32(0x62, 0x62, 0x62, 0xFF);
constexpr ImU32 White = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
inline ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
}

// ---------------------------------------------------------------------------
// strings / paths
// ---------------------------------------------------------------------------
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string toLower(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);
std::string strf(const char* fmt, ...);

std::string exeDir();
std::string resDir();                              // <exe>\resources
std::string resPath(const std::string& rel);       // <exe>\resources\<rel>
std::string runtimeDir();                          // <exe>\runtime (DLLs)
std::string appDataDir();        // %APPDATA%\GeSe
std::string localDataDir();      // %LOCALAPPDATA%\GeSe
std::string documentsDir();
std::string joinPath(const std::string& a, const std::string& b);
std::string fileName(const std::string& p);
std::string fileStem(const std::string& p);
std::string fileExt(const std::string& p);      // lower case, with dot
std::string parentDir(const std::string& p);
std::string normPath(std::string p);            // forward slashes -> backslashes
bool fileExists(const std::string& p);
bool dirExists(const std::string& p);
void makeDirs(const std::string& p);
bool readFile(const std::string& p, std::string& out);
bool writeFile(const std::string& p, const std::string& data);
bool writeFileBin(const std::string& p, const void* data, size_t n);
int64_t fileSize(const std::string& p);
int64_t fileTime(const std::string& p);         // unix seconds
std::vector<std::string> listFiles(const std::string& dir, const std::string& ext, bool recursive);
std::string fmtBytes(int64_t n);
std::string fmtDate(int64_t unixSec);

// ---------------------------------------------------------------------------
// misc
// ---------------------------------------------------------------------------
void logf(const char* fmt, ...);
std::vector<std::string> logTail(int n);
double nowSec();
uint64_t newId();
void seedIds(uint64_t minNext);
uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL);
std::string fmtTimecode(double t, double fps);   // H:MM:SS:FF
std::string fmtTimeShort(double t);               // 0:00:05.20
double parseTimecode(const std::string& s, double fps, bool* ok);
inline double frameSnap(double t, double fps) { return std::floor(t * fps + 1e-6) / fps; }
inline int64_t timeToFrame(double t, double fps) { return (int64_t)std::floor(t * fps + 1e-6); }
template <class T> inline T clampv(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------
// 2D affine math (row major, column vectors: p' = M * [x y 1])
// ---------------------------------------------------------------------------
struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    static Mat3 identity() { return Mat3(); }
    static Mat3 translate(float x, float y) { Mat3 r; r.m[2] = x; r.m[5] = y; return r; }
    static Mat3 scale(float x, float y) { Mat3 r; r.m[0] = x; r.m[4] = y; return r; }
    static Mat3 rotate(float deg) {
        float a = deg * kPi / 180.f, c = cosf(a), s = sinf(a);
        Mat3 r; r.m[0] = c; r.m[1] = -s; r.m[3] = s; r.m[4] = c; return r;
    }
    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r.m[i * 3 + j] = m[i * 3 + 0] * b.m[0 * 3 + j] + m[i * 3 + 1] * b.m[1 * 3 + j] + m[i * 3 + 2] * b.m[2 * 3 + j];
        return r;
    }
    // projective: 3D layers seen through the camera are homographies (affine for 2D layers: w = 1)
    ImVec2 apply(ImVec2 p) const {
        float w = m[6] * p.x + m[7] * p.y + m[8];
        if (fabsf(w) < 1e-9f) w = 1e-9f;
        return ImVec2((m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w);
    }
    bool projective() const { return fabsf(m[6]) > 1e-12f || fabsf(m[7]) > 1e-12f || fabsf(m[8] - 1) > 1e-6f; }
    ImVec2 applyVec(ImVec2 p) const { return ImVec2(m[0] * p.x + m[1] * p.y, m[3] * p.x + m[4] * p.y); }
    Mat3 inverse() const {
        float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
        Mat3 r;
        if (fabsf(det) < 1e-12f) return r;
        float id = 1.f / det;
        r.m[0] = (m[4] * m[8] - m[5] * m[7]) * id;
        r.m[1] = (m[2] * m[7] - m[1] * m[8]) * id;
        r.m[2] = (m[1] * m[5] - m[2] * m[4]) * id;
        r.m[3] = (m[5] * m[6] - m[3] * m[8]) * id;
        r.m[4] = (m[0] * m[8] - m[2] * m[6]) * id;
        r.m[5] = (m[2] * m[3] - m[0] * m[5]) * id;
        r.m[6] = (m[3] * m[7] - m[4] * m[6]) * id;
        r.m[7] = (m[1] * m[6] - m[0] * m[7]) * id;
        r.m[8] = (m[0] * m[4] - m[1] * m[3]) * id;
        return r;
    }
};

// 3D: x right, y down, z away from the viewer (the usual compositing convention). Row-major, column vectors.
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float k) const { return Vec3(x * k, y * k, z * k); }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
    float len() const { return sqrtf(x * x + y * y + z * z); }
    Vec3 norm() const { float l = len(); return l > 1e-9f ? *this * (1.f / l) : Vec3(0, 0, 1); }
};
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Mat4 translate(float x, float y, float z) { Mat4 r; r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
    static Mat4 scale(float x, float y, float z) { Mat4 r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
    static Mat4 rotX(float deg) {
        float a = deg * kPi / 180.f, c = cosf(a), s = sinf(a);
        Mat4 r; r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; return r;
    }
    static Mat4 rotY(float deg) {
        float a = deg * kPi / 180.f, c = cosf(a), s = sinf(a);
        Mat4 r; r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; return r;
    }
    static Mat4 rotZ(float deg) {  // same direction as the 2D Mat3::rotate
        float a = deg * kPi / 180.f, c = cosf(a), s = sinf(a);
        Mat4 r; r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; return r;
    }
    static Mat4 from2D(const Mat3& a) {  // embeds a 2D affine transform (z untouched)
        Mat4 r; r.m[0] = a.m[0]; r.m[1] = a.m[1]; r.m[3] = a.m[2]; r.m[4] = a.m[3]; r.m[5] = a.m[4]; r.m[7] = a.m[5]; return r;
    }
    static Mat4 basis(const Vec3& x, const Vec3& y, const Vec3& z) {  // columns = axes
        Mat4 r; r.m[0] = x.x; r.m[4] = x.y; r.m[8] = x.z; r.m[1] = y.x; r.m[5] = y.y; r.m[9] = y.z; r.m[2] = z.x; r.m[6] = z.y; r.m[10] = z.z; return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += m[i * 4 + k] * b.m[k * 4 + j];
                r.m[i * 4 + j] = s;
            }
        return r;
    }
    Vec3 point(const Vec3& p) const { return Vec3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]); }
    Vec3 vec(const Vec3& p) const { return Vec3(m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z); }
    Vec3 col(int c) const { return Vec3(m[c], m[4 + c], m[8 + c]); }
};

// ImVec2 arithmetic comes from IMGUI_DEFINE_MATH_OPERATORS (set globally by the build)
inline float len2(ImVec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
