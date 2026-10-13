// FIGHTO - procedural material texture generation (albedo/roughness and
// normal/AO/height arrays). All surface textures are generated at startup.
#include "texgen.h"
#include "../core/noise.h"

const float g_layerTile[MAT_COUNT] = {
    4.0f,  // asphalt
    2.0f,  // sidewalk
    2.0f,  // brick
    4.0f,  // concrete
    4.0f,  // glass facade
    2.0f,  // corrugated
    3.0f,  // grass
    3.0f,  // dirt
    4.0f,  // roof
    2.0f,  // wood
    2.0f,  // tiles
    3.0f,  // metal panel
    2.0f,  // alien
    0.5f,  // fabric
    0.3f,  // skin
    2.0f,  // rust
    1.0f,  // paint
    2.0f,  // plaster
    1.0f,  // car paint
    0.5f,  // leather
    1.0f,  // steel
    0.5f,  // rubber
    0.25f, // hair
    3.0f,  // floor concrete
};

struct TexSample {
    vec3 albedo;
    float rough;
    float height;
    float ao;
};

static float fbm(float u, float v, int period, int oct, u32 seed) { return noise_fbm_tiled(u * period, v * period, period, oct, seed); }
static float vn(float u, float v, int period, u32 seed) { return noise_value_tiled(u * period, v * period, period, seed); }
static float hashcell(int x, int y, u32 s) { return noise_hash2(x, y, s); }
static float fracf(float x) { return x - floorf(x); }

static TexSample tex_asphalt(float u, float v) {
    TexSample t;
    float fine = fbm(u, v, 64, 3, 11);
    float speck = hashcell((int)(u * 384), (int)(v * 384), 7) > 0.92f ? 1.0f : 0.0f;
    float patch = fbm(u, v, 4, 4, 23);
    float f1, edge;
    noise_worley_tiled(u * 3, v * 3, 3, 99, &f1, &edge, 0);
    float crackNoise = fbm(u, v, 16, 2, 5);
    float crack = smoothstepf(0.035f, 0.0f, edge) * smoothstepf(0.45f, 0.6f, crackNoise);
    float oil = smoothstepf(0.62f, 0.75f, fbm(u, v, 3, 3, 71));
    float g = 0.2f + 0.07f * fine + speck * 0.1f - (patch > 0.58f ? 0.025f : 0.0f) - oil * 0.04f - crack * 0.07f;
    t.albedo = vec3(g, g * 1.0f, g * 1.05f);
    t.rough = clampf(0.78f + 0.18f * fine - oil * 0.3f + crack * 0.2f, 0, 1);
    t.height = fine * 0.5f + speck * 0.35f - crack * 0.9f;
    t.ao = 1.0f - crack * 0.6f;
    return t;
}

static TexSample tex_sidewalk(float u, float v) {
    TexSample t;
    float lu = fracf(u * 2), lv = fracf(v * 2);
    float seam = fminf(fminf(lu, 1 - lu), fminf(lv, 1 - lv));
    float groove = smoothstepf(0.018f, 0.004f, seam);
    int sx = (int)(u * 2), sy = (int)(v * 2);
    float slabVar = hashcell(sx, sy, 3) * 0.06f;
    float n = fbm(u, v, 16, 4, 31);
    float stain = smoothstepf(0.55f, 0.75f, fbm(u, v, 4, 4, 77));
    float gum = hashcell((int)(u * 200), (int)(v * 200), 13) > 0.995f ? 1.0f : 0.0f;
    float g = 0.42f + slabVar + n * 0.08f - stain * 0.12f - groove * 0.15f - gum * 0.2f;
    t.albedo = vec3(g, g * 0.99f, g * 0.97f);
    t.rough = 0.85f + n * 0.1f;
    t.height = 0.6f + n * 0.3f - groove * 0.6f;
    t.ao = 1.0f - groove * 0.5f;
    return t;
}

