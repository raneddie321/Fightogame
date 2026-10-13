// FIGHTO - procedural audio engine. Every sound effect is synthesised at
// startup; loops (rain, city, engines, sirens) and the adaptive music score are
// generated in real time on the audio thread. No external audio files or libraries.
#include "audio.h"
#include "../platform/platform.h"

#define SR AUDIO_SAMPLE_RATE
#define MAX_VOICES 48
#define MAX_CMDS 128

struct Sample {
    float* data;
    int length;
};

struct Voice {
    bool active;
    int snd;
    float pos;
    float pitch;
    float vol;
    bool positional;
    vec3 wpos;
    float gainL, gainR;
};

struct Cmd {
    int snd;
    float vol, pitch;
    bool positional;
    vec3 pos;
};

struct EngineSlot {
    bool active;
    vec3 pos;
    float rpm, load;
    float phase, phase2;
    float vol;
};

static Sample g_samples[SND_COUNT];
static Voice g_voices[MAX_VOICES];
static Cmd g_cmds[MAX_CMDS];
static int g_cmdCount;
static SpinLock g_lock;
static bool g_ready;
static bool g_paused;

// Shared state (written by game thread under lock)
static vec3 g_listenerPos, g_listenerFwd(0, 0, 1);
static float g_master = 0.9f, g_musicVol = 0.6f, g_sfxVol = 0.9f, g_voiceVol = 0.8f;
static EngineSlot g_engines[4];
static vec3 g_sirenPos;
static float g_sirenVol;
static float g_ambRain, g_ambCity, g_ambIndoor, g_ambInvasion;
static int g_musicMood;
static float g_musicIntensity;
static int g_activeVoices;

// ---------------------------------------------------------------------------
// Synthesis helpers (run once at startup)
// ---------------------------------------------------------------------------
static u32 g_seed = 12345;
static float frand() { g_seed = g_seed * 1664525u + 1013904223u; return ((g_seed >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f; }

struct OnePole {
    float a, z;
    void lp(float cutoff) { a = expf(-TAU_F * cutoff / SR); z = 0; }
    float run(float x) { z = x * (1 - a) + z * a; return z; }
};

struct Biquad {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
    void bandpass(float f, float q) {
        float w = TAU_F * f / SR, al = sinf(w) / (2 * q), c = cosf(w);
        float a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        x1 = x2 = y1 = y2 = 0;
    }
    void lowpass(float f, float q) {
        float w = TAU_F * f / SR, al = sinf(w) / (2 * q), c = cosf(w);
        float a0 = 1 + al;
        b0 = (1 - c) * 0.5f / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        x1 = x2 = y1 = y2 = 0;
    }
    void highpass(float f, float q) {
        float w = TAU_F * f / SR, al = sinf(w) / (2 * q), c = cosf(w);
        float a0 = 1 + al;
        b0 = (1 + c) * 0.5f / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        x1 = x2 = y1 = y2 = 0;
    }
    float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

static Sample& alloc(int snd, float seconds) {
    Sample& s = g_samples[snd];
    s.length = (int)(seconds * SR);
    s.data = (float*)calloc((size_t)s.length, sizeof(float));
    return s;
}

static void normalize_sample(Sample& s, float peak) {
    float m = 0;
    for (int i = 0; i < s.length; i++) m = fmaxf(m, fabsf(s.data[i]));
    if (m < 1e-6f) return;
    float k = peak / m;
    for (int i = 0; i < s.length; i++) s.data[i] *= k;
    // Short fade in/out to avoid clicks
    int f = min_t(64, s.length / 4);
    for (int i = 0; i < f; i++) { float t = (float)i / f; s.data[i] *= t; s.data[s.length - 1 - i] *= t; }
}

// Simple feedback comb reverb tail applied in place
static void add_reverb(Sample& s, float mix, float decay) {
    int d1 = 1557, d2 = 1617, d3 = 1491, d4 = 1422;
    float* buf = (float*)calloc((size_t)s.length, sizeof(float));
    for (int i = 0; i < s.length; i++) {
        float x = s.data[i];
        float y = x;
        if (i >= d1) y += buf[i - d1] * decay;
        if (i >= d2) y += buf[i - d2] * decay * 0.9f;
        if (i >= d3) y += buf[i - d3] * decay * 0.85f;
        if (i >= d4) y += buf[i - d4] * decay * 0.8f;
        buf[i] = y * 0.5f;
        s.data[i] = x + buf[i] * mix;
    }
    free(buf);
}

static void gen_thump(Sample& s, float f0, float f1, float decay, float noiseAmt, float noiseF, float amp = 1.0f) {
    float ph = 0;
    Biquad bp;
    bp.bandpass(noiseF, 0.9f);
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / SR;
        float f = f1 + (f0 - f1) * expf(-t * 30.0f);
        ph += TAU_F * f / SR;
        float env = expf(-t / decay);
        float n = bp.run(frand()) * expf(-t * 60.0f) * noiseAmt;
        s.data[i] += (sinf(ph) * env + n) * amp;
    }
}

static void gen_gun(int snd, float len, float boomF, float crack, float tail, float lpStart, float lpEnd) {
    Sample& s = alloc(snd, len);
    Biquad lp;
    float ph = 0;
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / SR;
        float cutoff = lpEnd + (lpStart - lpEnd) * expf(-t * 25.0f);
        if (i % 64 == 0) lp.lowpass(fminf(cutoff, SR * 0.45f), 0.7f);
        float n = lp.run(frand());
        float env = expf(-t / tail) * (1.0f - expf(-t * 4000.0f));
        float crackEnv = expf(-t * 120.0f) * crack;
        ph += TAU_F * boomF * (1.0f + expf(-t * 40.0f)) / SR;
        float boom = sinf(ph) * expf(-t * 18.0f) * 0.8f;
        s.data[i] = n * env + frand() * crackEnv + boom;
    }
    add_reverb(s, 0.35f, 0.55f);
    normalize_sample(s, 0.95f);
}

static void gen_noise_burst(int snd, float len, float f, float q, float decay, float amp) {
    Sample& s = alloc(snd, len);
    Biquad bp;
    bp.bandpass(f, q);
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / SR;
        s.data[i] = bp.run(frand()) * expf(-t / decay) * amp;
    }
    normalize_sample(s, amp);
}

