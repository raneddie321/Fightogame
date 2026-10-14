// FIGHTO effects shader: particles (blood, sparks, smoke, fire, muzzle flash),
// decals (blood splatter, pools, bullet holes) and volumetric beams.
// Variants: PARTICLE, DECAL, BEAM, RAIN

uniform highp usampler2D uLightGrid;
uniform highp sampler2D uLightData;
uniform vec4 uLightGridParams;
uniform vec4 uLightGridParams2;

vec3 omni_light(vec3 P) {
    vec3 sum = vec3(0.0);
    int cx = int(floor((P.x - uLightGridParams.x) * uLightGridParams.z));
    int cz = int(floor((P.z - uLightGridParams.y) * uLightGridParams.z));
    int cellsX = int(uLightGridParams.w);
    int cellsZ = int(uLightGridParams2.x);
    if (cx < 0 || cz < 0 || cx >= cellsX || cz >= cellsZ) return sum;
    for (int t = 0; t < 3; t++) {
        uvec4 ids = texelFetch(uLightGrid, ivec2(cx * 3 + t, cz), 0);
        for (int c = 0; c < 4; c++) {
            uint id = ids[c];
            if (id == 65535u) return sum;
            vec4 pr = texelFetch(uLightData, ivec2(int(id), 0), 0);
            vec3 Lv = pr.xyz - P;
            float d2 = dot(Lv, Lv);
            float r = pr.w;
            if (d2 > r * r) continue;
            vec4 col = texelFetch(uLightData, ivec2(int(id), 1), 0);
            vec4 sp = texelFetch(uLightData, ivec2(int(id), 2), 0);
            float fall = clamp(1.0 - pow(sqrt(d2) / r, 4.0), 0.0, 1.0);
            float att = fall * fall / (d2 + 1.0);
            if (sp.w > 0.0) att *= smoothstep(sp.w, mix(sp.w, 1.0, 0.4), dot(-normalize(Lv), sp.xyz));
            sum += col.rgb * att;
        }
    }
    return sum;
}

// Diffuse + Blinn-Phong glints from the local lights (used per pixel by wet decals)
vec3 omni_light_spec(vec3 P, vec3 N, vec3 V, float shininess, out vec3 diffuse) {
    vec3 spec = vec3(0.0);
    diffuse = vec3(0.0);
    int cx = int(floor((P.x - uLightGridParams.x) * uLightGridParams.z));
    int cz = int(floor((P.z - uLightGridParams.y) * uLightGridParams.z));
    int cellsX = int(uLightGridParams.w);
    int cellsZ = int(uLightGridParams2.x);
    if (cx < 0 || cz < 0 || cx >= cellsX || cz >= cellsZ) return spec;
    for (int t = 0; t < 3; t++) {
        uvec4 ids = texelFetch(uLightGrid, ivec2(cx * 3 + t, cz), 0);
        for (int c = 0; c < 4; c++) {
            uint id = ids[c];
            if (id == 65535u) return spec;
            vec4 pr = texelFetch(uLightData, ivec2(int(id), 0), 0);
            vec3 Lv = pr.xyz - P;
            float d2 = dot(Lv, Lv);
            float r = pr.w;
            if (d2 > r * r) continue;
            vec4 col = texelFetch(uLightData, ivec2(int(id), 1), 0);
            vec4 sp = texelFetch(uLightData, ivec2(int(id), 2), 0);
            float fall = clamp(1.0 - pow(sqrt(d2) / r, 4.0), 0.0, 1.0);
            float att = fall * fall / (d2 + 1.0);
            vec3 L = Lv * inversesqrt(max(d2, 1e-6));
            if (sp.w > 0.0) att *= smoothstep(sp.w, mix(sp.w, 1.0, 0.4), dot(-L, sp.xyz));
            float NoL = max(dot(N, L), 0.0);
            diffuse += col.rgb * att * NoL;
            vec3 H = normalize(L + V);
            spec += col.rgb * att * pow(max(dot(N, H), 0.0), shininess) * NoL * (shininess + 8.0) * 0.04;
        }
    }
    return spec;
}