static TexSample tex_brick(float u, float v) {
    TexSample t;
    const float rows = 26, cols = 8;
    float rv = v * rows;
    int row = (int)floorf(rv);
    float off = (row & 1) ? 0.5f : 0.0f;
    float cu = u * cols + off;
    int col = (int)floorf(cu);
    float bu = fracf(cu), bv = fracf(rv);
    float mortarU = 0.05f, mortarV = 0.14f;
    float edge = fminf(fminf(bu, 1 - bu) / mortarU, fminf(bv, 1 - bv) / mortarV);
    float brick = smoothstepf(0.6f, 1.2f, edge);
    int wc = ((col % (int)cols) + (int)cols) % (int)cols;
    float h = hashcell(wc, row, 41);
    float h2 = hashcell(wc, row, 42);
    vec3 base = lerp(vec3(0.42f, 0.16f, 0.10f), vec3(0.55f, 0.26f, 0.16f), h);
    if (h2 > 0.85f) base = base * 0.6f;               // burnt bricks
    if (h2 < 0.08f) base = vec3(0.5f, 0.42f, 0.36f);  // light bricks
    float n = fbm(u, v, 32, 3, 17);
    float dirt = smoothstepf(0.5f, 0.8f, fbm(u * 1.0f, v, 4, 3, 61)) * 0.35f;
    vec3 mortar = vec3(0.5f, 0.48f, 0.45f) * (0.8f + n * 0.3f);
    t.albedo = lerp(mortar, base * (0.85f + n * 0.3f), brick) * (1.0f - dirt);
    t.rough = 0.88f;
    t.height = brick * (0.8f + n * 0.2f);
    t.ao = 0.55f + brick * 0.45f;
    return t;
}

static TexSample tex_concrete(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 16, 5, 101);
    float panelU = fracf(u * 2), panelV = fracf(v * 2);
    float seam = smoothstepf(0.012f, 0.003f, fminf(fminf(panelU, 1 - panelU), fminf(panelV, 1 - panelV)));
    // Formwork tie holes
    float hu = fracf(u * 4) - 0.5f, hv = fracf(v * 4) - 0.5f;
    float hole = smoothstepf(0.06f, 0.035f, sqrtf(hu * hu + hv * hv));
    float streak = fbm(u * 1.0f, v * 0.25f, 24, 3, 7);
    streak = smoothstepf(0.5f, 0.85f, streak);
    float g = 0.48f + n * 0.12f - streak * 0.14f - seam * 0.12f - hole * 0.25f;
    t.albedo = vec3(g, g * 0.98f, g * 0.95f);
    t.rough = 0.88f;
    t.height = 0.6f + n * 0.25f - seam * 0.5f - hole * 0.6f;
    t.ao = 1.0f - hole * 0.6f - seam * 0.3f;
    return t;
}

static TexSample tex_glass_facade(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 8, 3, 211);
    t.albedo = vec3(0.22f, 0.24f, 0.27f) * (0.9f + n * 0.2f);
    t.rough = 0.35f;
    t.height = 0.5f;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_corrugated(float u, float v) {
    TexSample t;
    float ridge = sinf(u * TAU_F * 16) * 0.5f + 0.5f;
    float n = fbm(u, v, 16, 3, 301);
    float rustMask = smoothstepf(0.55f, 0.72f, fbm(u, v * 0.5f, 6, 4, 303) + (1.0f - v) * 0.1f);
    float drip = smoothstepf(0.6f, 0.9f, fbm(u * 1.0f, v * 0.1f, 32, 2, 305)) * smoothstepf(0.4f, 0.9f, v);
    vec3 paint = vec3(0.52f, 0.56f, 0.58f) * (0.9f + n * 0.15f);
    vec3 rust = vec3(0.36f, 0.17f, 0.07f) * (0.7f + n * 0.5f);
    float r = fmaxf(rustMask, drip * 0.7f);
    t.albedo = lerp(paint, rust, r);
    t.rough = lerpf(0.5f, 0.9f, r);
    t.height = ridge * 0.9f + n * 0.1f;
    t.ao = 0.75f + ridge * 0.25f;
    return t;
}

static TexSample tex_grass(float u, float v) {
    TexSample t;
    float blades = fbm(u * 1.0f, v, 128, 2, 401);
    float clumps = fbm(u, v, 8, 4, 403);
    float dry = smoothstepf(0.55f, 0.75f, fbm(u, v, 4, 3, 405));
    vec3 green = lerp(vec3(0.10f, 0.20f, 0.06f), vec3(0.20f, 0.32f, 0.10f), blades);
    vec3 brown = vec3(0.30f, 0.27f, 0.14f);
    t.albedo = lerp(green, brown, dry * 0.7f) * (0.75f + clumps * 0.5f);
    t.rough = 0.95f;
    t.height = blades * 0.7f + clumps * 0.3f;
    t.ao = 0.7f + blades * 0.3f;
    return t;
}

