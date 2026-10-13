// FIGHTO main surface shader: city, characters, vehicles, props.
// Variants: SKINNED (GPU skinning), SHADOW_PASS (depth only), LOW_QUALITY (mobile).

#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aMat;
layout(location = 5) in vec4 aBones;
layout(location = 6) in vec4 aWeights;

uniform mat4 uModel;
uniform mat4 uShadowMat;
uniform float uWind;
#ifdef SKINNED
uniform mat4 uBones[24];
#endif

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
out vec4 vMat;
out vec4 vShadow;
out vec3 vLocal;

void main() {
    vec4 lp = vec4(aPos, 1.0);
    vec3 ln = aNormal.xyz;
#ifdef SKINNED
    mat4 skin = uBones[int(aBones.x)] * aWeights.x + uBones[int(aBones.y)] * aWeights.y +
                uBones[int(aBones.z)] * aWeights.z + uBones[int(aBones.w)] * aWeights.w;
    lp = skin * lp;
    ln = mat3(skin) * ln;
#endif
    vec4 wp = uModel * lp;
    // Foliage sway
    if ((int(aMat.y + 0.5) & 16) != 0) {
        float h = max(aPos.y - 1.0, 0.0);
        wp.x += sin(uTime * 1.7 + wp.z * 0.3) * 0.04 * h * (0.5 + uWind);
        wp.z += cos(uTime * 1.3 + wp.x * 0.3) * 0.03 * h * (0.5 + uWind);
    }
    vWorld = wp.xyz;
    vLocal = aPos;
    vNormal = normalize(mat3(uModel) * ln);
    vUV = aUV;
    vColor = aColor;
    vMat = aMat;
    vShadow = uShadowMat * wp;
    gl_Position = uViewProj * wp;
}
#endif

#ifdef FRAGMENT
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
in vec4 vMat;
in vec4 vShadow;
in vec3 vLocal;

#ifdef SHADOW_PASS
out vec4 oColor;
void main() { oColor = vec4(1.0); }
#else
uniform highp sampler2DArray uAlbedoArr;
uniform highp sampler2DArray uNormalArr;
uniform highp sampler2DShadow uShadowMap;
uniform highp usampler2D uLightGrid;
uniform highp sampler2D uLightData;
uniform vec4 uLightGridParams;      // originX, originZ, 1/cellSize, cellsX
uniform vec4 uLightGridParams2;     // cellsZ, shadow texel size, shadow strength, unused
uniform float uLayerTile[24];
uniform vec4 uTint;
uniform float uBlood;
uniform vec3 uBloodColor;
uniform float uHitFlash;
uniform float uWindowsOn;
uniform float uEmissiveScale;
uniform float uAlienPulse;

layout(location = 0) out vec4 oColor;

float shadow_factor(vec3 N) {
    vec3 sc = vShadow.xyz / vShadow.w;
    if (sc.x <= 0.0 || sc.x >= 1.0 || sc.y <= 0.0 || sc.y >= 1.0 || sc.z >= 1.0) return 1.0;
    float bias = 0.0008 + 0.0015 * (1.0 - clamp(dot(N, uSunDir), 0.0, 1.0));
    float ts = uLightGridParams2.y;
    float z = sc.z - bias;
#ifdef LOW_QUALITY
    float s = texture(uShadowMap, vec3(sc.xy, z));
#else
    float s = 0.0;
    s += texture(uShadowMap, vec3(sc.xy + vec2(-0.5, -0.5) * ts * 1.5, z));
    s += texture(uShadowMap, vec3(sc.xy + vec2(0.5, -0.5) * ts * 1.5, z));
    s += texture(uShadowMap, vec3(sc.xy + vec2(-0.5, 0.5) * ts * 1.5, z));
    s += texture(uShadowMap, vec3(sc.xy + vec2(0.5, 0.5) * ts * 1.5, z));
    s *= 0.25;
#endif
    return mix(1.0, s, uLightGridParams2.z);
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f + 1e-6);
}
float D_GGX_aniso(float NoH, float ToH, float BoH, float at, float ab) {
    float d = ToH * ToH / (at * at) + BoH * BoH / (ab * ab) + NoH * NoH;
    return 1.0 / (PI * at * ab * d * d + 1e-6);
}
vec3 F_Schlick(vec3 f0, float VoH) {
    float f = pow(1.0 - VoH, 5.0);
    return f0 + (vec3(1.0) - f0) * f;
}

