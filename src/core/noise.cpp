// FIGHTO - noise implementation.
#include "noise.h"

float noise_hash2(int x, int y, u32 seed) {
    u32 h = hash_u32((u32)x * 73856093u ^ hash_u32((u32)y * 19349663u ^ seed));
    return (h >> 8) * (1.0f / 16777216.0f);
}

static inline int wrapi(int v, int p) {
    int r = v % p;
    return r < 0 ? r + p : r;
}

float noise_value_tiled(float x, float y, int period, u32 seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    int x0 = wrapi(xi, period), x1 = wrapi(xi + 1, period);
    int y0 = wrapi(yi, period), y1 = wrapi(yi + 1, period);
    float a = noise_hash2(x0, y0, seed), b = noise_hash2(x1, y0, seed);
    float c = noise_hash2(x0, y1, seed), d = noise_hash2(x1, y1, seed);
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}

float noise_fbm_tiled(float x, float y, int period, int octaves, u32 seed, float gain) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += noise_value_tiled(x, y, period, seed + (u32)i * 1013u) * amp;
        norm += amp;
        x *= 2; y *= 2; period *= 2;
        amp *= gain;
    }
    return sum / norm;
}

void noise_worley_tiled(float x, float y, int period, u32 seed, float* f1, float* edge, u32* cellId) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float d1 = 1e9f, d2 = 1e9f;
    u32 best = 0;
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
            int cx = xi + ox, cy = yi + oy;
            int wx = wrapi(cx, period), wy = wrapi(cy, period);
            float px = (float)cx + noise_hash2(wx, wy, seed);
            float py = (float)cy + noise_hash2(wx, wy, seed ^ 0x9e3779b9u);
            float dx = px - x, dy = py - y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < d1) { d2 = d1; d1 = d; best = (u32)(wx * 7919 + wy); }
            else if (d < d2) d2 = d;
        }
    if (f1) *f1 = d1;
    if (edge) *edge = d2 - d1;
    if (cellId) *cellId = best;
}

float noise_value(float x, float y, u32 seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = noise_hash2(xi, yi, seed), b = noise_hash2(xi + 1, yi, seed);
    float c = noise_hash2(xi, yi + 1, seed), d = noise_hash2(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}

float noise_fbm(float x, float y, int octaves, u32 seed) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += noise_value(x, y, seed + (u32)i * 131u) * amp;
        norm += amp;
        x *= 2.03f; y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}