#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aParams;   // type, emissive, seed, extra
out vec2 vUV;
out vec4 vColor;
flat out vec4 vParams;
out vec3 vWorld;
out vec3 vLight;
#ifdef RAIN
uniform vec3 uRainBox;     // box size
uniform vec3 uRainVel;     // fall velocity
uniform float uRainLen;
#endif
void main() {
    vec3 wp = aPos;
#ifdef RAIN
    // aPos holds a random point in [0,1]^3, aUV.x the streak end (0/1), aUV.y the side (-1/1)
    vec3 v = uRainVel;
    vec3 base = aPos * uRainBox + v * uTime;
    vec3 cell = uCamPos - uRainBox * 0.5;
    vec3 rel = mod(base - cell, uRainBox);
    vec3 head = cell + rel;
    vec3 tail = head - normalize(v) * uRainLen;
    vec3 p = mix(head, tail, aUV.x);
    vec3 toCam = normalize(uCamPos - p);
    vec3 side = normalize(cross(normalize(v), toCam)) * 0.008;
    wp = p + side * aUV.y;
    vUV = vec2(aUV.y, aUV.x * 2.0 - 1.0);
#else
    vUV = aUV;
#endif
    vColor = aColor;
    vParams = aParams;
    vWorld = wp;
#ifdef BEAM
    vLight = vec3(1.0);
#else
    vLight = omni_light(wp) + uAmbientUp * 1.5 + uSunColor * 0.25;
#endif
    gl_Position = uViewProj * vec4(wp, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
in vec4 vColor;
flat in vec4 vParams;
in vec3 vWorld;
in vec3 vLight;
layout(location = 0) out vec4 oColor;

float fog_amount(vec3 P) {
    float d = length(P - uCamPos);
    return clamp(1.0 - exp(-d * uFogParams.x * 1.2), 0.0, uFogParams.w);
}

#ifdef RAIN
void main() {
    float a = (1.0 - abs(vUV.x)) * smoothstep(1.0, 0.2, abs(vUV.y)) * 0.32;
    vec3 c = vLight * 0.9 + vec3(0.12, 0.13, 0.16) + uFlash;
    float f = fog_amount(vWorld);
    a *= (1.0 - f * 0.7);
    oColor = vec4(c * a * uOutScale, a);
}
#endif

#ifdef PARTICLE
void main() {
    int type = int(vParams.x + 0.5);
    float seed = vParams.z;
    vec2 p = vUV;            // -1..1
    float r = length(p);
    float alpha = 0.0;
    vec3 col = srgb_to_linear(vColor.rgb);
    float additive = 0.0;
    if (type == 0) {         // smoke / dust / mist
        float n = vnoise(p * 2.5 + seed * 13.0 + uTime * 0.3);
        alpha = smoothstep(1.0, 0.1, r) * (0.55 + 0.45 * n);
        col *= vLight;
    } else if (type == 1) {  // blood droplet / chunk
        float n = vnoise(p * 3.0 + seed * 7.0);
        alpha = smoothstep(0.95, 0.75, r + n * 0.25);
        float spec = pow(max(1.0 - length(p - vec2(-0.3, 0.35)), 0.0), 6.0);
        col = col * (vLight * 0.8 + 0.06) + spec * vLight * 0.6;
    } else if (type == 2) {  // spark / glow (additive)
        alpha = pow(max(1.0 - r, 0.0), 2.0);
        additive = 1.0;
    } else if (type == 3) {  // fire
        float n = fbm2(vec2(p.x * 2.0, p.y * 2.0 - uTime * 3.0) + seed * 5.0);
        alpha = smoothstep(1.0, 0.2, r) * smoothstep(0.25, 0.7, n + (1.0 - r) * 0.4);
        col = mix(vec3(1.0, 0.25, 0.05), vec3(1.0, 0.85, 0.5), n) * 6.0;
        additive = 1.0;
    } else if (type == 4) {  // muzzle flash star
        float ang = atan(p.y, p.x);
        float star = 0.55 + 0.45 * pow(abs(cos(ang * 3.0 + seed * 6.0)), 6.0);
        alpha = smoothstep(star, star * 0.2, r);
        col = col * 12.0;
        additive = 1.0;
    } else if (type == 5) {  // stretched streak (blood stream / tracer)
        alpha = smoothstep(1.0, 0.3, abs(p.x)) * smoothstep(1.0, 0.6, abs(p.y));
        if (vParams.y > 0.0) { additive = 1.0; col *= vParams.y; }
        else col *= vLight * 0.8 + 0.05;
    } else if (type == 6) {  // shockwave ring
        alpha = smoothstep(0.15, 0.0, abs(r - 0.8)) * 0.8;
        additive = 1.0;
        col *= 3.0;
    } else {                 // alien ichor glow blob
        alpha = smoothstep(1.0, 0.5, r);
        col = col * (1.5 + vLight * 0.5);
        additive = 0.5;
    }
    alpha *= vColor.a;
    float f = fog_amount(vWorld);
    col = mix(col, uFogColor, f * (1.0 - additive));
    alpha *= (1.0 - f * additive * 0.8);
    // Premultiplied output; additive particles keep alpha at zero.
    oColor = vec4(col * alpha * uOutScale, alpha * (1.0 - additive));
}
#endif

#ifdef DECAL
void main() {
    int type = int(vParams.x + 0.5);
    float seed = vParams.z;
    float grow = vParams.w;
    vec2 p = vUV;
    float r = length(p);
    float alpha = 0.0;
    vec3 col = srgb_to_linear(vColor.rgb);
    float gloss = 0.0;
    if (type == 0) {        // blood splatter with satellite droplets
        float ang = atan(p.y, p.x);
        float rays = vnoise(vec2(ang * 3.0 + seed * 10.0, seed)) * 0.5 + vnoise(vec2(ang * 9.0, seed * 3.0)) * 0.3;
        float core = smoothstep(0.42 + rays * 0.5, 0.36 + rays * 0.5, r);
        vec2 cell = floor(p * 7.0 + seed * 3.0);
        vec2 fp = fract(p * 7.0 + seed * 3.0) - 0.5;
        float h = hash12(cell + seed);
        float drops = step(0.72, h) * smoothstep(0.22 * h, 0.14 * h, length(fp)) * smoothstep(1.0, 0.5, r);
        alpha = max(core, drops);
        float n = vnoise(p * 6.0 + seed);
        col *= 0.6 + 0.6 * n;
        gloss = 1.0;
    } else if (type == 1) { // spreading blood pool
        float n = fbm2(p * 2.0 + seed * 5.0);
        float rad = grow * (0.6 + n * 0.5);
        alpha = smoothstep(rad, rad - 0.06, r);
        col *= mix(0.45, 0.8, smoothstep(rad, 0.0, r));
        gloss = 1.0;
    } else if (type == 2) { // bullet hole
        alpha = smoothstep(0.3, 0.2, r) + smoothstep(0.9, 0.3, r) * 0.45 * vnoise(p * 8.0 + seed);
        col = vec3(0.02);
    } else if (type == 3) { // scorch mark
        float n = fbm2(p * 3.0 + seed);
        alpha = smoothstep(1.0, 0.3, r + n * 0.3) * 0.85;
        col = vec3(0.015);
    } else if (type == 4) { // alien ichor
        float n = fbm2(p * 3.0 + seed);
        alpha = smoothstep(0.7, 0.55, r + n * 0.3);
        gloss = 1.0;
    } else if (type == 5) { // drag smear / streak
        float n = vnoise(vec2(p.x * 9.0, p.y * 1.5) + seed);
        alpha = smoothstep(1.0, 0.6, abs(p.x)) * smoothstep(1.0, 0.7, abs(p.y)) * (0.4 + 0.6 * n);
        gloss = 1.0;
    } else {                // tyre marks
        alpha = smoothstep(1.0, 0.7, abs(p.x)) * 0.6;
        col = vec3(0.01);
    }
    alpha *= vColor.a;
    // The decal normal comes from screen-space derivatives (decals are flat quads)
    vec3 V = normalize(uCamPos - vWorld);
    vec3 Nd = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    if (dot(Nd, V) < 0.0) Nd = -Nd;
    vec3 lit;
    if (gloss > 0.5) {
        // Wet liquid (blood, ichor): dark diffuse body, sharp glints from every nearby light
        // and a weak Fresnel reflection of the sky - deep red, never washed out
        vec3 diff;
        vec3 spec = omni_light_spec(vWorld, Nd, V, 90.0, diff);
        float NoV = max(dot(Nd, V), 0.0);
        float fres = 0.025 + 0.975 * pow(1.0 - NoV, 5.0);
        vec3 env = sky_color(reflect(-V, Nd));
        float thick = (type == 1) ? smoothstep(0.0, 0.5, alpha) : alpha;
        // The base lift keeps fresh blood reading as deep red even away from street lights
        lit = col * (diff * 0.9 + uAmbientUp * 2.0 + uSunColor * 0.3 + vec3(0.32));
        lit += thick * (spec * 0.9 + env * fres * 0.35);
        // Light scattering through the thin edge of fresh blood
        if (type == 0 || type == 1 || type == 5) lit += col * diff * 0.6 * (1.0 - smoothstep(0.35, 0.9, alpha));
    } else {
        lit = col * (vLight * 0.6 + uAmbientUp * 0.5);
    }
    if (type == 4) lit += col * 1.2;
    float f = fog_amount(vWorld);
    lit = mix(lit, uFogColor, f);
    oColor = vec4(lit * alpha * uOutScale, alpha);
}
#endif

#ifdef BEAM
void main() {
    // vUV.x across the cone (-1..1), vUV.y along the beam (0 at source)
    float across = 1.0 - abs(vUV.x);
    float along = clamp(vUV.y, 0.0, 1.0);
    float n = vnoise(vec2(vUV.x * 3.0 + uTime * 0.2, vUV.y * 6.0 - uTime * 0.4) + vParams.z);
    float a = pow(across, 1.5) * pow(1.0 - along, 1.6) * (0.75 + 0.25 * n) * vColor.a;
    a *= 1.0 + uRain * 0.8;
    vec3 col = srgb_to_linear(vColor.rgb) * vParams.y;
    float d = length(vWorld - uCamPos);
    a *= smoothstep(0.5, 3.0, d);
    oColor = vec4(col * a * uOutScale, 0.0);
}
#endif
#endif
