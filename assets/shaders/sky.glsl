// FIGHTO sky: night gradient, moon, storm clouds lit by the city, invasion glow.
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
out vec2 vNdc;
void main() {
    vNdc = aPos.xy;
    gl_Position = vec4(aPos.xy, 0.9999, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform float uInvasion;
uniform float uCloudCover;
layout(location = 0) out vec4 oColor;

void main() {
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - uCamPos);
    vec3 col = sky_color(dir);

    // Stars (faint, hidden by clouds)
    vec2 sp = dir.xz / max(dir.y, 0.05) * 60.0;
    float star = step(0.9975, hash12(floor(sp))) * smoothstep(0.1, 0.4, dir.y);

    // Moon
    float md = dot(dir, uSunDir);
    float moon = smoothstep(0.9993, 0.9996, md);
    float halo = pow(max(md, 0.0), 600.0) * 0.8 + pow(max(md, 0.0), 40.0) * 0.12;
    vec3 moonCol = uSunColor * 3.0;

    // Clouds on a virtual plane
    float cloud = 0.0;
    if (dir.y > 0.0) {
        vec2 cp = dir.xz / (dir.y + 0.08) * 900.0 * 0.0011 + vec2(uTime * 0.012, uTime * 0.004);
        float n = fbm2(cp * 1.3) * 0.65 + fbm2(cp * 3.7 + 4.0) * 0.35;
        cloud = smoothstep(0.62 - uCloudCover * 0.45, 0.9, n) * smoothstep(0.0, 0.12, dir.y);
    }
    vec3 cityGlow = uFogColor * 1.8 + vec3(0.25, 0.11, 0.04) * (1.0 - uInvasion);
    vec3 cloudCol = mix(uSkyTop * 1.6, cityGlow, exp(-max(dir.y, 0.0) * 3.5)) * (0.55 + uFlash * 2.5);
    cloudCol += vec3(0.9, 0.15, 0.08) * uInvasion * 0.25 * (0.6 + 0.4 * sin(uTime * 0.7 + dir.x * 5.0));

    col += star * vec3(0.7, 0.75, 0.9) * (1.0 - cloud) * (1.0 - uInvasion * 0.8);
    col += (moon * moonCol + halo * uSunColor) * (1.0 - cloud * 0.85);
    col = mix(col, cloudCol, cloud * 0.9);
    // Invasion: alien aurora bands
    if (uInvasion > 0.0) {
        float band = vnoise(vec2(dir.x * 4.0 + uTime * 0.05, dir.z * 4.0)) * smoothstep(0.05, 0.5, dir.y);
        col += vec3(0.1, 0.55, 0.9) * pow(band, 3.0) * uInvasion * 0.8;
    }
    oColor = vec4(col * uOutScale, 1.0);
}
#endif
