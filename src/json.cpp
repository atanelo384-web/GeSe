#include "json.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

Json& Json::operator[](const std::string& k) {
    if (t != Obj) { t = Obj; o.clear(); }
    for (auto& kv : o)
        if (kv.first == k) return kv.second;
    o.emplace_back(k, Json());
    return o.back().second;
}
const Json* Json::get(const std::string& k) const {
    if (t != Obj) return nullptr;
    for (auto& kv : o)
        if (kv.first == k) return &kv.second;
    return nullptr;
}
double Json::num(const std::string& k, double def) const {
    const Json* j = get(k);
    if (!j) return def;
    if (j->t == Num) return j->n;
    if (j->t == Bool) return j->b ? 1 : 0;
    return def;
}
std::string Json::str(const std::string& k, const std::string& def) const {
    const Json* j = get(k);
    return (j && j->t == Str) ? j->s : def;
}
bool Json::boolean(const std::string& k, bool def) const {
    const Json* j = get(k);
    if (!j) return def;
    if (j->t == Bool) return j->b;
    if (j->t == Num) return j->n != 0;
    return def;
}

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
    out += '"';
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append((size_t)(d * indent), ' ');
    };
    switch (t) {
    case Null: out += "null"; break;
    case Bool: out += b ? "true" : "false"; break;
    case Num: {
        char buf[64];
        if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 9e15) snprintf(buf, sizeof(buf), "%lld", (long long)n);
        else if (std::isfinite(n)) snprintf(buf, sizeof(buf), "%.9g", n);
        else snprintf(buf, sizeof(buf), "0");
        out += buf;
        break;
    }
    case Str: escapeTo(out, s); break;
    case Arr: {
        out += '[';
        // keep short numeric arrays on one line
        bool flat = a.size() <= 4;
        for (auto& e : a) if (e.t == Arr || e.t == Obj) flat = false;
        for (size_t i = 0; i < a.size(); i++) {
            if (i) out += ',';
            if (!flat) nl(depth + 1);
            a[i].dumpTo(out, flat ? -1 : indent, depth + 1);
        }
        if (!flat && !a.empty()) nl(depth);
        out += ']';
        break;
    }
    case Obj: {
        out += '{';
        for (size_t i = 0; i < o.size(); i++) {
            if (i) out += ',';
            nl(depth + 1);
            escapeTo(out, o[i].first);
            out += indent >= 0 ? ": " : ":";
            o[i].second.dumpTo(out, indent, depth + 1);
        }
        if (!o.empty()) nl(depth);
        out += '}';
        break;
    }
    }
}
std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

namespace {
struct Parser {
    const char* p;
    const char* e;
    bool ok = true;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    std::string str() {
        std::string o;
        p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    uint32_t cp = (uint32_t)strtoul(std::string(p, std::min(p + 4, e)).c_str(), nullptr, 16);
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= e && p[0] == '\\' && p[1] == 'u') {
                        uint32_t lo = (uint32_t)strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16);
                        p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += c;
                }
            } else o += *p++;
        }
        if (p < e) p++; else ok = false;
        return o;
    }
    Json val() {
        ws();
        if (p >= e) { ok = false; return Json(); }
        char c = *p;
        if (c == '{') {
            Json j = Json::object();
            p++;
            ws();
            if (p < e && *p == '}') { p++; return j; }
            while (ok && p < e) {
                ws();
                if (p >= e || *p != '"') { ok = false; break; }
                std::string k = str();
                ws();
                if (p >= e || *p != ':') { ok = false; break; }
                p++;
                j.o.emplace_back(k, val());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                ok = false;
            }
            return j;
        }
        if (c == '[') {
            Json j = Json::array();
            p++;
            ws();
            if (p < e && *p == ']') { p++; return j; }
            while (ok && p < e) {
                j.a.push_back(val());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                ok = false;
            }
            return j;
        }
        if (c == '"') return Json(str());
        if (!strncmp(p, "true", 4)) { p += 4; return Json(true); }
        if (!strncmp(p, "false", 5)) { p += 5; return Json(false); }
        if (!strncmp(p, "null", 4)) { p += 4; return Json(); }
        char* end = nullptr;
        double d = strtod(p, &end);
        if (end == p) { ok = false; return Json(); }
        p = end;
        return Json(d);
    }
};
}  // namespace

Json Json::parse(const std::string& text, bool* ok) {
    Parser ps{text.data(), text.data() + text.size()};
    Json j = ps.val();
    if (ok) *ok = ps.ok;
    return j;
}
