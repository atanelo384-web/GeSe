// Minimal JSON value used for projects, presets and settings.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    Json() {}
    Json(bool v) : t(Bool), b(v) {}
    Json(int v) : t(Num), n(v) {}
    Json(int64_t v) : t(Num), n((double)v) {}
    Json(uint64_t v) : t(Num), n((double)v) {}
    Json(float v) : t(Num), n(v) {}
    Json(double v) : t(Num), n(v) {}
    Json(const char* v) : t(Str), s(v) {}
    Json(const std::string& v) : t(Str), s(v) {}
    static Json object() { Json j; j.t = Obj; return j; }
    static Json array() { Json j; j.t = Arr; return j; }

    bool isObj() const { return t == Obj; }
    bool isArr() const { return t == Arr; }
    Json& operator[](const std::string& k);
    const Json* get(const std::string& k) const;
    void push(const Json& v) { if (t != Arr) { t = Arr; a.clear(); } a.push_back(v); }
    size_t size() const { return t == Arr ? a.size() : (t == Obj ? o.size() : 0); }

    double num(const std::string& k, double def = 0) const;
    std::string str(const std::string& k, const std::string& def = "") const;
    bool boolean(const std::string& k, bool def = false) const;
    uint64_t id(const std::string& k) const { return (uint64_t)num(k, 0); }

    std::string dump(int indent = 1) const;
    static Json parse(const std::string& text, bool* ok = nullptr);

  private:
    void dumpTo(std::string& out, int indent, int depth) const;
};
