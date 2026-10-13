// FIGHTO shared shader code (GLSL 330 core / GLSL ES 300).
#define PI 3.14159265

uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uSunDir;          // direction towards the moon/sun
uniform vec3 uSunColor;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uAmbientUp;
uniform vec3 uAmbientDown;
uniform vec3 uFogColor;
uniform vec4 uFogParams;       // x density, y height falloff, z base height, w max opacity
uniform float uRain;           // wetness 0..1
uniform float uFlash;          // lightning flash intensity
uniform float uOutScale;       // 1.0 for float targets, 0.25 when HDR is packed into RGBA8

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
float fbm2(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += vnoise(p) * a;
        p = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return s;
}

vec3 srgb_to_linear(vec3 c) { return c * c; }

// Night sky with city light pollution near the horizon.
vec3 sky_color(vec3 dir) {
    float h = clamp(dir.y, -0.2, 1.0);
    float horizon = exp(-max(h, 0.0) * 6.0);
    vec3 col = mix(uSkyTop, uSkyHorizon, horizon);
    if (dir.y < 0.0) col = mix(uSkyHorizon, uFogColor * 0.6, clamp(-dir.y * 5.0, 0.0, 1.0));
    return col + uFlash * vec3(0.6, 0.65, 0.8) * (0.4 + 0.6 * horizon);
}

vec3 apply_fog(vec3 col, vec3 worldPos) {
    vec3 v = worldPos - uCamPos;
    float dist = length(v);
    float hf = uFogParams.y;
    float camH = max(uCamPos.y - uFogParams.z, 0.0);
    float dy = v.y;
    // Integrated exponential height fog
    float fogInt = dist * uFogParams.x * exp(-hf * max(camH, -50.0));
    if (abs(dy) > 0.01) {
        float t = hf * dy;
        fogInt *= (1.0 - exp(-t)) / t;
    }
    float fog = clamp(1.0 - exp(-fogInt), 0.0, uFogParams.w);
    vec3 dir = v / max(dist, 0.001);
    vec3 fogCol = mix(uFogColor, sky_color(dir), 0.35);
    float sunGlow = pow(max(dot(dir, uSunDir), 0.0), 8.0);
    fogCol += uSunColor * sunGlow * 0.25;
    return mix(col, fogCol, fog);
}