static void gen_tone(int snd, float len, float f0, float f1, float decay, int wave, float amp) {
    Sample& s = alloc(snd, len);
    float ph = 0;
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / SR;
        float f = f0 + (f1 - f0) * (t / len);
        ph += f / SR;
        float x = wave == 0 ? sinf(ph * TAU_F) : wave == 1 ? (fmodf(ph, 1.0f) < 0.5f ? 1.0f : -1.0f) : (2.0f * fmodf(ph, 1.0f) - 1.0f);
        s.data[i] = x * expf(-t / decay) * amp;
    }
    normalize_sample(s, amp);
}

static void gen_formant_voice(int snd, float len, float f0, float f1, float vib, float amp) {
    Sample& s = alloc(snd, len);
    Biquad fa, fb, fc;
    fa.bandpass(700, 4);
    fb.bandpass(1150, 5);
    fc.bandpass(2600, 6);
    float ph = 0;
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / SR;
        float u = t / len;
        float f = f0 + (f1 - f0) * sinf(u * PI_F) + sinf(t * 30.0f) * vib;
        ph += f / SR;
        float saw = 2.0f * fmodf(ph, 1.0f) - 1.0f + frand() * 0.15f;
        float env = sinf(fminf(u * 6.0f, 1.0f) * PI_F * 0.5f) * (1.0f - u);
        s.data[i] = (fa.run(saw) + fb.run(saw) * 0.7f + fc.run(saw) * 0.3f) * env;
    }
    normalize_sample(s, amp);
}