struct Surf {
    vec3 P;
    vec3 N;
    vec3 V;
    vec3 albedo;
    vec3 f0;
    float rough;
    float metal;
    float aniso;   // 1 for wet road streak highlights
    vec3 T;
    vec3 B;
    float wrap;
};

vec3 shade_light(Surf s, vec3 L, vec3 radiance) {
    float NoLraw = dot(s.N, L);
    float NoL = clamp((NoLraw + s.wrap) / (1.0 + s.wrap), 0.0, 1.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(L + s.V);
    float NoH = max(dot(s.N, H), 0.0);
    float NoV = max(dot(s.N, s.V), 0.05);
    float VoH = max(dot(s.V, H), 0.0);
    float a = max(s.rough * s.rough, 0.02);
    float D;
    if (s.aniso > 0.5) {
        D = D_GGX_aniso(NoH, dot(s.T, H), dot(s.B, H), max(a * 2.5, 0.25), max(a * 0.35, 0.02));
    } else {
        D = D_GGX(NoH, a);
    }
    float k = a * 0.5;
    float Vis = 0.25 / ((NoL * (1.0 - k) + k) * (NoV * (1.0 - k) + k));
    vec3 F = F_Schlick(s.f0, VoH);
    vec3 spec = D * Vis * F * max(NoLraw, 0.0);
    vec3 diff = (vec3(1.0) - F) * (1.0 - s.metal) * s.albedo / PI * NoL;
    return (diff + spec) * radiance;
}

vec3 local_lights(Surf s) {
    vec3 sum = vec3(0.0);
    float cs = 1.0 / uLightGridParams.z;
    int cx = int(floor((s.P.x - uLightGridParams.x) * uLightGridParams.z));
    int cz = int(floor((s.P.z - uLightGridParams.y) * uLightGridParams.z));
    int cellsX = int(uLightGridParams.w);
    int cellsZ = int(uLightGridParams2.x);
    if (cx < 0 || cz < 0 || cx >= cellsX || cz >= cellsZ) return sum;
#ifdef LOW_QUALITY
    const int TEXELS = 2;
#else
    const int TEXELS = 3;
#endif
    for (int t = 0; t < TEXELS; t++) {
        uvec4 ids = texelFetch(uLightGrid, ivec2(cx * 3 + t, cz), 0);
        for (int c = 0; c < 4; c++) {
            uint id = ids[c];
            if (id == 65535u) return sum;
            vec4 pr = texelFetch(uLightData, ivec2(int(id), 0), 0);
            vec3 Lv = pr.xyz - s.P;
            float d2 = dot(Lv, Lv);
            float r = pr.w;
            if (d2 > r * r) continue;
            vec4 col = texelFetch(uLightData, ivec2(int(id), 1), 0);
            vec4 sp = texelFetch(uLightData, ivec2(int(id), 2), 0);
            float d = sqrt(d2);
            vec3 L = Lv / max(d, 0.0001);
            float fall = clamp(1.0 - pow(d / r, 4.0), 0.0, 1.0);
            float att = fall * fall / (d2 + 1.0);
            if (sp.w > 0.0) {
                float cd = dot(-L, sp.xyz);
                att *= smoothstep(sp.w, mix(sp.w, 1.0, 0.4), cd);
            }
            if (att > 0.0) sum += shade_light(s, L, col.rgb * att);
        }
    }
    return sum;
}

// Procedural lit windows for building facades (uv in metres), anti-aliased.
vec3 facade_windows(vec2 uvm, float seed, float tower, out float glassMask, out float frame) {
    vec2 cell = tower > 0.5 ? vec2(2.2, 3.8) : vec2(3.0, 3.4);
    vec2 g = uvm / cell;
    vec2 id = floor(g);
    vec2 f = fract(g);
    vec2 fw = max(fwidth(g), vec2(1e-4));
    float wx = tower > 0.5 ? 0.06 : 0.2;
    // Box-filtered window rectangle
    vec2 lo = vec2(wx, 0.22);
    vec2 hi = vec2(1.0 - wx, 0.86);
    vec2 a = smoothstep(lo - fw, lo + fw, f) * (1.0 - smoothstep(hi - fw, hi + fw, f));
    float win = a.x * a.y;
    float ground = step(1.0, id.y + 0.5 - (tower > 0.5 ? 0.0 : 0.3));
    float far = smoothstep(0.25, 0.6, max(fw.x, fw.y));
    float avgWin = (hi.x - lo.x) * (hi.y - lo.y);
    win = mix(win, avgWin, far);
    frame = 1.0 - win;
    glassMask = win * ground;
    float h = hash12(id + vec2(seed * 13.7, seed * 3.1));
    if (tower > 0.5) h = mix(h, hash12(vec2(floor(id.x / 6.0), id.y) + seed), 0.75);
    float lit = step(0.7, h);
    float tone = hash12(id * 1.37 + seed);
    vec3 warm = vec3(1.0, 0.62, 0.32);
    vec3 cool = vec3(0.55, 0.72, 1.0);
    vec3 tv = vec3(0.35, 0.5, 1.0) * (0.7 + 0.3 * sin(uTime * 7.0 + h * 40.0));
    vec3 c = tone > 0.93 ? tv : (tone > 0.65 ? cool : warm);
    float curtain = smoothstep(0.3, 0.32, fract(f.x * 2.0 + h)) * 0.4 + 0.6;
    float inten = lit * (0.4 + 0.9 * hash12(id + 3.1)) * (0.65 + 0.35 * f.y) * curtain;
    // Far away: average brightness of a window grid (prevents shimmering)
    vec3 farCol = mix(warm, cool, 0.35) * 0.3 * 0.75;
    vec3 col = mix(c * inten, farCol, far);
    return glassMask * col;
}

vec3 ripple_normal(vec2 p) {
    vec2 cell = floor(p * 1.5);
    vec2 n = vec2(0.0);
    for (int i = 0; i < 4; i++) {
        vec2 o = vec2(float(i - (i / 2) * 2), float(i / 2));
        vec2 c = cell + o;
        float h = hash12(c);
        vec2 center = (c + vec2(hash12(c + 7.1), hash12(c + 3.3))) / 1.5;
        float t = fract(uTime * 0.9 + h);
        vec2 dv = p - center;
        float d = length(dv);
        float ring = t * 0.6;
        float w = exp(-pow((d - ring) * 18.0, 2.0)) * (1.0 - t);
        n += normalize(dv + 1e-4) * w * sin((d - ring) * 40.0);
    }
    return vec3(n.x, 0.0, n.y) * 0.35;
}

void main() {
    int layer = int(vMat.x + 0.5);
    int flags = int(vMat.y + 0.5);
    float roughScale = vMat.z / 128.0;
    float emissive = vMat.w * 0.1;
    float tile = uLayerTile[layer];
    vec2 tuv = vUV / tile;
    vec4 alb = texture(uAlbedoArr, vec3(tuv, float(layer)));
    vec4 nrm = texture(uNormalArr, vec3(tuv, float(layer)));

    vec3 vcol = vColor.a > 0.5 ? srgb_to_linear(vColor.rgb) : srgb_to_linear(uTint.rgb);
    vec3 albedo = srgb_to_linear(alb.rgb) * vcol;
    float rough = clamp(alb.a * roughScale, 0.04, 1.0);
    float ao = nrm.b;

    // Normal mapping with a derivative based tangent frame
    vec3 Ng = normalize(vNormal);
    if (!gl_FrontFacing) Ng = -Ng;
    vec3 dp1 = dFdx(vWorld);
    vec3 dp2 = dFdy(vWorld);
    vec2 duv1 = dFdx(tuv);
    vec2 duv2 = dFdy(tuv);
    vec3 dp2perp = cross(dp2, Ng);
    vec3 dp1perp = cross(Ng, dp1);
    vec3 Tn = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 Bn = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(Tn, Tn), dot(Bn, Bn)) + 1e-12);
    vec2 nxy = nrm.rg * 2.0 - 1.0;
    float nstr = 1.0;
    vec3 N = normalize(Tn * invmax * nxy.x * nstr + Bn * invmax * nxy.y * nstr + Ng * sqrt(max(1.0 - dot(nxy, nxy), 0.0)));

    float metal = (flags & 4) != 0 ? 0.85 : 0.0;
    vec3 emit = vec3(0.0);

    // Character blood stains
    if ((flags & 32) != 0 && uBlood > 0.0) {
        float n = vnoise3(vLocal * 9.0) * 0.6 + vnoise3(vLocal * 23.0) * 0.4;
        float upper = smoothstep(0.6, 1.7, vLocal.y) * 0.5 + 0.5;
        float mask = smoothstep(1.0 - uBlood * 0.75 * upper, 1.02 - uBlood * 0.75 * upper + 0.08, n);
        albedo = mix(albedo, uBloodColor * 0.35, mask);
        rough = mix(rough, 0.18, mask);
        if (uBloodColor.g > uBloodColor.r) emit += uBloodColor * mask * 0.6;
    }

    // Wet ground & puddles
    float wet = 0.0;
    float isGround = (flags & 8) != 0 ? 1.0 : 0.0;
    if (uRain > 0.0) {
        float up = clamp(Ng.y, 0.0, 1.0);
        float puddle = smoothstep(0.48, 0.62, fbm2(vWorld.xz * 0.12) + nrm.a * 0.15 - 0.05);
        wet = uRain * mix(0.35 * up + 0.15, mix(0.55, 1.0, puddle), isGround * up);
        albedo *= mix(1.0, 0.42, wet);
        rough = mix(rough, 0.04, wet * (isGround > 0.5 ? puddle * 0.85 + 0.15 : 0.6));
        if (isGround > 0.5) {
            N = normalize(mix(N, Ng, puddle * 0.9));
#ifndef LOW_QUALITY
            N = normalize(N + ripple_normal(vWorld.xz) * puddle * uRain);
#endif
        }
    }

    vec3 V = normalize(uCamPos - vWorld);
    Surf s;
    s.P = vWorld;
    s.N = N;
    s.V = V;
    s.metal = metal;
    s.albedo = albedo;
    s.f0 = mix(vec3(0.04), albedo, metal);
    s.rough = rough;
    s.aniso = 0.0;
    s.wrap = (flags & 32) != 0 ? 0.25 : 0.0;