static TexSample tex_dirt(float u, float v) {
    TexSample t;
    float f1, edge;
    noise_worley_tiled(u * 24, v * 24, 24, 501, &f1, &edge, 0);
    float pebble = smoothstepf(0.35f, 0.15f, f1);
    float n = fbm(u, v, 16, 4, 503);
    vec3 dirt = lerp(vec3(0.22f, 0.17f, 0.12f), vec3(0.34f, 0.27f, 0.2f), n);
    vec3 stone = vec3(0.4f, 0.38f, 0.35f) * (0.8f + hashcell((int)(u * 24), (int)(v * 24), 9) * 0.4f);
    t.albedo = lerp(dirt, stone, pebble * 0.8f);
    t.rough = 0.93f;
    t.height = n * 0.5f + pebble * 0.5f;
    t.ao = 0.7f + pebble * 0.3f;
    return t;
}

static TexSample tex_roof(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 96, 2, 601);
    float big = fbm(u, v, 6, 3, 603);
    float g = 0.16f + n * 0.08f + big * 0.04f;
    t.albedo = vec3(g, g, g * 1.02f);
    t.rough = 0.95f;
    t.height = n;
    t.ao = 0.8f + n * 0.2f;
    return t;
}

static TexSample tex_wood(float u, float v) {
    TexSample t;
    float planks = 10;
    float pv = v * planks;
    int p = (int)floorf(pv);
    float lv = fracf(pv);
    float gap = smoothstepf(0.04f, 0.0f, fminf(lv, 1 - lv));
    float off = hashcell(p, 0, 701) * 10.0f;
    float grain = fbm(u * 0.5f + off, v * 4.0f, 16, 4, 703);
    float ring = sinf((grain * 12.0f + u * 3.0f) * TAU_F) * 0.5f + 0.5f;
    float pv2 = hashcell(p, 1, 705);
    vec3 c = lerp(vec3(0.30f, 0.18f, 0.09f), vec3(0.48f, 0.32f, 0.17f), ring * 0.5f + pv2 * 0.5f);
    t.albedo = c * (1.0f - gap * 0.7f);
    t.rough = 0.7f;
    t.height = 0.7f - gap * 0.7f + ring * 0.1f;
    t.ao = 1.0f - gap * 0.5f;
    return t;
}

static TexSample tex_tiles(float u, float v) {
    TexSample t;
    float tu = fracf(u * 4), tv = fracf(v * 4);
    float grout = smoothstepf(0.025f, 0.008f, fminf(fminf(tu, 1 - tu), fminf(tv, 1 - tv)));
    float var = hashcell((int)(u * 4), (int)(v * 4), 801) * 0.06f;
    float n = fbm(u, v, 32, 3, 803);
    float dirt = smoothstepf(0.6f, 0.85f, fbm(u, v, 4, 3, 805)) * 0.15f;
    float g = 0.62f + var + n * 0.05f - dirt;
    vec3 c = vec3(g * 0.97f, g, g * 1.02f);
    t.albedo = lerp(c, vec3(0.25f, 0.25f, 0.25f), grout);
    t.rough = lerpf(0.28f, 0.9f, grout) + dirt;
    t.height = 0.7f - grout * 0.6f;
    t.ao = 1.0f - grout * 0.4f;
    return t;
}

static TexSample tex_metal_panel(float u, float v) {
    TexSample t;
    // Large panels split into a 3x3 grid with some subdivided cells
    float gu = u * 3, gv = v * 3;
    int cx = (int)gu, cy = (int)gv;
    float lu = fracf(gu), lv = fracf(gv);
    if (hashcell(cx, cy, 901) > 0.5f) { lv = fracf(gv * 2); }
    float seam = smoothstepf(0.02f, 0.005f, fminf(fminf(lu, 1 - lu), fminf(lv, 1 - lv)));
    float bu = fminf(lu, 1 - lu), bv = fminf(lv, 1 - lv);
    float bolt = smoothstepf(0.035f, 0.02f, sqrtf((bu - 0.06f) * (bu - 0.06f) + (bv - 0.06f) * (bv - 0.06f)));
    float n = fbm(u, v, 24, 3, 903);
    float scratch = smoothstepf(0.7f, 0.95f, fbm(u * 1.0f, v * 0.05f, 64, 2, 905));
    float g = 0.24f + n * 0.05f + hashcell(cx, cy, 907) * 0.05f + scratch * 0.15f;
    t.albedo = vec3(g * 0.95f, g, g * 1.06f) * (1.0f - seam * 0.5f);
    t.rough = 0.42f - scratch * 0.15f + seam * 0.3f;
    t.height = 0.6f - seam * 0.6f + bolt * 0.4f;
    t.ao = 1.0f - seam * 0.5f;
    return t;
}

