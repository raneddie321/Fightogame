// FIGHTO - tiny line-based script tokenizer used by missions, dialogue and cutscenes.
#pragma once
#include "../core/base.h"

#define SCRIPT_MAX_ARGS 16

struct ScriptLine {
    int argc;
    const char* argv[SCRIPT_MAX_ARGS];
    char buf[640];
    int lineNo;
};

// Iterates over lines of a text buffer. Returns false at the end.
// Tokens are separated by whitespace; "double quoted strings" form one token; '#' starts a comment.
bool script_next_line(const char** cursor, ScriptLine* out, int* lineNo);
float script_float(const ScriptLine& l, int i, float def = 0.0f);
int script_int(const ScriptLine& l, int i, int def = 0);
const char* script_str(const ScriptLine& l, int i, const char* def = "");