#ifndef LOW_QUALITY
    if (isGround > 0.5 && wet > 0.45) {
        s.aniso = 1.0;
        vec3 vproj = V - N * dot(V, N);
        s.T = normalize(vproj + vec3(1e-4, 0.0, 0.0));
        s.B = cross(N, s.T);
    }
#endif
    s.T = s.aniso > 0.5 ? s.T : vec3(1.0, 0.0, 0.0);
    s.B = s.aniso > 0.5 ? s.B : vec3(0.0, 0.0, 1.0);

    // Glass surfaces (windows, windshields)
    float glassMask = 0.0;
    if ((flags & 1) != 0) {
        float frame;
        float tower = (layer == 4) ? 1.0 : 0.0;
        vec3 w = facade_windows(vUV, vMat.w, tower, glassMask, frame);
        emit += w * uWindowsOn * 1.6;
        s.albedo = mix(s.albedo, vec3(0.02, 0.025, 0.03), glassMask);
        s.rough = mix(s.rough, 0.08, glassMask);
        s.f0 = mix(s.f0, vec3(0.06), glassMask);
        emissive = 0.0;
    }
    if ((flags & 64) != 0) {
        s.albedo = albedo * 0.15;
        s.rough = 0.05;
        s.f0 = vec3(0.08);
        glassMask = 1.0;
    }

    float shadow = shadow_factor(N);
    vec3 col = shade_light(s, uSunDir, uSunColor * shadow);
    col += local_lights(s);

    // Ambient (hemisphere) + environment reflection
    float hemi = N.y * 0.5 + 0.5;
    vec3 amb = mix(uAmbientDown, uAmbientUp, hemi);
    col += s.albedo * amb * ao * (1.0 - metal * 0.7);
    vec3 R = reflect(-V, N);
    float NoV = max(dot(N, V), 0.0);
    vec3 Fenv = s.f0 + (max(vec3(1.0 - s.rough), s.f0) - s.f0) * pow(1.0 - NoV, 5.0);
    vec3 env = sky_color(R) * (1.0 - s.rough) * (1.0 - s.rough);
    col += env * Fenv * ao * (0.5 + 1.5 * max(wet, glassMask));

    // Emission
    if ((flags & 2) != 0) {
        float pulse = (flags & 128) != 0 ? (0.65 + 0.35 * sin(uTime * 3.0 + vWorld.x * 0.5 + vWorld.z * 0.3)) * (0.6 + uAlienPulse) : 1.0;
        emit += vcol * emissive * pulse;
    }
    col += emit * uEmissiveScale;
    col += vec3(1.0, 0.25, 0.2) * uHitFlash * 0.6;

    col = apply_fog(col, vWorld);
    oColor = vec4(col * uOutScale, 1.0);
}
#endif
#endif