static TexSample tex_alien(float u, float v) {
    TexSample t;
    float f1, edge;
    noise_worley_tiled(u * 6, v * 6, 6, 1001, &f1, &edge, 0);
    float veins = smoothstepf(0.08f, 0.0f, edge);
    float n = fbm(u, v, 16, 4, 1003);
    float plate = smoothstepf(0.0f, 0.6f, f1);
    vec3 chitin = lerp(vec3(0.05f, 0.04f, 0.07f), vec3(0.14f, 0.10f, 0.16f), n);
    t.albedo = lerp(chitin, vec3(0.2f, 0.45f, 0.5f), veins * 0.6f);
    t.rough = 0.3f + n * 0.2f;
    t.height = (1.0f - plate) * 0.6f + n * 0.3f + veins * 0.2f;
    t.ao = 0.6f + (1.0f - veins) * 0.4f;
    return t;
}

static TexSample tex_fabric(float u, float v) {
    TexSample t;
    float N = 48;
    float w = sinf(u * TAU_F * N) * sinf(v * TAU_F * N);
    float n = fbm(u, v, 16, 3, 1101);
    float g = 0.78f + w * 0.06f + n * 0.12f;
    t.albedo = vec3(g, g, g);
    t.rough = 0.95f;
    t.height = 0.5f + w * 0.25f + n * 0.2f;
    t.ao = 0.9f + w * 0.1f;
    return t;
}

static TexSample tex_skin(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 16, 4, 1201);
    float pores = hashcell((int)(u * 256), (int)(v * 256), 1203) > 0.9f ? 1.0f : 0.0f;
    float g = 0.92f + n * 0.1f - pores * 0.04f;
    t.albedo = vec3(g, g * 0.97f, g * 0.95f);
    t.rough = 0.52f + n * 0.1f;
    t.height = 0.5f + n * 0.2f - pores * 0.2f;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_rust(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 16, 5, 1301);
    float big = fbm(u, v, 4, 3, 1303);
    vec3 c = lerp(vec3(0.25f, 0.11f, 0.05f), vec3(0.5f, 0.25f, 0.1f), n);
    c = lerp(c, vec3(0.2f, 0.2f, 0.21f), smoothstepf(0.6f, 0.75f, big));
    t.albedo = c;
    t.rough = 0.85f;
    t.height = n;
    t.ao = 0.8f + n * 0.2f;
    return t;
}

static TexSample tex_paint(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 32, 3, 1401);
    float wear = smoothstepf(0.6f, 0.72f, fbm(u, v, 8, 4, 1403));
    float g = 0.88f - wear * 0.55f + n * 0.06f;
    t.albedo = vec3(g, g, g);
    t.rough = 0.6f + wear * 0.3f;
    t.height = 0.55f - wear * 0.2f;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_plaster(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 16, 5, 1501);
    float g = 0.74f + n * 0.08f;
    t.albedo = vec3(g, g * 0.98f, g * 0.94f);
    t.rough = 0.85f;
    t.height = n;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_carpaint(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 8, 2, 1601);
    t.albedo = vec3(0.9f, 0.9f, 0.9f) * (0.97f + n * 0.03f);
    t.rough = 0.22f + n * 0.04f;
    t.height = 0.5f;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_leather(float u, float v) {
    TexSample t;
    float f1, edge;
    noise_worley_tiled(u * 20, v * 20, 20, 1701, &f1, &edge, 0);
    float crease = smoothstepf(0.1f, 0.0f, edge);
    float n = fbm(u, v, 8, 4, 1703);
    float g = 0.86f + n * 0.1f - crease * 0.15f;
    t.albedo = vec3(g, g, g);
    t.rough = 0.42f + crease * 0.2f + n * 0.1f;
    t.height = 0.6f - crease * 0.5f + n * 0.2f;
    t.ao = 1.0f - crease * 0.2f;
    return t;
}

static TexSample tex_steel(float u, float v) {
    TexSample t;
    float brushed = fbm(u * 1.0f, v * 0.02f, 128, 2, 1801);
    float g = 0.62f + brushed * 0.1f;
    t.albedo = vec3(g, g, g * 1.02f);
    t.rough = 0.25f + brushed * 0.1f;
    t.height = 0.5f + brushed * 0.1f;
    t.ao = 1.0f;
    return t;
}

