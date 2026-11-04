// Minimal JSON reader for library.json (own code, MIT with the rest of the port). Objects, arrays, strings (with \uXXXX),
// numbers, true/false/null. No writer. Missing keys read as null values, so callers can chain without checks.
#pragma once
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json& operator[](const char* key) const {
        static const Json none;
        for (auto& kv : o) if (kv.first == key) return kv.second;
        return none;
    }
    bool isNull() const { return type == Null; }
    std::string str(const std::string& def = "") const { return type == Str ? s : def; }
    int num(int def = 0) const { return type == Num ? (int)n : def; }
    double dbl(double def = 0) const { return type == Num ? n : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : def; }

    // Returns false and sets err on a syntax error.
    static bool parse(const std::string& text, Json& out, std::string& err) {
        P p{text.c_str(), text.c_str() + text.size(), ""};
        if (!p.value(out, 0)) { err = p.err; return false; }
        p.ws();
        if (p.c != p.e) { err = "trailing characters"; return false; }
        return true;
    }

private:
    struct P {
        const char* c; const char* e; std::string err;
        void ws() { while (c < e && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++; }
        bool fail(const char* m) { if (err.empty()) err = m; return false; }
        static void utf8(std::string& s, unsigned cp) {
            if (cp < 0x80) s += (char)cp;
            else if (cp < 0x800) { s += (char)(0xC0 | cp >> 6); s += (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { s += (char)(0xE0 | cp >> 12); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
            else { s += (char)(0xF0 | cp >> 18); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
        }
        bool hex4(unsigned& v) {
            if (e - c < 4) return false;
            v = 0;
            for (int i = 0; i < 4; i++) {
                char h = *c++; v <<= 4;
                if (h >= '0' && h <= '9') v |= h - '0';
                else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                else return false;
            }
            return true;
        }
        bool string(std::string& r) {
            c++;  // opening quote
            while (c < e && *c != '"') {
                if (*c != '\\') { r += *c++; continue; }
                if (++c >= e) break;
                switch (*c++) {
                    case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
                    case 'b': r += '\b'; break; case 'f': r += '\f'; break;
                    case 'u': {
                        unsigned cp;
                        if (!hex4(cp)) return fail("bad \\u escape");
                        if (cp >= 0xD800 && cp < 0xDC00 && e - c >= 6 && c[0] == '\\' && c[1] == 'u') {
                            c += 2; unsigned lo;
                            if (!hex4(lo)) return fail("bad \\u escape");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(r, cp); break;
                    }
                    default: r += c[-1]; break;  // " \ /
                }
            }
            if (c >= e) return fail("unterminated string");
            c++;
            return true;
        }
        bool value(Json& v, int depth) {
            if (depth > 64) return fail("nesting too deep");
            ws();
            if (c >= e) return fail("unexpected end");
            if (*c == '{') {
                v.type = Obj; c++; ws();
                if (c < e && *c == '}') { c++; return true; }
                for (;;) {
                    ws();
                    if (c >= e || *c != '"') return fail("object key expected");
                    std::string k; if (!string(k)) return false;
                    ws(); if (c >= e || *c != ':') return fail("':' expected"); c++;
                    Json child; if (!value(child, depth + 1)) return false;
                    v.o.emplace_back(std::move(k), std::move(child));
                    ws();
                    if (c < e && *c == ',') { c++; continue; }
                    if (c < e && *c == '}') { c++; return true; }
                    return fail("',' or '}' expected");
                }
            }
            if (*c == '[') {
                v.type = Arr; c++; ws();
                if (c < e && *c == ']') { c++; return true; }
                for (;;) {
                    Json child; if (!value(child, depth + 1)) return false;
                    v.a.push_back(std::move(child));
                    ws();
                    if (c < e && *c == ',') { c++; continue; }
                    if (c < e && *c == ']') { c++; return true; }
                    return fail("',' or ']' expected");
                }
            }
            if (*c == '"') { v.type = Str; return string(v.s); }
            if (e - c >= 4 && std::string(c, 4) == "true") { v.type = Bool; v.b = true; c += 4; return true; }
            if (e - c >= 5 && std::string(c, 5) == "false") { v.type = Bool; c += 5; return true; }
            if (e - c >= 4 && std::string(c, 4) == "null") { c += 4; return true; }
            char* end = nullptr;
            double d = strtod(c, &end);
            if (end == c) return fail("value expected");
            v.type = Num; v.n = d; c = end;
            return true;
        }
    };
};