static void synth_all() {
    double t0 = plat_time_seconds();
    // Melee
    { Sample& s = alloc(SND_PUNCH, 0.25f); gen_thump(s, 140, 55, 0.07f, 0.9f, 1800); normalize_sample(s, 0.9f); }
    { Sample& s = alloc(SND_PUNCH_HEAVY, 0.4f); gen_thump(s, 110, 40, 0.12f, 1.1f, 1200); for (int i = 0; i < s.length; i++) s.data[i] = tanhf(s.data[i] * 2.5f); normalize_sample(s, 1.0f); }
    { Sample& s = alloc(SND_KICK, 0.35f); gen_thump(s, 95, 45, 0.1f, 0.6f, 900); normalize_sample(s, 0.95f); }
    {
        Sample& s = alloc(SND_WHOOSH, 0.3f);
        Biquad bp;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR, u = t / 0.3f;
            if (i % 32 == 0) bp.bandpass(400 + u * 1800, 1.2f);
            s.data[i] = bp.run(frand()) * sinf(u * PI_F);
        }
        normalize_sample(s, 0.6f);
    }
    { Sample& s = alloc(SND_BLOCK, 0.25f); gen_thump(s, 200, 90, 0.05f, 1.2f, 2500); normalize_sample(s, 0.8f); }
    {
        Sample& s = alloc(SND_BONE_CRACK, 0.2f);
        for (int k = 0; k < 7; k++) {
            int start = (int)((0.005f + k * 0.012f + frand() * 0.004f) * SR);
            for (int i = 0; i < 90 && start + i < s.length; i++) s.data[start + i] += frand() * expf(-i / 18.0f);
        }
        Biquad hp; hp.highpass(900, 0.7f);
        for (int i = 0; i < s.length; i++) s.data[i] = hp.run(s.data[i]);
        normalize_sample(s, 0.9f);
    }
    for (int v = 0; v < 2; v++) {
        int snd = v == 0 ? SND_SPLAT : SND_SPLASH_BLOOD;
        float len = v == 0 ? 0.35f : 0.6f;
        Sample& s = alloc(snd, len);
        Biquad lp;
        lp.lowpass(1400, 1.5f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float bubble = sinf(t * TAU_F * (180 + 120 * sinf(t * 37.0f))) * 0.4f;
            s.data[i] = (lp.run(frand()) + bubble * expf(-t * 12.0f)) * expf(-t * (v == 0 ? 11.0f : 6.0f));
        }
        normalize_sample(s, 0.8f);
    }
    { Sample& s = alloc(SND_BODYFALL, 0.5f); gen_thump(s, 70, 35, 0.14f, 0.8f, 500); normalize_sample(s, 0.9f); }
    gen_noise_burst(SND_FOOTSTEP, 0.09f, 700, 0.8f, 0.018f, 0.6f);
    {
        Sample& s = alloc(SND_FOOTSTEP_WET, 0.16f);
        Biquad bp, bp2;
        bp.bandpass(600, 0.8f);
        bp2.bandpass(3500, 1.0f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            s.data[i] = bp.run(frand()) * expf(-t / 0.02f) + bp2.run(frand()) * expf(-t / 0.05f) * 0.6f;
        }
        normalize_sample(s, 0.6f);
    }
    // Firearms
    gen_gun(SND_PISTOL, 0.6f, 130, 1.0f, 0.06f, 9000, 900);
    gen_gun(SND_SHOTGUN, 1.0f, 75, 1.2f, 0.16f, 6000, 500);
    gen_gun(SND_SMG, 0.35f, 150, 0.9f, 0.04f, 10000, 1200);
    gen_gun(SND_RIFLE, 0.7f, 110, 1.1f, 0.08f, 11000, 1000);
    gen_gun(SND_MAGNUM, 1.1f, 70, 1.3f, 0.18f, 7000, 600);
    {
        Sample& s = alloc(SND_ARC, 0.35f);
        float ph = 0;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float f = 180 + frand() * 120 + 400 * expf(-t * 20);
            ph += f / SR;
            float sq = fmodf(ph, 1.0f) < 0.5f ? 1.0f : -1.0f;
            s.data[i] = (sq * 0.6f + frand() * 0.5f) * expf(-t * 9.0f);
        }
        normalize_sample(s, 0.8f);
    }
    {
        Sample& s = alloc(SND_LANCE, 1.2f);
        float ph = 0;
        Biquad lp;
        lp.lowpass(800, 0.8f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float whine = t < 0.25f ? sinf((ph += (200 + t * 4000) / SR) * TAU_F) * (t / 0.25f) : 0;
            float boom = t >= 0.22f ? lp.run(frand()) * expf(-(t - 0.22f) * 3.0f) * 2.0f : 0;
            s.data[i] = whine * 0.6f + boom;
        }
        add_reverb(s, 0.4f, 0.6f);
        normalize_sample(s, 0.95f);
    }
    {
        Sample& s = alloc(SND_SHARD, 0.3f);
        float ph = 0, mph = 0;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            mph += 3700.0f / SR;
            ph += (1300.0f + sinf(mph * TAU_F) * 900.0f * expf(-t * 10)) / SR;
            s.data[i] = sinf(ph * TAU_F) * expf(-t * 14.0f);
        }
        normalize_sample(s, 0.7f);
    }
    {
        Sample& s = alloc(SND_RELOAD, 0.5f);
        Biquad bp;
        bp.bandpass(3000, 6);
        for (int k = 0; k < 2; k++) {
            int start = (int)((0.05f + k * 0.28f) * SR);
            for (int i = 0; i < 2200 && start + i < s.length; i++) s.data[start + i] += bp.run(frand()) * expf(-i / 300.0f);
        }
        normalize_sample(s, 0.7f);
    }
    gen_noise_burst(SND_EMPTY, 0.08f, 2500, 5, 0.01f, 0.6f);
    {
        Sample& s = alloc(SND_SHELL, 0.4f);
        for (int k = 0; k < 3; k++) {
            int start = (int)(k * 0.09f * SR);
            float f = 4200 + k * 700;
            for (int i = 0; start + i < s.length; i++) s.data[start + i] += sinf(TAU_F * f * i / SR) * expf(-i / 1200.0f) * 0.5f;
        }
        normalize_sample(s, 0.4f);
    }
    {
        Sample& s = alloc(SND_RICOCHET, 0.45f);
        float ph = 0;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            ph += (3200 - t * 3000 + sinf(t * 90) * 200) / SR;
            s.data[i] = sinf(ph * TAU_F) * expf(-t * 7.0f) + frand() * expf(-t * 80.0f);
        }
        normalize_sample(s, 0.6f);
    }
    { Sample& s = alloc(SND_IMPACT_FLESH, 0.25f); gen_thump(s, 120, 50, 0.05f, 0.7f, 800); normalize_sample(s, 0.7f); }
    {
        Sample& s = alloc(SND_IMPACT_METAL, 0.6f);
        float fr[5] = {523, 1371, 2210, 3127, 4410};
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float v = 0;
            for (int k = 0; k < 5; k++) v += sinf(TAU_F * fr[k] * t) * expf(-t * (6 + k * 3));
            s.data[i] = v + frand() * expf(-t * 90);
        }
        normalize_sample(s, 0.6f);
    }
    gen_noise_burst(SND_IMPACT_CONCRETE, 0.15f, 1800, 0.7f, 0.025f, 0.6f);
    {
        Sample& s = alloc(SND_GLASS, 0.8f);
        for (int k = 0; k < 40; k++) {
            int start = (int)(fabsf(frand()) * 0.5f * SR);
            float f = 3000 + fabsf(frand()) * 5000;
            for (int i = 0; i < 2000 && start + i < s.length; i++) s.data[start + i] += sinf(TAU_F * f * i / SR) * expf(-i / 250.0f) * 0.3f;
        }
        normalize_sample(s, 0.6f);
    }
    {
        Sample& s = alloc(SND_EXPLOSION, 2.6f);
        Biquad lp;
        float ph = 0;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            if (i % 64 == 0) lp.lowpass(fmaxf(120.0f, 4000.0f * expf(-t * 4.0f)), 0.7f);
            ph += (55 * (1 + expf(-t * 8))) / SR;
            s.data[i] = lp.run(frand()) * expf(-t * 1.6f) * 1.5f + sinf(ph * TAU_F) * expf(-t * 3.0f) + frand() * expf(-t * 40) * 0.8f;
        }
        for (int i = 0; i < s.length; i++) s.data[i] = tanhf(s.data[i] * 1.5f);
        add_reverb(s, 0.5f, 0.6f);
        normalize_sample(s, 1.0f);
    }
    { Sample& s = alloc(SND_CAR_DOOR, 0.35f); gen_thump(s, 90, 50, 0.06f, 0.8f, 1100); normalize_sample(s, 0.7f); }
    {
        Sample& s = alloc(SND_CRASH, 1.2f);
        float fr[4] = {310, 870, 1530, 2400};
        Biquad lp;
        lp.lowpass(2500, 0.7f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float v = lp.run(frand()) * expf(-t * 4.0f) * 1.3f;
            for (int k = 0; k < 4; k++) v += sinf(TAU_F * fr[k] * t + sinf(t * 13 * k)) * expf(-t * (5 + k * 2)) * 0.4f;
            s.data[i] = tanhf(v * 1.5f);
        }
        normalize_sample(s, 0.95f);
    }
    {
        Sample& s = alloc(SND_HORN, 0.55f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float a = fmodf(t * 415, 1.0f) < 0.5f ? 1.0f : -1.0f;
            float b = fmodf(t * 520, 1.0f) < 0.5f ? 1.0f : -1.0f;
            s.data[i] = (a + b) * 0.5f * fminf(1.0f, (0.55f - t) * 20.0f);
        }
        Biquad lp; lp.lowpass(2200, 0.7f);
        for (int i = 0; i < s.length; i++) s.data[i] = lp.run(s.data[i]);
        normalize_sample(s, 0.6f);
    }
    {
        Sample& s = alloc(SND_SKID, 0.7f);
        Biquad bp;
        bp.bandpass(1900, 8);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            s.data[i] = bp.run(frand()) * (0.7f + 0.3f * sinf(t * 60)) * sinf(t / 0.7f * PI_F);
        }
        normalize_sample(s, 0.5f);
    }
    {
        Sample& s = alloc(SND_ALIEN_SCREECH, 1.3f);
        float ph = 0, mph = 0;
        Biquad bp;
        bp.bandpass(2400, 2);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR, u = t / 1.3f;
            float f = 600 + 900 * sinf(u * PI_F) + sinf(t * 45) * 60;
            mph += f * 2.7f / SR;
            ph += (f + sinf(mph * TAU_F) * f * 1.5f) / SR;
            float env = sinf(fminf(u * 8, 1.0f) * PI_F * 0.5f) * (1 - u);
            s.data[i] = (sinf(ph * TAU_F) * 0.7f + bp.run(frand()) * 0.6f) * env;
        }
        add_reverb(s, 0.4f, 0.5f);
        normalize_sample(s, 0.9f);
    }
    {
        Sample& s = alloc(SND_ALIEN_GROWL, 1.0f);
        float ph = 0;
        Biquad lp;
        lp.lowpass(600, 2);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR, u = t / 1.0f;
            ph += (62 + sinf(t * 7) * 8) / SR;
            float saw = 2 * fmodf(ph, 1.0f) - 1;
            s.data[i] = lp.run(saw * (0.6f + 0.4f * fabsf(frand()))) * sinf(u * PI_F);
        }
        normalize_sample(s, 0.8f);
    }
    { Sample& s = alloc(SND_ALIEN_HIT, 0.3f); gen_thump(s, 160, 70, 0.05f, 1.0f, 900); normalize_sample(s, 0.7f); }
    gen_tone(SND_DRONE_SHOT, 0.2f, 1600, 350, 0.08f, 0, 0.6f);
    gen_tone(SND_UI_CLICK, 0.05f, 1300, 1100, 0.015f, 0, 0.35f);
    gen_tone(SND_UI_HOVER, 0.03f, 900, 900, 0.01f, 0, 0.15f);
    gen_tone(SND_UI_BACK, 0.06f, 700, 500, 0.02f, 0, 0.3f);
    // Stingers built from chords
    auto chord = [](int snd, float len, const float* notes, int n, float decay, float amp, bool rising) {
        Sample& s = alloc(snd, len);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float v = 0;
            for (int k = 0; k < n; k++) {
                float start = rising ? k * 0.12f : 0.0f;
                if (t < start) continue;
                float tt = t - start;
                float f = notes[k];
                v += (sinf(TAU_F * f * tt) + 0.4f * sinf(TAU_F * f * 2.01f * tt) + 0.25f * (2 * fmodf(f * 0.5f * tt, 1.0f) - 1)) * expf(-tt / decay);
            }
            s.data[i] = v;
        }
        add_reverb(s, 0.5f, 0.6f);
        normalize_sample(s, amp);
    };
    { float n[4] = {110, 164.8f, 220, 261.6f}; chord(SND_MISSION_START, 2.0f, n, 4, 0.8f, 0.7f, true); }
    { float n[5] = {130.8f, 196, 261.6f, 329.6f, 392}; chord(SND_MISSION_COMPLETE, 3.0f, n, 5, 1.2f, 0.75f, true); }
    { float n[3] = {98, 116.5f, 146.8f}; chord(SND_MISSION_FAIL, 2.5f, n, 3, 1.2f, 0.7f, false); }
    { float n[2] = {880, 1318.5f}; chord(SND_NOTIFY, 0.4f, n, 2, 0.08f, 0.3f, true); }
    { float n[3] = {660, 990, 1320}; chord(SND_PICKUP, 0.4f, n, 3, 0.07f, 0.4f, true); }
    { float n[4] = {1568, 2093, 2637, 3136}; chord(SND_MONEY, 0.6f, n, 4, 0.12f, 0.45f, true); }
    { float n[3] = {523, 784, 1046}; chord(SND_CHECKPOINT, 0.8f, n, 3, 0.3f, 0.4f, true); }
    { float n[2] = {1174.7f, 880}; chord(SND_ELEVATOR, 1.2f, n, 2, 0.5f, 0.5f, true); }
    {
        Sample& s = alloc(SND_THUNDER, 4.5f);
        Biquad lp;
        lp.lowpass(300, 0.7f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            float rumble = lp.run(frand()) * (expf(-t * 0.8f) * (0.6f + 0.4f * sinf(t * 3.1f) * sinf(t * 1.7f)));
            float crack = t < 0.3f ? frand() * expf(-t * 15) * 0.6f : 0;
            s.data[i] = rumble * 2.5f + crack;
        }
        normalize_sample(s, 0.9f);
    }
    {
        Sample& s = alloc(SND_HEARTBEAT, 0.9f);
        gen_thump(s, 60, 40, 0.06f, 0.0f, 100);
        Sample tmp;
        tmp.length = s.length;
        int off = (int)(0.25f * SR);
        for (int i = s.length - 1; i >= off; i--) s.data[i] += s.data[i - off] * 0.7f;
        FIGHTO_UNUSED(tmp);
        normalize_sample(s, 0.7f);
    }
    gen_formant_voice(SND_GRUNT, 0.28f, 115, 140, 6, 0.6f);
    gen_formant_voice(SND_SCREAM, 1.0f, 320, 520, 25, 0.7f);
    {
        Sample& s = alloc(SND_DIALOGUE_BLIP, 0.09f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR;
            s.data[i] = (sinf(TAU_F * 1450 * t) * 0.5f + frand() * 0.2f) * expf(-t * 35);
        }
        normalize_sample(s, 0.25f);
    }
    {
        Sample& s = alloc(SND_SHARD_PULSE, 2.2f);
        float ph = 0;
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR, u = t / 2.2f;
            ph += (55 + 30 * u) / SR;
            float shimmer = 0;
            for (int k = 1; k < 6; k++) shimmer += sinf(TAU_F * (880 * k * 1.003f) * t) / k;
            s.data[i] = (sinf(ph * TAU_F) * 0.8f + shimmer * 0.15f * u) * sinf(u * PI_F);
        }
        add_reverb(s, 0.6f, 0.65f);
        normalize_sample(s, 0.8f);
    }
    {
        Sample& s = alloc(SND_CROWD_CHEER, 2.4f);
        Biquad b1, b2;
        b1.bandpass(800, 1.5f);
        b2.bandpass(1600, 2.0f);
        for (int i = 0; i < s.length; i++) {
            float t = (float)i / SR, u = t / 2.4f;
            float am = 0.6f + 0.4f * sinf(t * 23.0f) * sinf(t * 7.0f);
            s.data[i] = (b1.run(frand()) + b2.run(frand()) * 0.7f) * am * sinf(fminf(u * 4, 1.0f) * PI_F * 0.5f) * (1 - u * 0.6f);
        }
        normalize_sample(s, 0.6f);
    }
    log_info("Synthesised %d sound effects in %.2fs", SND_COUNT, plat_time_seconds() - t0);
}

