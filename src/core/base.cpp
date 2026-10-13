// FIGHTO - core utilities: formatting, logging, strings, CRC.
#include "base.h"
#include "../platform/platform.h"

static int fmt_put(char* buf, int cap, int pos, char c) {
    if (pos < cap - 1) buf[pos] = c;
    return pos + 1;
}

static int fmt_uint(char* buf, int cap, int pos, u64 v, int base, int width, char pad, bool upper) {
    char tmp[32];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[n++] = digits[v % (u64)base]; v /= (u64)base; } while (v && n < 31);
    while (n < width && n < 31) tmp[n++] = pad;
    while (n > 0) pos = fmt_put(buf, cap, pos, tmp[--n]);
    return pos;
}

int fmt_strv(char* buf, int cap, const char* f, va_list ap) {
    int pos = 0;
    if (cap <= 0) return 0;
    while (*f) {
        char c = *f++;
        if (c != '%') { pos = fmt_put(buf, cap, pos, c); continue; }
        // flags / width / precision
        char pad = ' ';
        int width = 0, prec = -1;
        bool plus = false;
        if (*f == '+') { plus = true; f++; }
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { f++; prec = 0; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        bool longmod = false;
        while (*f == 'l' || *f == 'z') { longmod = true; f++; }
        char t = *f++;
        switch (t) {
        case 'd': case 'i': {
            i64 v = longmod ? va_arg(ap, i64) : (i64)va_arg(ap, int);
            if (v < 0) { pos = fmt_put(buf, cap, pos, '-'); v = -v; if (width) width--; }
            else if (plus) { pos = fmt_put(buf, cap, pos, '+'); if (width) width--; }
            pos = fmt_uint(buf, cap, pos, (u64)v, 10, width, pad, false);
        } break;
        case 'u': {
            u64 v = longmod ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
            pos = fmt_uint(buf, cap, pos, v, 10, width, pad, false);
        } break;
        case 'x': case 'X': {
            u64 v = longmod ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
            pos = fmt_uint(buf, cap, pos, v, 16, width, pad, t == 'X');
        } break;
        case 'p': {
            u64 v = (u64)(uintptr_t)va_arg(ap, void*);
            pos = fmt_put(buf, cap, pos, '0'); pos = fmt_put(buf, cap, pos, 'x');
            pos = fmt_uint(buf, cap, pos, v, 16, 0, '0', false);
        } break;
        case 'f': case 'g': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = (t == 'g') ? 3 : 6;
            if (v != v) { pos = fmt_put(buf, cap, pos, 'n'); pos = fmt_put(buf, cap, pos, 'a'); pos = fmt_put(buf, cap, pos, 'n'); break; }
            bool neg = v < 0;
            if (neg) v = -v;
            double scale = 1.0;
            for (int i = 0; i < prec; i++) scale *= 10.0;
            u64 whole = (u64)v;
            u64 frac = (u64)((v - (double)whole) * scale + 0.5);
            if (frac >= (u64)scale) { whole++; frac -= (u64)scale; }
            if (neg) { pos = fmt_put(buf, cap, pos, '-'); if (width) width--; }
            else if (plus) { pos = fmt_put(buf, cap, pos, '+'); if (width) width--; }
            int fw = width - (prec > 0 ? prec + 1 : 0);
            pos = fmt_uint(buf, cap, pos, whole, 10, fw > 0 ? fw : 0, pad, false);
            if (prec > 0) {
                pos = fmt_put(buf, cap, pos, '.');
                pos = fmt_uint(buf, cap, pos, frac, 10, prec, '0', false);
            }
        } break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            for (int i = len; i < width; i++) pos = fmt_put(buf, cap, pos, ' ');
            for (int i = 0; i < len; i++) pos = fmt_put(buf, cap, pos, s[i]);
        } break;
        case 'c': pos = fmt_put(buf, cap, pos, (char)va_arg(ap, int)); break;
        case '%': pos = fmt_put(buf, cap, pos, '%'); break;
        default: pos = fmt_put(buf, cap, pos, '%'); pos = fmt_put(buf, cap, pos, t); break;
        }
    }
    buf[pos < cap ? pos : cap - 1] = 0;
    return pos;
}

int fmt_str(char* buf, int cap, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = fmt_strv(buf, cap, f, ap);
    va_end(ap);
    return r;
}

void log_info(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    fmt_strv(buf, sizeof(buf), f, ap);
    va_end(ap);
    plat_log(buf, false);
}

void log_error(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    fmt_strv(buf, sizeof(buf), f, ap);
    va_end(ap);
    plat_log(buf, true);
}

void str_copy(char* dst, int cap, const char* src) {
    if (cap <= 0) return;
    int i = 0;
    if (src) for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

bool str_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

bool str_starts(const char* s, const char* prefix) {
    while (*prefix) { if (*s++ != *prefix++) return false; }
    return true;
}

int str_to_int(const char* s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sign;
}

float str_to_float(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    float sign = 1.0f;
    if (*s == '-') { sign = -1.0f; s++; } else if (*s == '+') s++;
    double v = 0.0;
    while (*s >= '0' && *s <= '9') v = v * 10.0 + (*s++ - '0');
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * scale; scale *= 0.1; }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int es = 1, e = 0;
        if (*s == '-') { es = -1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') e = e * 10 + (*s++ - '0');
        double m = 1.0;
        for (int i = 0; i < e; i++) m *= 10.0;
        v = es > 0 ? v * m : v / m;
    }
    return (float)(v * sign);
}

u32 hash_str(const char* s) {
    u32 h = 2166136261u;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h;
}

u32 crc32(const void* data, size_t size, u32 seed) {
    static u32 table[256];
    static bool init = false;
    if (!init) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    u32 c = ~seed;
    const u8* p = (const u8*)data;
    for (size_t i = 0; i < size; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
