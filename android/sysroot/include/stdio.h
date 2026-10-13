#pragma once
// Only what FIGHTO (and the JDK's jni.h) needs; file I/O on Android goes through
// AAssetManager (game data) and open/read/write (user data).
#include <sys/types.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct __sFILE FILE;
int rename(const char* from, const char* to);
int remove(const char* path);
int sprintf(char* out, const char* fmt, ...);
int snprintf(char* out, size_t n, const char* fmt, ...);
int vsnprintf(char* out, size_t n, const char* fmt, va_list ap);
#ifdef __cplusplus
}
#endif