// ---------------------------------------------------------------------------
// Public API (game thread)
// ---------------------------------------------------------------------------
bool audio_init() {
    synth_all();
    g_ready = true;
    return true;
}

void audio_shutdown() {
    g_lock.lock();
    g_ready = false;
    g_lock.unlock();
    for (int i = 0; i < SND_COUNT; i++) { free(g_samples[i].data); g_samples[i].data = 0; }
}

static void push_cmd(int snd, float vol, float pitch, bool positional, vec3 pos) {
    if (!g_ready || snd < 0 || snd >= SND_COUNT) return;
    g_lock.lock();
    if (g_cmdCount < MAX_CMDS) {
        Cmd& c = g_cmds[g_cmdCount++];
        c.snd = snd;
        c.vol = vol;
        c.pitch = pitch;
        c.positional = positional;
        c.pos = pos;
    }
    g_lock.unlock();
}

void audio_play(int snd, float vol, float pitch) { push_cmd(snd, vol, pitch, false, vec3(0, 0, 0)); }
void audio_play_at(int snd, vec3 pos, float vol, float pitch) {
    if (length_sq(pos - g_listenerPos) > 160.0f * 160.0f) return;
    push_cmd(snd, vol, pitch, true, pos);
}

void audio_set_listener(vec3 pos, vec3 fwd) {
    g_lock.lock();
    g_listenerPos = pos;
    g_listenerFwd = normalize(flat(fwd));
    g_lock.unlock();
}

