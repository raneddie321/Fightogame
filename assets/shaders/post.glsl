// FIGHTO post-processing: bloom down/upsampling and the final cinematic composite.
// Variants: DOWNSAMPLE, UPSAMPLE, COMPOSITE

#ifdef VERTEX
layout(location = 0) in vec3 aPos;
out vec2 vUV;
void main() {
    vUV = aPos.xy * 0.5 + 0.5;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
uniform highp sampler2D uTex;
uniform vec2 uTexel;
layout(location = 0) out vec4 oColor;

#ifdef DOWNSAMPLE
uniform float uThreshold;   // > 0 for the first (prefilter) pass
uniform float uDecode;
vec3 tap(vec2 o) { return texture(uTex, vUV + o * uTexel).rgb * uDecode; }
void main() {
    vec3 a = tap(vec2(-2.0, -2.0)), b = tap(vec2(0.0, -2.0)), c = tap(vec2(2.0, -2.0));
    vec3 d = tap(vec2(-1.0, -1.0)), e = tap(vec2(1.0, -1.0));
    vec3 f = tap(vec2(-2.0, 0.0)), g = tap(vec2(0.0, 0.0)), h = tap(vec2(2.0, 0.0));
    vec3 i = tap(vec2(-1.0, 1.0)), j = tap(vec2(1.0, 1.0));
    vec3 k = tap(vec2(-2.0, 2.0)), l = tap(vec2(0.0, 2.0)), m = tap(vec2(2.0, 2.0));
    vec3 col = (d + e + i + j) * 0.125 + (a + b + f + g) * 0.03125 + (b + c + g + h) * 0.03125 +
               (f + g + k + l) * 0.03125 + (g + h + l + m) * 0.03125;
    if (uThreshold > 0.0) {
        float br = max(col.r, max(col.g, col.b));
        float soft = clamp(br - uThreshold + 0.5, 0.0, 1.0);
        soft = soft * soft * 0.5;
        float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
        col *= contrib;
        col = min(col, vec3(60.0));
    }
    oColor = vec4(col, 1.0);
}
#endif

#ifdef UPSAMPLE
uniform float uRadius;
void main() {
    vec2 o = uTexel * uRadius;
    vec3 c = texture(uTex, vUV + vec2(-o.x, -o.y)).rgb + texture(uTex, vUV + vec2(0.0, -o.y)).rgb * 2.0 +
             texture(uTex, vUV + vec2(o.x, -o.y)).rgb + texture(uTex, vUV + vec2(-o.x, 0.0)).rgb * 2.0 +
             texture(uTex, vUV).rgb * 4.0 + texture(uTex, vUV + vec2(o.x, 0.0)).rgb * 2.0 +
             texture(uTex, vUV + vec2(-o.x, o.y)).rgb + texture(uTex, vUV + vec2(0.0, o.y)).rgb * 2.0 +
             texture(uTex, vUV + vec2(o.x, o.y)).rgb;
    oColor = vec4(c / 16.0, 1.0);
}
#endif

#ifdef COMPOSITE
uniform highp sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uDecode;
uniform float uVignette;
uniform float uGrain;
uniform float uDamage;        // red damage vignette 0..1
uniform float uLowHealth;     // pulsing low health 0..1
uniform float uBloodScreen;   // blood splatter on screen 0..1
uniform float uBloodSeed;
uniform float uLetterbox;     // 0..1
uniform float uFade;          // 0 = visible, 1 = black
uniform float uWhiteFlash;
uniform float uSaturation;
uniform float uBrightness;
uniform float uChroma;
uniform float uAlienTint;     // shard vision
uniform float uFxaa;

vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 sample_scene(vec2 uv) { return texture(uTex, uv).rgb * uDecode; }

vec3 fxaa(vec2 uv) {
    vec3 rgbM = sample_scene(uv);
    vec3 rgbNW = sample_scene(uv + vec2(-1.0, -1.0) * uTexel);
    vec3 rgbNE = sample_scene(uv + vec2(1.0, -1.0) * uTexel);
    vec3 rgbSW = sample_scene(uv + vec2(-1.0, 1.0) * uTexel);
    vec3 rgbSE = sample_scene(uv + vec2(1.0, 1.0) * uTexel);
    vec3 lw = vec3(0.299, 0.587, 0.114);
    float lNW = dot(aces(rgbNW), lw), lNE = dot(aces(rgbNE), lw), lSW = dot(aces(rgbSW), lw), lSE = dot(aces(rgbSE), lw), lM = dot(aces(rgbM), lw);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uTexel;
    vec3 a = 0.5 * (sample_scene(uv + dir * (1.0 / 3.0 - 0.5)) + sample_scene(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 b = a * 0.5 + 0.25 * (sample_scene(uv + dir * -0.5) + sample_scene(uv + dir * 0.5));
    float lb = dot(aces(b), lw);
    if (lb < lMin || lb > lMax) return a;
    return b;
}

void main() {
    vec2 uv = vUV;
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    vec3 col;
    if (uChroma > 0.0) {
        vec2 off = c * r2 * uChroma * 0.006;
        col = vec3(sample_scene(uv + off).r, sample_scene(uv).g, sample_scene(uv - off).b);
    } else if (uFxaa > 0.5) {
        col = fxaa(uv);
    } else {
        col = sample_scene(uv);
    }
    vec3 bloom = texture(uBloom, uv).rgb;
    col += bloom * uBloomStrength;
    col *= uExposure;

    // Shard vision: alien energy tint
    col = mix(col, col * vec3(0.4, 1.1, 1.4) + vec3(0.0, 0.03, 0.05), uAlienTint * 0.6);

    col = aces(col);
    // Cinematic grade: cool shadows, warm highlights
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowTint = vec3(0.92, 1.0, 1.06);
    vec3 highTint = vec3(1.06, 1.0, 0.92);
    col *= mix(shadowTint, highTint, smoothstep(0.1, 0.7, lum));
    col = mix(vec3(lum), col, uSaturation);
    col = pow(col, vec3(1.0 / 2.2));
    col = col * uBrightness;

    // Vignette
    col *= 1.0 - uVignette * smoothstep(0.15, 0.8, r2 * 1.6);
    // Damage / low health
    float edge = smoothstep(0.08, 0.55, r2 * 1.8);
    float pulse = 0.6 + 0.4 * sin(uTime * 6.0);
    vec3 bloodRed = vec3(0.45, 0.0, 0.0);
    col = mix(col, bloodRed, clamp(edge * (uDamage * 0.85 + uLowHealth * pulse * 0.6), 0.0, 0.9));
    if (uLowHealth > 0.0) col = mix(col, vec3(dot(col, vec3(0.33))), uLowHealth * 0.5);
    // Blood splatter on the camera lens
    if (uBloodScreen > 0.0) {
        float total = 0.0;
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            vec2 ctr = vec2(hash12(vec2(fi, uBloodSeed)), hash12(vec2(uBloodSeed, fi * 3.1)));
            ctr = mix(vec2(0.05), vec2(0.95), ctr);
            vec2 d = (uv - ctr) * vec2(1.7, 1.0);
            float ang = atan(d.y, d.x);
            float rr = 0.08 + 0.1 * hash12(vec2(fi * 7.0, uBloodSeed));
            float shape = rr * (0.7 + 0.5 * vnoise(vec2(ang * 3.0 + fi, uBloodSeed)));
            float drip = smoothstep(0.012, 0.0, abs(d.x + 0.01 * sin(d.y * 40.0))) * step(d.y, 0.0) * step(-0.25 * rr * 6.0, d.y);
            total = max(total, smoothstep(shape, shape * 0.8, length(d)) + drip * 0.8);
        }
        total *= uBloodScreen;
        col = mix(col, vec3(0.28, 0.0, 0.01) + col * 0.15, clamp(total, 0.0, 0.92));
    }
    // Film grain
    float g = hash12(uv * vec2(1920.0, 1080.0) + fract(uTime) * 100.0) - 0.5;
    col += g * uGrain;
    // Letterbox bars
    float bar = uLetterbox * 0.12;
    if (uv.y < bar || uv.y > 1.0 - bar) col = vec3(0.0);
    col = mix(col, vec3(1.0), uWhiteFlash);
    col *= 1.0 - uFade;
    oColor = vec4(col, 1.0);
}
#endif
#endif
