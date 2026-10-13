// FIGHTO - core base definitions.
// Engine-free foundation shared by every platform (Windows, Linux, Android).
// The code base deliberately avoids the C++ standard library so the same
// sources compile for desktop and for the freestanding Android build.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define FIGHTO_UNUSED(x) (void)(x)

#if defined(_WIN32)
#define FIGHTO_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#define FIGHTO_PLATFORM_ANDROID 1
#else
#define FIGHTO_PLATFORM_LINUX 1
#endif

#if defined(__ANDROID__) || defined(FIGHTO_GLES)
#define FIGHTO_MOBILE_GL 1
#endif

// Placement new without <new>.
struct PlacementTag {};
inline void* operator new(size_t, PlacementTag, void* p) noexcept { return p; }
inline void operator delete(void*, PlacementTag, void*) noexcept {}

template <typename T> inline T min_t(T a, T b) { return a < b ? a : b; }
template <typename T> inline T max_t(T a, T b) { return a > b ? a : b; }
template <typename T> inline T clamp_t(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> inline void swap_t(T& a, T& b) { T t = a; a = b; b = t; }

// ---------------------------------------------------------------------------
// Logging / formatting (implemented in base.cpp, routed to the platform layer)
// ---------------------------------------------------------------------------
int fmt_strv(char* buf, int cap, const char* f, va_list ap);
int fmt_str(char* buf, int cap, const char* f, ...);
void log_info(const char* f, ...);
void log_error(const char* f, ...);

// String helpers
void str_copy(char* dst, int cap, const char* src);
bool str_eq(const char* a, const char* b);
bool str_starts(const char* s, const char* prefix);
int str_to_int(const char* s);
float str_to_float(const char* s);
u32 hash_str(const char* s);
u32 crc32(const void* data, size_t size, u32 seed = 0);

// ---------------------------------------------------------------------------
// Dynamic array for trivially copyable types. Zero-initialised == empty.
// Memory is released explicitly with free_mem() (POD friendly semantics).
// ---------------------------------------------------------------------------
template <typename T> struct Array {
    T* data;
    int count;
    int cap;

    void reserve(int n) {
        if (n <= cap) return;
        int nc = cap ? cap : 8;
        while (nc < n) nc *= 2;
        data = (T*)realloc(data, sizeof(T) * (size_t)nc);
        cap = nc;
    }
    T& push(const T& v) {
        if (count >= cap) reserve(count + 1);
        data[count] = v;
        return data[count++];
    }
    T& push_zero() {
        if (count >= cap) reserve(count + 1);
        memset(&data[count], 0, sizeof(T));
        return data[count++];
    }
    void resize(int n) {
        reserve(n);
        if (n > count) memset(data + count, 0, sizeof(T) * (size_t)(n - count));
        count = n;
    }
    void pop() { if (count > 0) count--; }
    void clear() { count = 0; }
    void remove_swap(int i) { data[i] = data[count - 1]; count--; }
    void remove_ordered(int i) {
        if (i < count - 1) memmove(data + i, data + i + 1, sizeof(T) * (size_t)(count - i - 1));
        count--;
    }
    void free_mem() { free(data); data = 0; count = 0; cap = 0; }
    T& operator[](int i) { return data[i]; }
    const T& operator[](int i) const { return data[i]; }
    T& last() { return data[count - 1]; }
    T* begin() { return data; }
    T* end() { return data + count; }
};

// ---------------------------------------------------------------------------
// Random numbers (PCG32)
// ---------------------------------------------------------------------------
struct Rng {
    u64 state;
    u64 inc;
    void seed(u64 s, u64 seq = 54u) {
        state = 0; inc = (seq << 1u) | 1u; next(); state += s; next();
    }
    u32 next() {
        u64 old = state;
        state = old * 6364136223846793005ULL + inc;
        u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
        u32 rot = (u32)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    float f01() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int irange(int a, int b) { return a + (int)(next() % (u32)(b - a + 1)); } // inclusive
    bool chance(float p) { return f01() < p; }
};

inline u32 hash_u32(u32 x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float hash_f01(u32 x) { return (hash_u32(x) >> 8) * (1.0f / 16777216.0f); }

// Spin lock used between the game thread and the audio thread.
struct SpinLock {
    volatile bool flag;
    void lock() { while (__atomic_test_and_set(&flag, __ATOMIC_ACQUIRE)) {} }
    void unlock() { __atomic_clear(&flag, __ATOMIC_RELEASE); }
};