void audio_set_volumes(float master, float music, float sfx, float voice) {
    g_lock.lock();
    g_master = master; g_musicVol = music; g_sfxVol = sfx; g_voiceVol = voice;
    g_lock.unlock();
}

void audio_engine(int slot, vec3 pos, float rpm, float load, bool active) {
    if (slot < 0 || slot >= 4) return;
    g_lock.lock();
    g_engines[slot].active = active;
    g_engines[slot].pos = pos;
    g_engines[slot].rpm = rpm;
    g_engines[slot].load = load;
    g_lock.unlock();
}

void audio_siren(vec3 pos, float vol) {
    g_lock.lock();
    g_sirenPos = pos;
    g_sirenVol = vol;
    g_lock.unlock();
}

void audio_set_ambience(float rain, float city, float indoor, float invasion) {
    g_lock.lock();
    g_ambRain = rain; g_ambCity = city; g_ambIndoor = indoor; g_ambInvasion = invasion;
    g_lock.unlock();
}

void audio_set_music(int mood, float intensity) {
    g_lock.lock();
    g_musicMood = mood;
    g_musicIntensity = intensity;
    g_lock.unlock();
}

void audio_pause(bool p) { g_paused = p; }
void audio_frame() {}
int audio_active_voices() { return g_activeVoices; }