static TexSample tex_rubber(float u, float v) {
    TexSample t;
    float tread = fabsf(fracf(u * 6 + fabsf(fracf(v * 4) - 0.5f) * 0.5f) - 0.5f);
    float groove = smoothstepf(0.1f, 0.04f, tread);
    float n = fbm(u, v, 16, 2, 1901);
    float g = 0.07f + n * 0.03f;
    t.albedo = vec3(g, g, g);
    t.rough = 0.9f;
    t.height = 0.7f - groove * 0.6f;
    t.ao = 1.0f - groove * 0.4f;
    return t;
}

static TexSample tex_hair(float u, float v) {
    TexSample t;
    float strands = fbm(u * 1.0f, v * 0.06f, 128, 2, 2001);
    float g = 0.7f + strands * 0.35f;
    t.albedo = vec3(g, g, g);
    t.rough = 0.5f;
    t.height = strands;
    t.ao = 0.8f + strands * 0.2f;
    return t;
}

static TexSample tex_floor_concrete(float u, float v) {
    TexSample t;
    float n = fbm(u, v, 16, 5, 2101);
    float stain = smoothstepf(0.55f, 0.8f, fbm(u, v, 3, 4, 2103));
    float f1, edge;
    noise_worley_tiled(u * 2, v * 2, 2, 2105, &f1, &edge, 0);
    float crack = smoothstepf(0.02f, 0.0f, edge) * smoothstepf(0.5f, 0.6f, fbm(u, v, 8, 2, 2107));
    float g = 0.42f + n * 0.08f - stain * 0.12f - crack * 0.15f;
    t.albedo = vec3(g, g * 0.99f, g * 0.97f);
    t.rough = 0.45f + stain * 0.2f + n * 0.1f;
    t.height = 0.5f + n * 0.2f - crack * 0.5f;
    t.ao = 1.0f - crack * 0.4f;
    return t;
}

typedef TexSample (*TexFn)(float u, float v);
static const TexFn g_texFns[MAT_COUNT] = {
    tex_asphalt, tex_sidewalk, tex_brick, tex_concrete, tex_glass_facade, tex_corrugated,
    tex_grass, tex_dirt, tex_roof, tex_wood, tex_tiles, tex_metal_panel, tex_alien,
    tex_fabric, tex_skin, tex_rust, tex_paint, tex_plaster, tex_carpaint, tex_leather,
    tex_steel, tex_rubber, tex_hair, tex_floor_concrete,
};
static const float g_normalStrength[MAT_COUNT] = {
    2.0f, 3.0f, 4.0f, 2.0f, 1.0f, 6.0f, 2.0f, 3.0f, 2.0f, 3.0f, 3.0f, 3.0f, 5.0f,
    1.5f, 0.6f, 2.0f, 1.0f, 1.0f, 0.3f, 2.0f, 0.8f, 3.0f, 1.0f, 2.0f,
};

void texgen_layer(int layer, int size, u8* albedoOut, u8* normalOut) {
    TexFn fn = g_texFns[layer];
    float* height = (float*)malloc(sizeof(float) * (size_t)(size * size));
    float inv = 1.0f / size;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            TexSample s = fn((x + 0.5f) * inv, (y + 0.5f) * inv);
            u8* a = albedoOut + (y * size + x) * 4;
            a[0] = (u8)(saturate(s.albedo.x) * 255.0f);
            a[1] = (u8)(saturate(s.albedo.y) * 255.0f);
            a[2] = (u8)(saturate(s.albedo.z) * 255.0f);
            a[3] = (u8)(saturate(s.rough) * 255.0f);
            height[y * size + x] = s.height;
            normalOut[(y * size + x) * 4 + 2] = (u8)(saturate(s.ao) * 255.0f);
            normalOut[(y * size + x) * 4 + 3] = (u8)(saturate(s.height) * 255.0f);
        }
    }
    float str = g_normalStrength[layer] * size / 256.0f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float hl = height[y * size + ((x - 1 + size) % size)];
            float hr = height[y * size + ((x + 1) % size)];
            float hd = height[((y - 1 + size) % size) * size + x];
            float hu = height[((y + 1) % size) * size + x];
            vec3 n = normalize(vec3(-(hr - hl) * str * 0.5f, -(hu - hd) * str * 0.5f, 1.0f));
            u8* o = normalOut + (y * size + x) * 4;
            o[0] = (u8)clampf((n.x * 0.5f + 0.5f) * 255.0f, 0, 255);
            o[1] = (u8)clampf((n.y * 0.5f + 0.5f) * 255.0f, 0, 255);
        }
    }
    free(height);
}
