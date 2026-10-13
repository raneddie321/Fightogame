// FIGHTO - script tokenizer implementation.
#include "script.h"

bool script_next_line(const char** cursor, ScriptLine* out, int* lineNo) {
    const char* p = *cursor;
    while (*p) {
        // Read one physical line
        const char* start = p;
        while (*p && *p != '\n') p++;
        int len = (int)(p - start);
        if (*p == '\n') p++;
        if (lineNo) (*lineNo)++;
        if (len >= (int)sizeof(out->buf)) len = (int)sizeof(out->buf) - 1;
        memcpy(out->buf, start, (size_t)len);
        out->buf[len] = 0;
        out->argc = 0;
        out->lineNo = lineNo ? *lineNo : 0;
        char* q = out->buf;
        while (*q && out->argc < SCRIPT_MAX_ARGS) {
            while (*q == ' ' || *q == '\t' || *q == '\r') q++;
            if (!*q || *q == '#') break;
            if (*q == '"') {
                q++;
                out->argv[out->argc++] = q;
                while (*q && *q != '"') q++;
                if (*q) *q++ = 0;
            } else {
                out->argv[out->argc++] = q;
                while (*q && *q != ' ' && *q != '\t' && *q != '\r') q++;
                if (*q) *q++ = 0;
            }
        }
        if (out->argc > 0) {
            *cursor = p;
            return true;
        }
    }
    *cursor = p;
    return false;
}

float script_float(const ScriptLine& l, int i, float def) { return i < l.argc ? str_to_float(l.argv[i]) : def; }
int script_int(const ScriptLine& l, int i, int def) { return i < l.argc ? str_to_int(l.argv[i]) : def; }
const char* script_str(const ScriptLine& l, int i, const char* def) { return i < l.argc ? l.argv[i] : def; }