float audio_debug_render_peak(int snd) {
    if (snd < 0 || snd >= SND_COUNT || !g_samples[snd].data) return 0;
    float m = 0;
    for (int i = 0; i < g_samples[snd].length; i++) m = fmaxf(m, fabsf(g_samples[snd].data[i]));
    return m;
}

// ---------------------------------------------------------------------------
// Audio thread: mixing
// ---------------------------------------------------------------------------
static void spatialize(vec3 pos, vec3 lpos, vec3 lfwd, float* gl, float* gr) {
    vec3 d = pos - lpos;
    float dist = length(d);
    float att = 1.0f / (1.0f + dist * 0.09f + dist * dist * 0.0012f);
    if (dist > 150.0f) att = 0;
    vec3 right(-lfwd.z, 0, lfwd.x);
    float pan = dist > 0.5f ? dot(normalize(flat(d)), right) : 0.0f;
    float l = sqrtf(0.5f * (1.0f - pan)), r = sqrtf(0.5f * (1.0f + pan));
    *gl = att * l * 1.41f;
    *gr = att * r * 1.41f;
}

struct MusicState {
    double t;
    float padPh[6];
    float bassPh;
    float arpPh;
    float kickEnv, snareEnv, hatEnv;
    float kickPh;
    int lastStep;
    float lpz[2];
    float mood, inten;
    u32 noise;
    float alienPh[3];
};
static MusicState M;

static float mnoise() { M.noise = M.noise * 1664525u + 1013904223u; return ((M.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f; }

static float note_hz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

static void music_sample(float* l, float* r, int mood, float intensity) {
    if (mood == MUSIC_NONE) { *l = *r = 0; return; }
    M.t += 1.0 / SR;
    float bpm = (mood == MUSIC_COMBAT || mood == MUSIC_CHASE) ? 118.0f : (mood == MUSIC_TENSION ? 96.0f : 76.0f);
    double beat = M.t * bpm / 60.0;
    int step = (int)(beat * 4.0);
    // Dark minor progression: Dm - Bb - Gm - A (per bar)
    static const int roots[4] = {50, 46, 43, 45};
    static const int minor[4] = {1, 0, 1, 0};
    int bar = ((int)(beat / 4.0)) & 3;
    int root = roots[bar];
    int third = root + (minor[bar] ? 3 : 4);
    int fifth = root + 7;
    float combat = (mood == MUSIC_COMBAT || mood == MUSIC_CHASE) ? 1.0f : 0.0f;
    float alien = mood == MUSIC_ALIEN ? 1.0f : 0.0f;
    // Pad (detuned saws, low-passed)
    int notes[6] = {root + 12, third + 12, fifth + 12, root + 24, third + 24, fifth};
    float pad = 0;
    for (int k = 0; k < 6; k++) {
        float f = note_hz(notes[k]) * (1.0f + (k % 2 ? 0.003f : -0.003f));
        M.padPh[k] += f / SR;
        if (M.padPh[k] > 1.0f) M.padPh[k] -= 1.0f;
        pad += 2.0f * M.padPh[k] - 1.0f;
    }
    pad *= 0.08f;
    float cut = 0.02f + 0.03f * intensity + 0.02f * sinf((float)M.t * 0.3f);
    M.lpz[0] += (pad - M.lpz[0]) * cut;
    M.lpz[1] += (M.lpz[0] - M.lpz[1]) * cut;
    float padOut = M.lpz[1];
    // Bass: pulses on eighth notes
    float bassF = note_hz(root - 12);
    M.bassPh += bassF / SR;
    if (M.bassPh > 1.0f) M.bassPh -= 1.0f;
    float eighth = (float)fmod(beat * 2.0, 1.0);
    float bassEnv = expf(-eighth * (combat > 0 ? 6.0f : 2.5f));
    float bass = (2.0f * M.bassPh - 1.0f) * 0.5f + sinf(M.bassPh * TAU_F) * 0.6f;
    bass *= bassEnv * (0.2f + 0.25f * intensity);
    // Arpeggio (tension / combat)
    static const int arpSeq[8] = {0, 7, 12, 3, 7, 15, 12, 7};
    int arpNote = root + 24 + arpSeq[step & 7] + (minor[bar] ? 0 : 1) * (arpSeq[step & 7] == 3 ? 1 : 0);
    M.arpPh += note_hz(arpNote) / SR;
    float sixteenth = (float)fmod(beat * 4.0, 1.0);
    float arp = (fmodf(M.arpPh, 1.0f) < 0.5f ? 1.0f : -1.0f) * expf(-sixteenth * 8.0f) * 0.05f * (mood == MUSIC_TENSION ? 1.0f : combat) * intensity;
    // Drums
    if (step != M.lastStep) {
        M.lastStep = step;
        int s16 = step & 15;
        if (combat > 0 || mood == MUSIC_TENSION) {
            if (s16 == 0 || s16 == 8 || (combat > 0 && (s16 == 6 || s16 == 10))) { M.kickEnv = 1.0f; M.kickPh = 0; }
            if (combat > 0 && (s16 == 4 || s16 == 12)) M.snareEnv = 1.0f;
            if (combat > 0 && (s16 & 1) == 0) M.hatEnv = 0.6f;
        } else if (mood == MUSIC_CALM || mood == MUSIC_MENU) {
            if (s16 == 0) { M.kickEnv = 0.5f; M.kickPh = 0; }
        }
    }
    M.kickPh += (50.0f + 90.0f * M.kickEnv) / SR;
    float kick = sinf(M.kickPh * TAU_F) * M.kickEnv * 0.55f;
    M.kickEnv *= 0.9993f;
    float snare = mnoise() * M.snareEnv * 0.25f;
    M.snareEnv *= 0.9985f;
    float hat = mnoise() * M.hatEnv * 0.06f;
    M.hatEnv *= 0.995f;
    // Alien drones: tritone beating and metallic pings
    float al = 0;
    if (alien > 0 || mood == MUSIC_ALIEN) {
        M.alienPh[0] += 55.0f / SR;
        M.alienPh[1] += 77.78f / SR;
        M.alienPh[2] += (880.0f + 40.0f * sinf((float)M.t)) / SR;
        al = sinf(M.alienPh[0] * TAU_F) * 0.15f + sinf(M.alienPh[1] * TAU_F) * 0.12f;
        float ping = sinf(M.alienPh[2] * TAU_F) * expf(-(float)fmod(beat, 2.0) * 3.0f) * 0.05f;
        al += ping;
    }
    float mix = padOut * (mood == MUSIC_ALIEN ? 0.5f : 1.0f) + bass + arp + (kick + snare + hat) * (0.5f + 0.5f * intensity) + al;
    float stereo = sinf((float)M.t * 0.5f) * 0.15f;
    *l = mix * (1.0f - stereo);
    *r = mix * (1.0f + stereo);
}

struct AmbState {
    float rainLP, rainHP, cityLP, sirenPh, engPh[4][2], indoorLP;
    u32 n;
    float dropEnv;
    float boomEnv, boomPh;
};
static AmbState A;
static float anoise() { A.n = A.n * 1664525u + 1013904223u; return ((A.n >> 8) & 0xFFFF) / 32768.0f - 1.0f; }

void game_audio_mix(float* out, int frames) {
    memset(out, 0, sizeof(float) * (size_t)frames * 2);
    if (!g_ready) return;
    // Snapshot shared state
    Cmd cmds[MAX_CMDS];
    int ncmd;
    vec3 lpos, lfwd;
    float master, musicVol, sfxVol;
    EngineSlot eng[4];
    vec3 sirenPos;
    float sirenVol, ambRain, ambCity, ambIndoor, ambInv;
    int mood;
    float inten;
    g_lock.lock();
    ncmd = g_cmdCount;
    memcpy(cmds, g_cmds, sizeof(Cmd) * (size_t)ncmd);
    g_cmdCount = 0;
    lpos = g_listenerPos; lfwd = g_listenerFwd;
    master = g_master; musicVol = g_musicVol; sfxVol = g_sfxVol;
    memcpy(eng, g_engines, sizeof(eng));
    sirenPos = g_sirenPos; sirenVol = g_sirenVol;
    ambRain = g_ambRain; ambCity = g_ambCity; ambIndoor = g_ambIndoor; ambInv = g_ambInvasion;
    mood = g_musicMood; inten = g_musicIntensity;
    g_lock.unlock();
    if (g_paused) return;
    // Start new voices
    for (int c = 0; c < ncmd; c++) {
        int slot = -1;
        float quietest = 1e9f;
        for (int v = 0; v < MAX_VOICES; v++) {
            if (!g_voices[v].active) { slot = v; break; }
            float loud = g_voices[v].vol * (g_voices[v].gainL + g_voices[v].gainR);
            if (loud < quietest) { quietest = loud; slot = v; }
        }
        Voice& v = g_voices[slot];
        v.active = true;
        v.snd = cmds[c].snd;
        v.pos = 0;
        v.pitch = cmds[c].pitch;
        v.vol = cmds[c].vol;
        v.positional = cmds[c].positional;
        v.wpos = cmds[c].pos;
        if (v.positional) spatialize(v.wpos, lpos, lfwd, &v.gainL, &v.gainR);
        else v.gainL = v.gainR = 1.0f;
    }
    int active = 0;
    for (int vi = 0; vi < MAX_VOICES; vi++) {
        Voice& v = g_voices[vi];
        if (!v.active) continue;
        active++;
        const Sample& s = g_samples[v.snd];
        if (!s.data) { v.active = false; continue; }
        if (v.positional) spatialize(v.wpos, lpos, lfwd, &v.gainL, &v.gainR);
        float gl = v.gainL * v.vol * sfxVol, gr = v.gainR * v.vol * sfxVol;
        for (int i = 0; i < frames; i++) {
            int idx = (int)v.pos;
            if (idx >= s.length - 1) { v.active = false; break; }
            float fr = v.pos - idx;
            float x = s.data[idx] * (1 - fr) + s.data[idx + 1] * fr;
            out[i * 2] += x * gl;
            out[i * 2 + 1] += x * gr;
            v.pos += v.pitch;
        }
    }
    g_activeVoices = active;
    // Engines
    for (int e = 0; e < 4; e++) {
        if (!eng[e].active) continue;
        float gl, gr;
        spatialize(eng[e].pos, lpos, lfwd, &gl, &gr);
        float f = 32.0f + eng[e].rpm * 140.0f;
        float vol = (0.12f + eng[e].load * 0.12f) * sfxVol;
        for (int i = 0; i < frames; i++) {
            A.engPh[e][0] += f / SR;
            A.engPh[e][1] += f * 2.0f / SR;
            float a = 2.0f * fmodf(A.engPh[e][0], 1.0f) - 1.0f;
            float b = fmodf(A.engPh[e][1], 1.0f) < 0.3f ? 1.0f : -0.4f;
            float x = (a * 0.6f + b * 0.3f + anoise() * 0.05f) * vol;
            out[i * 2] += x * gl;
            out[i * 2 + 1] += x * gr;
        }
        if (A.engPh[e][0] > 1000) A.engPh[e][0] -= 1000;
        if (A.engPh[e][1] > 1000) A.engPh[e][1] -= 1000;
    }
    // Siren (wail)
    if (sirenVol > 0.01f) {
        float gl, gr;
        spatialize(sirenPos, lpos, lfwd, &gl, &gr);
        for (int i = 0; i < frames; i++) {
            static float sweep = 0;
            sweep += 1.0f / SR;
            float f = 700.0f + 550.0f * (0.5f + 0.5f * sinf(sweep * TAU_F * 0.35f));
            A.sirenPh += f / SR;
            float x = (fmodf(A.sirenPh, 1.0f) < 0.5f ? 0.5f : -0.5f) * 0.18f * sirenVol * sfxVol;
            out[i * 2] += x * gl;
            out[i * 2 + 1] += x * gr;
        }
        if (A.sirenPh > 1000) A.sirenPh -= 1000;
    }
    // Ambience
    float rainVol = ambRain * (1.0f - ambIndoor * 0.85f) * 0.22f * sfxVol;
    float cityVol = ambCity * (1.0f - ambIndoor * 0.7f) * 0.08f * sfxVol;
    for (int i = 0; i < frames; i++) {
        float n = anoise();
        A.rainLP += (n - A.rainLP) * 0.35f;
        A.rainHP = n - A.rainLP;
        float rain = (A.rainLP * 0.5f + A.rainHP * 0.25f) * rainVol;
        if (ambRain > 0.1f && (A.n & 0x7FF) == 3) A.dropEnv = 1.0f;
        rain += anoise() * A.dropEnv * 0.15f * rainVol;
        A.dropEnv *= 0.995f;
        A.cityLP += (anoise() - A.cityLP) * 0.004f;
        float city = A.cityLP * 4.0f * cityVol;
        A.indoorLP += (anoise() - A.indoorLP) * 0.002f;
        float room = A.indoorLP * 3.0f * ambIndoor * 0.05f * sfxVol;
        float inv = 0;
        if (ambInv > 0.01f) {
            if ((A.n & 0x1FFFF) == 7) { A.boomEnv = 1.0f; A.boomPh = 0; }
            A.boomPh += 40.0f / SR;
            inv = (sinf(A.boomPh * TAU_F) * A.boomEnv + A.cityLP * 2.0f) * 0.12f * ambInv * sfxVol;
            A.boomEnv *= 0.99985f;
        }
        float ml, mr;
        music_sample(&ml, &mr, mood, inten);
        out[i * 2] += rain + city + room + inv + ml * musicVol;
        out[i * 2 + 1] += rain * 0.95f + city + room + inv + mr * musicVol;
    }
    // Master + soft clip
    for (int i = 0; i < frames * 2; i++) out[i] = tanhf(out[i] * master);
}
