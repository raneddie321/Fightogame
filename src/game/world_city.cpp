// FIGHTO - procedural generation of Blackridge: roads, blocks, districts,
// buildings, props, street lighting and neon.
#include "world_gen.h"

World g_world;

static MeshBuilder* g_chunkBuilders[GEN_CHUNKS * GEN_CHUNKS];

MeshBuilder& gen_chunk(vec3 p) {
    int cx = clamp_t((int)floorf((p.x - GEN_ORIGIN) / GEN_CHUNK), 0, GEN_CHUNKS - 1);
    int cz = clamp_t((int)floorf((p.z - GEN_ORIGIN) / GEN_CHUNK), 0, GEN_CHUNKS - 1);
    // Underground geometry goes into separate chunks (offset index space)
    int idx = cz * GEN_CHUNKS + cx;
    if (!g_chunkBuilders[idx]) {
        g_chunkBuilders[idx] = (MeshBuilder*)calloc(1, sizeof(MeshBuilder));
        g_chunkBuilders[idx]->reset();
    }
    return *g_chunkBuilders[idx];
}

void gen_flush_chunks() {
    int count = 0;
    long verts = 0;
    for (int i = 0; i < GEN_CHUNKS * GEN_CHUNKS; i++) {
        MeshBuilder* mb = g_chunkBuilders[i];
        if (!mb) continue;
        if (mb->inds.count > 0) {
            // Split very large chunks to stay friendly to 16-bit-minded drivers
            Mesh m = mesh_create(*mb);
            renderer_add_chunk(m);
            count++;
            verts += mb->verts.count;
        }
        mb->free_mem();
        free(mb);
        g_chunkBuilders[i] = 0;
    }
    g_world.chunkCount = count;
    log_info("World mesh: %d chunks, %ld vertices", count, verts);
}

void gen_box(vec3 mn, vec3 mx, int layer, vec3 color, int flags, float rough, float emissive, bool top, bool bottom) {
    MeshBuilder& mb = gen_chunk((mn + mx) * 0.5f);
    mb.set_mat(layer, flags, rough, emissive);
    mb.set_color(color);
    mb.box(mn, mx, top, bottom);
}

void gen_solid(vec3 mn, vec3 mx, int layer, vec3 color, int surface, int flags, float rough) {
    gen_box(mn, mx, layer, color, flags, rough);
    world_add_box(mn, mx, surface, 1);
}

void gen_cyl(vec3 base, float r, float h, int seg, int layer, vec3 color, int flags, float rough, float emissive, bool solid) {
    MeshBuilder& mb = gen_chunk(base);
    mb.set_mat(layer, flags, rough, emissive);
    mb.set_color(color);
    mb.cylinder(base, r, h, seg, true);
    if (solid) world_add_box(base - vec3(r * 0.85f, 0, r * 0.85f), base + vec3(r * 0.85f, h, r * 0.85f), SURF_METAL, 1);
}

void gen_tube(vec3 a, vec3 b, float r, int seg, int layer, vec3 color, int flags, float emissive) {
    MeshBuilder& mb = gen_chunk((a + b) * 0.5f);
    mb.set_mat(layer, flags, 1.0f, emissive);
    mb.set_color(color);
    mb.tube(a, b, r, r, seg, true, true);
}

void gen_ellipsoid(vec3 c, vec3 r, int layer, vec3 color, int flags, float rough, float emissive) {
    MeshBuilder& mb = gen_chunk(c);
    mb.set_mat(layer, flags, rough, emissive);
    mb.set_color(color);
    mb.ellipsoid(c, r, 10, 7);
}

void gen_quad(vec3 a, vec3 b, vec3 c, vec3 d, int layer, vec3 color, int flags, float rough, float emissive) {
    MeshBuilder& mb = gen_chunk((a + c) * 0.5f);
    mb.set_mat(layer, flags, rough, emissive);
    mb.set_color(color);
    mb.quad_auto(a, b, c, d);
}

void gen_light(vec3 pos, float radius, vec3 color, float intensity) {
    Light l;
    l.pos = pos;
    l.radius = radius;
    l.color = color * intensity;
    l.spotDir = vec3(0, -1, 0);
    l.spotCos = 0;
    renderer_add_static_light(l);
}

void gen_spot(vec3 pos, vec3 dir, float radius, vec3 color, float intensity, float cosAngle) {
    Light l;
    l.pos = pos;
    l.radius = radius;
    l.color = color * intensity;
    l.spotDir = normalize(dir);
    l.spotCos = cosAngle;
    renderer_add_static_light(l);
}

void gen_beam(vec3 a, vec3 b, float r0, float r1, vec3 color, float intensity) {
    BeamDraw bd;
    bd.a = a;
    bd.b = b;
    bd.r0 = r0;
    bd.r1 = r1;
    bd.color = color;
    bd.intensity = intensity;
    bd.alpha = 1.0f;
    renderer_add_static_beam(bd);
}

void gen_indoor(vec3 mn, vec3 mx, float ambient) {
    IndoorVolume v;
    v.box.mn = mn;
    v.box.mx = mx;
    v.ambient = ambient;
    g_world.indoors.push(v);
}

void gen_room(vec3 mn, vec3 mx, float wall, int layerWall, vec3 wallColor, int layerFloor, vec3 floorColor, int doorMask, float doorW, float doorH, bool ceiling,
              int layerOutside, vec3 outsideColor, int outsideFlags) {
    float h = mx.y - mn.y;
    // Floor
    gen_box(vec3(mn.x, mn.y - 0.2f, mn.z), vec3(mx.x, mn.y, mx.z), layerFloor, floorColor, MF_GROUND, 0.8f);
    world_add_box(vec3(mn.x, mn.y - 0.2f, mn.z), vec3(mx.x, mn.y, mx.z), SURF_CONCRETE, 1);
    if (ceiling) {
        gen_box(vec3(mn.x, mx.y, mn.z), vec3(mx.x, mx.y + 0.3f, mx.z), layerWall, wallColor * 0.8f, 0, 1.0f, 0, true, true);
        world_add_box(vec3(mn.x, mx.y, mn.z), vec3(mx.x, mx.y + 0.3f, mx.z), SURF_CONCRETE, 1);
    }
    int lo = layerOutside >= 0 ? layerOutside : layerWall;
    vec3 oc = layerOutside >= 0 ? outsideColor : wallColor;
    // Walls: for each side, either full or split around a door
    for (int side = 0; side < 4; side++) {
        bool alongX = side < 2;   // -Z/+Z walls run along X
        float a0 = alongX ? mn.x : mn.z, a1 = alongX ? mx.x : mx.z;
        float c = (side == 0) ? mn.z : (side == 1) ? mx.z - wall : (side == 2) ? mn.x : mx.x - wall;
        bool door = (doorMask & (1 << side)) != 0;
        float mid = (a0 + a1) * 0.5f;
        float segs[3][2];
        int ns = 0;
        if (door) {
            segs[ns][0] = a0; segs[ns][1] = mid - doorW * 0.5f; ns++;
            segs[ns][0] = mid + doorW * 0.5f; segs[ns][1] = a1; ns++;
        } else {
            segs[ns][0] = a0; segs[ns][1] = a1; ns++;
        }
        for (int s = 0; s < ns; s++) {
            vec3 bmn, bmx;
            if (alongX) { bmn = vec3(segs[s][0], mn.y, c); bmx = vec3(segs[s][1], mx.y, c + wall); }
            else { bmn = vec3(c, mn.y, segs[s][0]); bmx = vec3(c + wall, mx.y, segs[s][1]); }
            if (bmx.x - bmn.x < 0.01f || bmx.z - bmn.z < 0.01f) continue;
            gen_box(bmn, bmx, lo, oc, outsideFlags, 1.0f);
            // Inner skin with interior material (slightly inset)
            vec3 imn = bmn, imx = bmx;
            if (alongX) { if (side == 0) imn.z = imx.z - 0.02f, imx.z += 0.001f; else imx.z = imn.z + 0.02f, imn.z -= 0.001f; }
            else { if (side == 2) imn.x = imx.x - 0.02f, imx.x += 0.001f; else imx.x = imn.x + 0.02f, imn.x -= 0.001f; }
            if (lo != layerWall) gen_box(imn, imx, layerWall, wallColor, 0, 1.0f, 0, false, false);
            world_add_box(bmn, bmx, SURF_CONCRETE, 1);
        }
        if (door && doorH < h) {
            vec3 bmn, bmx;
            if (alongX) { bmn = vec3(mid - doorW * 0.5f, mn.y + doorH, c); bmx = vec3(mid + doorW * 0.5f, mx.y, c + wall); }
            else { bmn = vec3(c, mn.y + doorH, mid - doorW * 0.5f); bmx = vec3(c + wall, mx.y, mid + doorW * 0.5f); }
            gen_box(bmn, bmx, lo, oc, outsideFlags, 1.0f, 0, true, true);
            world_add_box(bmn, bmx, SURF_CONCRETE, 1);
        }
    }
}

void gen_crate(vec3 base, vec3 size, u32 seed) {
    float t = hash_f01(seed);
    vec3 col = t < 0.5f ? vec3(0.62f, 0.5f, 0.36f) : vec3(0.3f, 0.36f, 0.24f);
    int layer = t < 0.5f ? MAT_WOOD : MAT_METAL_PANEL;
    gen_solid(base - vec3(size.x * 0.5f, 0, size.z * 0.5f), base + vec3(size.x * 0.5f, size.y, size.z * 0.5f), layer, col, t < 0.5f ? SURF_WOOD : SURF_METAL);
}

void gen_container(vec3 base, float yaw90, vec3 color) {
    vec3 half = yaw90 > 0.5f ? vec3(1.25f, 0, 6.0f) : vec3(6.0f, 0, 1.25f);
    vec3 mn = base - half, mx = base + half + vec3(0, 2.6f, 0);
    gen_box(mn, mx, MAT_CORRUGATED, color, MF_METAL, 1.0f);
    world_add_box(mn, mx, SURF_METAL, 1);
}

void gen_car_wreck(vec3 base, float yaw, vec3 color) {
    MeshBuilder& mb = gen_chunk(base);
    int start = mb.vcount();
    mb.set_mat(MAT_RUST, 0, 1.0f);
    mb.set_color(color);
    mb.box(vec3(-0.95f, 0.3f, -2.2f), vec3(0.95f, 1.0f, 2.2f));
    mb.set_color(color * 0.7f);
    mb.box(vec3(-0.85f, 1.0f, -1.0f), vec3(0.85f, 1.45f, 1.0f));
    mb.set_mat(MAT_RUBBER, 0, 1.0f);
    mb.set_color(vec3(0.2f, 0.2f, 0.2f));
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? 0.85f : -0.85f, z = (i & 2) ? 1.4f : -1.4f;
        mb.box(vec3(x - 0.12f, 0.0f, z - 0.35f), vec3(x + 0.12f, 0.6f, z + 0.35f));
    }
    mb.transform(start, mat4_trs(base, quat_axis_angle(vec3(0, 1, 0), yaw), vec3(1, 1, 1)));
    vec3 e(1.2f, 0, 1.2f);
    world_add_box(base - vec3(1.4f, 0, 1.4f), base + vec3(1.4f, 1.45f, 1.4f), SURF_METAL, 1);
    FIGHTO_UNUSED(e);
}

void gen_tree(vec3 base, float s, u32 seed) {
    Rng rng;
    rng.seed(seed);
    float trunkH = 3.0f * s;
    MeshBuilder& mb = gen_chunk(base);
    mb.set_mat(MAT_WOOD, 0, 1.0f);
    mb.set_color(vec3(0.35f, 0.28f, 0.22f));
    mb.tube(base, base + vec3(0, trunkH, 0), 0.18f * s, 0.11f * s, 6, false, true);
    mb.set_mat(MAT_GRASS, MF_FOLIAGE, 1.0f);
    int blobs = 4 + (int)(rng.f01() * 3);
    for (int i = 0; i < blobs; i++) {
        vec3 c = base + vec3(rng.range(-1.2f, 1.2f) * s, trunkH + rng.range(0.4f, 2.2f) * s, rng.range(-1.2f, 1.2f) * s);
        float r = rng.range(1.0f, 1.7f) * s;
        mb.set_color(vec3(0.38f, 0.52f, 0.3f) * rng.range(0.7f, 1.1f));
        mb.ellipsoid(c, vec3(r, r * 0.8f, r), 8, 6);
    }
    world_add_box(base - vec3(0.25f, 0, 0.25f), base + vec3(0.25f, trunkH, 0.25f), SURF_WOOD, 1);
}

void gen_streetlight(vec3 base, vec3 armDir, bool led) {
    float h = 7.5f;
    MeshBuilder& mb = gen_chunk(base);
    mb.set_mat(MAT_STEEL, MF_METAL, 2.0f);
    mb.set_color(vec3(0.22f, 0.23f, 0.24f));
    mb.cone_frustum(base, 0.13f, 0.08f, h, 6, true);
    vec3 top = base + vec3(0, h, 0);
    vec3 end = top + armDir * 2.2f - vec3(0, 0.15f, 0);
    mb.tube(top, end, 0.06f, 0.05f, 5, false, true);
    vec3 col = led ? vec3(0.82f, 0.9f, 1.0f) : vec3(1.0f, 0.62f, 0.28f);
    // Lamp head
    vec3 side = normalize(cross(armDir, vec3(0, 1, 0)));
    vec3 hc = end - vec3(0, 0.08f, 0);
    mb.set_mat(MAT_STEEL, MF_METAL, 1.5f);
    mb.set_color(vec3(0.15f, 0.15f, 0.16f));
    int s0 = mb.vcount();
    mb.box(vec3(-0.25f, -0.06f, -0.45f), vec3(0.25f, 0.1f, 0.45f));
    mat4 xf = mat4_identity();
    xf.m[0] = side.x; xf.m[1] = side.y; xf.m[2] = side.z;
    xf.m[8] = armDir.x; xf.m[9] = armDir.y; xf.m[10] = armDir.z;
    xf.m[12] = hc.x; xf.m[13] = hc.y; xf.m[14] = hc.z;
    mb.transform(s0, xf);
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 25.0f);
    mb.set_color(col);
    s0 = mb.vcount();
    mb.box(vec3(-0.2f, -0.1f, -0.38f), vec3(0.2f, -0.05f, 0.38f));
    mb.transform(s0, xf);
    world_add_box(base - vec3(0.15f, 0, 0.15f), base + vec3(0.15f, h, 0.15f), SURF_METAL, 1);
    vec3 lp = hc - vec3(0, 0.4f, 0);
    Light l;
    l.pos = lp;
    l.radius = 24.0f;
    l.color = col * (led ? 46.0f : 42.0f);
    l.spotDir = vec3(0, -1, 0);
    l.spotCos = 0;
    int id = renderer_add_static_light(l);
    g_world.streetLightIds.push(id);
    g_world.streetLightOrig.push(l);
    gen_beam(hc - vec3(0, 0.1f, 0), vec3(hc.x, 0.3f, hc.z), 0.3f, 3.2f, col, 0.18f);
}

// ---------------------------------------------------------------------------
// Neon stroke font (x 0..4, y 0..6)
// ---------------------------------------------------------------------------
static const char* neon_glyph(char c) {
    switch (c) {
    case 'A': return "00-04 04-26 26-44 44-40 03-43";
    case 'B': return "00-06 06-36 36-45 45-34 34-03 34-42 42-41 41-30 30-00";
    case 'C': return "46-16 16-05 05-01 01-10 10-40";
    case 'D': return "00-06 06-26 26-44 44-42 42-20 20-00";
    case 'E': return "40-00 00-06 06-46 03-33";
    case 'F': return "00-06 06-46 03-33";
    case 'G': return "46-16 16-05 05-01 01-10 10-40 40-43 43-23";
    case 'H': return "00-06 40-46 03-43";
    case 'I': return "10-30 20-26 16-36";
    case 'J': return "46-41 41-30 30-10 10-01";
    case 'K': return "00-06 03-46 03-40";
    case 'L': return "06-00 00-40";
    case 'M': return "00-06 06-23 23-46 46-40";
    case 'N': return "00-06 06-40 40-46";
    case 'O': case '0': return "10-30 30-41 41-45 45-36 36-16 16-05 05-01 01-10";
    case 'P': return "00-06 06-36 36-45 45-44 44-33 33-03";
    case 'Q': return "10-30 30-41 41-45 45-36 36-16 16-05 05-01 01-10 22-40";
    case 'R': return "00-06 06-36 36-45 45-44 44-33 33-03 23-40";
    case 'S': return "45-36 36-16 16-05 05-04 04-13 13-33 33-42 42-41 41-30 30-10 10-01";
    case 'T': return "06-46 26-20";
    case 'U': return "06-01 01-10 10-30 30-41 41-46";
    case 'V': return "06-20 20-46";
    case 'W': return "06-10 10-23 23-30 30-46";
    case 'X': return "00-46 06-40";
    case 'Y': return "06-23 46-23 23-20";
    case 'Z': return "06-46 46-00 00-40";
    case '1': return "15-26 26-20 10-30";
    case '2': return "05-16 16-36 36-45 45-44 44-00 00-40";
    case '3': return "06-46 46-23 23-33 33-42 42-41 41-30 30-10 10-01";
    case '4': return "30-36 36-02 02-42";
    case '5': return "46-06 06-03 03-33 33-42 42-41 41-30 30-00";
    case '6': return "46-16 16-05 05-01 01-10 10-30 30-41 41-42 42-33 33-03";
    case '7': return "06-46 46-10";
    case '8': return "13-33 33-44 44-45 45-36 36-16 16-05 05-04 04-13 13-02 02-01 01-10 10-30 30-41 41-42 42-33";
    case '9': return "43-13 13-04 04-05 05-16 16-36 36-45 45-41 41-30 30-10";
    case '-': return "13-33";
    case '+': return "13-33 22-24";
    case '\'': return "25-26";
    case '&': return "40-05 05-16 16-25 25-03 03-01 01-10 10-20 20-42";
    case '.': return "20-21";
    default: return "";
    }
}

void gen_neon_text(vec3 origin, vec3 right, vec3 up, float h, const char* text, vec3 color, bool addLight, bool backing) {
    float unit = h / 6.0f;
    float adv = 5.2f * unit;
    int n = (int)strlen(text);
    vec3 normal = normalize(cross(right, up));
    MeshBuilder& mb = gen_chunk(origin);
    if (backing) {
        float w = n * adv + unit * 2;
        int s0 = mb.vcount();
        mb.set_mat(MAT_METAL_PANEL, MF_METAL, 1.0f);
        mb.set_color(vec3(0.08f, 0.08f, 0.09f));
        vec3 a = origin - right * unit - up * unit - normal * 0.12f;
        vec3 b = a + right * w;
        vec3 c = b + up * (h + unit * 2);
        vec3 d = a + up * (h + unit * 2);
        mb.quad_auto(a, b, c, d);
        mb.quad_auto(b, a, d, c);
        FIGHTO_UNUSED(s0);
    }
    mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 22.0f);
    mb.set_color(color);
    float r = unit * 0.32f;
    for (int i = 0; i < n; i++) {
        char ch = text[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        const char* g = neon_glyph(ch);
        vec3 lo = origin + right * (i * adv);
        while (*g) {
            if (*g == ' ') { g++; continue; }
            int x0 = g[0] - '0', y0 = g[1] - '0', x1 = g[3] - '0', y1 = g[4] - '0';
            vec3 a = lo + right * (x0 * unit) + up * (y0 * unit);
            vec3 b = lo + right * (x1 * unit) + up * (y1 * unit);
            if (length_sq(b - a) < 1e-6f) b = a + up * (unit * 0.3f);
            mb.tube(a, b, r, r, 4, true, true);
            g += 5;
        }
    }
    if (addLight) {
        vec3 c = origin + right * (n * adv * 0.5f) + up * (h * 0.5f) + normal * 1.0f;
        gen_light(c, 9.0f + n * 0.8f, color, 10.0f);
    }
}

// ---------------------------------------------------------------------------
// Districts
// ---------------------------------------------------------------------------
static const char g_districtMap[CITY_BLOCKS][CITY_BLOCKS + 1] = {
    "WWWMMMMII",
    "WWWMMMMII",
    "WWWDDDDII",
    "WWMDDDDII",
    "BBMDDDDII",
    "BBBDDDDII",
    "BBBBMMIII",
    "BBBBMMIII",
    "BBBBMMIII",
};

int block_district(int i, int j) {
    if (i < 0 || j < 0 || i >= CITY_BLOCKS || j >= CITY_BLOCKS) return D_OUTSKIRTS;
    switch (g_districtMap[j][i]) {
    case 'W': return D_WESTHILLS;
    case 'M': return D_MIDTOWN;
    case 'D': return D_DOWNTOWN;
    case 'B': return D_BLOCKS;
    default: return D_IRONWORKS;
    }
}

bool block_of(vec3 p, int* i, int* j) {
    int bi = (int)floorf((p.x + CITY_HALF) / BLOCK_PITCH);
    int bj = (int)floorf((p.z + CITY_HALF) / BLOCK_PITCH);
    if (i) *i = bi;
    if (j) *j = bj;
    return bi >= 0 && bj >= 0 && bi < CITY_BLOCKS && bj < CITY_BLOCKS;
}

int district_at(vec3 p) {
    if (p.y < -60.0f) return D_AEGIS;
    if (p.y < -20.0f) return D_DRAIN;
    int i, j;
    if (!block_of(p, &i, &j)) return D_OUTSKIRTS;
    return block_district(i, j);
}

const char* district_name(int d) {
    switch (d) {
    case D_DOWNTOWN: return "DOWNTOWN";
    case D_BLOCKS: return "THE BLOCKS";
    case D_IRONWORKS: return "IRONWORKS";
    case D_WESTHILLS: return "WEST HILLS";
    case D_MIDTOWN: return "MIDTOWN";
    case D_AEGIS: return "A.E.G.I.S. FACILITY";
    case D_DRAIN: return "STORM DRAINS";
    default: return "OUTSKIRTS";
    }
}

static const char* g_shopSigns[] = {"HOTEL", "DRUGS", "LIQUOR", "BAR", "24H", "PIZZA", "OPEN", "MOTEL", "CLUB", "DELI", "TATTOO", "LOANS", "NOODLES", "PAWN", "GYM"};
static const vec3 g_neonColors[] = {
    vec3(1.0f, 0.15f, 0.35f), vec3(0.2f, 0.85f, 1.0f), vec3(1.0f, 0.55f, 0.1f), vec3(0.4f, 1.0f, 0.4f),
    vec3(0.85f, 0.3f, 1.0f), vec3(1.0f, 0.9f, 0.3f), vec3(1.0f, 0.25f, 0.15f),
};

// Storefront along one face of a building. face: 0 -Z, 1 +Z, 2 -X, 3 +X
void storefront(vec3 mn, vec3 mx, int face, Rng& rng, bool sign) {
    float sh = 4.0f;
    vec3 n, r;
    vec3 o;
    float len;
    switch (face) {
    case 0: n = vec3(0, 0, -1); r = vec3(-1, 0, 0); o = vec3(mx.x, 0, mn.z); len = mx.x - mn.x; break;
    case 1: n = vec3(0, 0, 1); r = vec3(1, 0, 0); o = vec3(mn.x, 0, mx.z); len = mx.x - mn.x; break;
    case 2: n = vec3(-1, 0, 0); r = vec3(0, 0, 1); o = vec3(mn.x, 0, mn.z); len = mx.z - mn.z; break;
    default: n = vec3(1, 0, 0); r = vec3(0, 0, -1); o = vec3(mx.x, 0, mx.z); len = mx.z - mn.z; break;
    }
    o.y = CURB_H;
    // Dark glass shop window with a warm interior glow strip
    vec3 a = o + r * 0.8f + n * 0.03f + vec3(0, 0.4f, 0);
    vec3 b = o + r * (len - 0.8f) + n * 0.03f + vec3(0, 0.4f, 0);
    gen_quad(a, b, b + vec3(0, sh - 1.2f, 0), a + vec3(0, sh - 1.2f, 0), MAT_PAINT, vec3(0.05f, 0.05f, 0.06f), MF_GLASS, 0.1f);
    vec3 glow = rng.chance(0.5f) ? vec3(1.0f, 0.75f, 0.45f) : vec3(0.7f, 0.85f, 1.0f);
    gen_quad(a + n * 0.01f + vec3(0, sh - 1.45f, 0), b + n * 0.01f + vec3(0, sh - 1.45f, 0), b + n * 0.01f + vec3(0, sh - 1.25f, 0), a + n * 0.01f + vec3(0, sh - 1.25f, 0),
             MAT_PAINT, glow, MF_EMISSIVE, 1.0f, 12.0f);
    // Awning
    if (rng.chance(0.5f)) {
        vec3 ac = g_neonColors[rng.irange(0, ARRAY_COUNT(g_neonColors) - 1)] * 0.5f;
        vec3 p0 = o + r * 0.5f + vec3(0, sh, 0), p1 = o + r * (len - 0.5f) + vec3(0, sh, 0);
        gen_quad(p0, p1, p1 + n * 1.6f - vec3(0, 0.7f, 0), p0 + n * 1.6f - vec3(0, 0.7f, 0), MAT_FABRIC, ac, 0, 1.0f);
    }
    gen_light(o + r * (len * 0.5f) + n * 1.5f + vec3(0, 2.2f, 0), 9.0f, glow, 6.0f);
    if (sign) {
        const char* txt = g_shopSigns[rng.irange(0, ARRAY_COUNT(g_shopSigns) - 1)];
        vec3 col = g_neonColors[rng.irange(0, ARRAY_COUNT(g_neonColors) - 1)];
        float lh = 0.9f;
        float w = (float)strlen(txt) * lh * 5.2f / 6.0f;
        if (rng.chance(0.35f) && len > 6) {
            // Vertical blade sign sticking out from the facade
            vec3 base = o + r * (len * 0.2f) + n * 0.2f + vec3(0, sh + 1.0f, 0);
            int n2 = (int)strlen(txt);
            for (int k = 0; k < n2; k++) {
                char one[2] = {txt[k], 0};
                gen_neon_text(base + vec3(0, (n2 - 1 - k) * lh * 1.25f, 0) + n * 0.6f, n, vec3(0, 1, 0), lh, one, col, k == n2 / 2, true);
            }
        } else if (w < len - 1.0f) {
            vec3 start = o + r * ((len - w) * 0.5f) + n * 0.25f + vec3(0, sh + 0.3f, 0);
            gen_neon_text(start, r, vec3(0, 1, 0), lh, txt, col, true, true);
        }
    }
}

// Fire escape on a facade
static void fire_escape(vec3 o, vec3 r, vec3 n, float width, int floors) {
    vec3 col(0.12f, 0.12f, 0.13f);
    for (int f = 1; f < floors; f++) {
        float y = CURB_H + f * 3.4f;
        vec3 a = o + vec3(0, y, 0);
        vec3 b = a + r * width;
        MeshBuilder& mb = gen_chunk(a);
        mb.set_mat(MAT_STEEL, MF_METAL, 1.6f);
        mb.set_color(col);
        mb.quad_auto(a + n * 0.05f, b + n * 0.05f, b + n * 1.3f, a + n * 1.3f);
        mb.quad_auto(a + n * 1.3f, b + n * 1.3f, b + n * 0.05f, a + n * 0.05f);
        // Railing
        mb.tube(a + n * 1.3f + vec3(0, 1.0f, 0), b + n * 1.3f + vec3(0, 1.0f, 0), 0.025f, 0.025f, 4);
        for (int k = 0; k <= 4; k++) {
            vec3 p = a + r * (width * k / 4.0f) + n * 1.3f;
            mb.tube(p, p + vec3(0, 1.0f, 0), 0.02f, 0.02f, 4, false, false);
        }
        // Stairs to the next platform
        if (f + 1 < floors) {
            vec3 s0 = a + r * (width * 0.15f) + n * 0.7f;
            vec3 s1 = a + r * (width * 0.75f) + n * 0.7f + vec3(0, 3.4f, 0);
            mb.tube(s0, s1, 0.05f, 0.05f, 4);
        }
    }
}

static void rooftop_details(vec3 mn, vec3 mx, float top, Rng& rng, bool waterTower) {
    // Parapet
    float p = 0.6f;
    gen_box(vec3(mn.x, top, mn.z), vec3(mx.x, top + p, mn.z + 0.3f), MAT_CONCRETE, vec3(0.5f, 0.5f, 0.5f));
    gen_box(vec3(mn.x, top, mx.z - 0.3f), vec3(mx.x, top + p, mx.z), MAT_CONCRETE, vec3(0.5f, 0.5f, 0.5f));
    gen_box(vec3(mn.x, top, mn.z), vec3(mn.x + 0.3f, top + p, mx.z), MAT_CONCRETE, vec3(0.5f, 0.5f, 0.5f));
    gen_box(vec3(mx.x - 0.3f, top, mn.z), vec3(mx.x, top + p, mx.z), MAT_CONCRETE, vec3(0.5f, 0.5f, 0.5f));
    int units = rng.irange(1, 3);
    for (int i = 0; i < units; i++) {
        float x = rng.range(mn.x + 2, mx.x - 4), z = rng.range(mn.z + 2, mx.z - 4);
        gen_box(vec3(x, top, z), vec3(x + rng.range(1.5f, 3.0f), top + rng.range(1.0f, 2.0f), z + rng.range(1.5f, 3.0f)), MAT_METAL_PANEL, vec3(0.55f, 0.56f, 0.58f), MF_METAL);
    }
    if (waterTower && rng.chance(0.5f)) {
        float x = rng.range(mn.x + 3, mx.x - 3), z = rng.range(mn.z + 3, mx.z - 3);
        vec3 b(x, top, z);
        for (int k = 0; k < 4; k++) {
            vec3 o((k & 1) ? 1.0f : -1.0f, 0, (k & 2) ? 1.0f : -1.0f);
            gen_tube(b + o * 1.0f, b + o * 0.9f + vec3(0, 3.0f, 0), 0.08f, 4, MAT_STEEL, vec3(0.2f, 0.2f, 0.2f), MF_METAL);
        }
        gen_cyl(b + vec3(0, 3.0f, 0), 1.6f, 3.2f, 10, MAT_WOOD, vec3(0.55f, 0.45f, 0.35f));
        MeshBuilder& mb = gen_chunk(b);
        mb.set_mat(MAT_ROOF, 0, 1.0f);
        mb.set_color(vec3(0.3f, 0.3f, 0.3f));
        mb.cone_frustum(b + vec3(0, 6.2f, 0), 1.75f, 0.1f, 1.0f, 10, false);
    }
}

// A building box with facade windows, roof and collision.
void building(vec3 mn, vec3 mx, int layer, vec3 color, Rng& rng, bool windows, bool waterTower) {
    int flags = windows ? MF_WINDOWS : 0;
    float seed = (float)rng.irange(1, 250);
    MeshBuilder& mb = gen_chunk((mn + mx) * 0.5f);
    mb.set_mat(layer, flags, 1.0f, seed * 0.1f);
    mb.set_color(color);
    mb.box(mn, mx, false, false);
    mb.set_mat(MAT_ROOF, 0, 1.0f);
    mb.set_color(vec3(0.6f, 0.6f, 0.6f));
    mb.box(vec3(mn.x, mx.y - 0.01f, mn.z), vec3(mx.x, mx.y, mx.z), true, false, false);
    world_add_box(mn, mx, layer == MAT_CORRUGATED ? SURF_METAL : SURF_CONCRETE, 1);
    rooftop_details(mn, mx, mx.y, rng, waterTower);
}

void tower(vec3 mn, vec3 mx, float height, Rng& rng) {
    int style = rng.irange(0, 2);
    vec3 col = style == 0 ? vec3(0.55f, 0.65f, 0.78f) : style == 1 ? vec3(0.7f, 0.68f, 0.64f) : vec3(0.35f, 0.38f, 0.42f);
    int layer = style == 1 ? MAT_CONCRETE : MAT_GLASS_FACADE;
    float seed = (float)rng.irange(1, 250);
    // Lobby
    float lobbyH = 6.0f;
    gen_box(vec3(mn.x, CURB_H, mn.z), vec3(mx.x, lobbyH, mx.z), MAT_PAINT, vec3(0.04f, 0.05f, 0.06f), MF_GLASS, 0.1f, 0, false);
    vec3 lg = rng.chance(0.5f) ? vec3(1.0f, 0.82f, 0.6f) : vec3(0.8f, 0.9f, 1.0f);
    gen_box(vec3(mn.x - 0.02f, lobbyH - 0.6f, mn.z - 0.02f), vec3(mx.x + 0.02f, lobbyH - 0.35f, mx.z + 0.02f), MAT_PAINT, lg, MF_EMISSIVE, 1.0f, 10.0f, false);
    gen_light(vec3((mn.x + mx.x) * 0.5f, 3.0f, mn.z - 2.0f), 14.0f, lg, 12.0f);
    gen_light(vec3((mn.x + mx.x) * 0.5f, 3.0f, mx.z + 2.0f), 14.0f, lg, 12.0f);
    world_add_box(vec3(mn.x, 0, mn.z), vec3(mx.x, lobbyH, mx.z), SURF_GLASS, 1);
    // Tiers
    int tiers = rng.irange(1, 3);
    float y = lobbyH;
    vec3 tmn = mn, tmx = mx;
    for (int t = 0; t < tiers; t++) {
        float th = (t == tiers - 1) ? height - y : (height - lobbyH) * rng.range(0.35f, 0.55f);
        if (th < 6) break;
        MeshBuilder& mb = gen_chunk((tmn + tmx) * 0.5f);
        mb.set_mat(layer, MF_WINDOWS, style == 0 ? 0.6f : 1.0f, seed * 0.1f + t);
        mb.set_color(col);
        mb.box(vec3(tmn.x, y, tmn.z), vec3(tmx.x, y + th, tmx.z), false, false);
        mb.set_mat(MAT_ROOF, 0, 1.0f);
        mb.set_color(vec3(0.45f, 0.45f, 0.47f));
        mb.box(vec3(tmn.x, y + th - 0.01f, tmn.z), vec3(tmx.x, y + th, tmx.z), true, false, false);
        world_add_box(vec3(tmn.x, y, tmn.z), vec3(tmx.x, y + th, tmx.z), SURF_CONCRETE, 1);
        // Vertical accent light strips on corners for dark towers
        if (style == 2) {
            vec3 ac = rng.chance(0.5f) ? vec3(0.3f, 0.8f, 1.0f) : vec3(1.0f, 0.95f, 0.9f);
            float s = 0.25f;
            gen_box(vec3(tmn.x - 0.05f, y, tmn.z - 0.05f), vec3(tmn.x + s, y + th, tmn.z + s), MAT_PAINT, ac, MF_EMISSIVE, 1.0f, 8.0f, false);
            gen_box(vec3(tmx.x - s, y, tmx.z - s), vec3(tmx.x + 0.05f, y + th, tmx.z + 0.05f), MAT_PAINT, ac, MF_EMISSIVE, 1.0f, 8.0f, false);
        }
        y += th;
        float inset = rng.range(3.0f, 6.0f);
        tmn = tmn + vec3(inset, 0, inset);
        tmx = tmx - vec3(inset, 0, inset);
        if (tmx.x - tmn.x < 8 || tmx.z - tmn.z < 8) break;
    }
    // Crown: aircraft warning lights and antenna
    vec3 c((mn.x + mx.x) * 0.5f, y, (mn.z + mx.z) * 0.5f);
    if (height > 70) {
        gen_tube(c, c + vec3(0, height * 0.15f, 0), 0.25f, 5, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
        gen_ellipsoid(c + vec3(0, height * 0.15f + 0.3f, 0), vec3(0.35f, 0.35f, 0.35f), MAT_PAINT, vec3(1.0f, 0.1f, 0.05f), MF_EMISSIVE | MF_PULSE, 1.0f, 25.0f);
    }
    for (int k = 0; k < 4; k++) {
        vec3 p((k & 1) ? tmx.x : tmn.x, y + 0.5f, (k & 2) ? tmx.z : tmn.z);
        gen_ellipsoid(p, vec3(0.25f, 0.25f, 0.25f), MAT_PAINT, vec3(1.0f, 0.12f, 0.05f), MF_EMISSIVE | MF_PULSE, 1.0f, 20.0f);
    }
}

void house(vec3 mn, vec3 mx, Rng& rng, bool mansion) {
    vec3 wallCols[] = {vec3(0.85f, 0.82f, 0.75f), vec3(0.7f, 0.72f, 0.75f), vec3(0.75f, 0.68f, 0.6f), vec3(0.45f, 0.5f, 0.55f)};
    vec3 col = wallCols[rng.irange(0, 3)];
    float wallH = mansion ? 9.0f : rng.chance(0.5f) ? 6.8f : 3.6f;
    float seed = (float)rng.irange(1, 250);
    MeshBuilder& mb = gen_chunk((mn + mx) * 0.5f);
    mb.set_mat(MAT_PLASTER, MF_WINDOWS, 1.0f, seed * 0.1f);
    mb.set_color(col);
    mb.box(vec3(mn.x, CURB_H, mn.z), vec3(mx.x, CURB_H + wallH, mx.z), false, false);
    world_add_box(vec3(mn.x, 0, mn.z), vec3(mx.x, CURB_H + wallH, mx.z), SURF_CONCRETE, 1);
    // Pitched roof along the longer axis
    float y0 = CURB_H + wallH;
    float rh = mansion ? 4.0f : 2.8f;
    bool alongX = (mx.x - mn.x) >= (mx.z - mn.z);
    float o = 0.6f;
    vec3 roofCol = rng.chance(0.5f) ? vec3(0.25f, 0.22f, 0.2f) : vec3(0.35f, 0.18f, 0.14f);
    mb.set_mat(MAT_ROOF, 0, 1.0f);
    mb.set_color(roofCol);
    if (alongX) {
        float zm = (mn.z + mx.z) * 0.5f;
        vec3 a(mn.x - o, y0, mn.z - o), b(mx.x + o, y0, mn.z - o), c(mx.x + o, y0 + rh, zm), d(mn.x - o, y0 + rh, zm);
        mb.quad_auto(a, b, c, d);
        vec3 a2(mx.x + o, y0, mx.z + o), b2(mn.x - o, y0, mx.z + o);
        mb.quad_auto(a2, b2, d, c);
        mb.set_mat(MAT_PLASTER, 0, 1.0f);
        mb.set_color(col);
        u32 i0 = mb.vert(vec3(mn.x, y0, mn.z), vec3(-1, 0, 0), vec2(mn.z, y0));
        u32 i1 = mb.vert(vec3(mn.x, y0, mx.z), vec3(-1, 0, 0), vec2(mx.z, y0));
        u32 i2 = mb.vert(vec3(mn.x, y0 + rh, zm), vec3(-1, 0, 0), vec2(zm, y0 + rh));
        mb.tri(i0, i1, i2);
        i0 = mb.vert(vec3(mx.x, y0, mx.z), vec3(1, 0, 0), vec2(mx.z, y0));
        i1 = mb.vert(vec3(mx.x, y0, mn.z), vec3(1, 0, 0), vec2(mn.z, y0));
        i2 = mb.vert(vec3(mx.x, y0 + rh, zm), vec3(1, 0, 0), vec2(zm, y0 + rh));
        mb.tri(i0, i1, i2);
    } else {
        float xm = (mn.x + mx.x) * 0.5f;
        vec3 a(mx.x + o, y0, mn.z - o), b(mx.x + o, y0, mx.z + o), c(xm, y0 + rh, mx.z + o), d(xm, y0 + rh, mn.z - o);
        mb.quad_auto(a, b, c, d);
        vec3 a2(mn.x - o, y0, mx.z + o), b2(mn.x - o, y0, mn.z - o);
        mb.quad_auto(a2, b2, d, c);
        mb.set_mat(MAT_PLASTER, 0, 1.0f);
        mb.set_color(col);
        u32 i0 = mb.vert(vec3(mx.x, y0, mn.z), vec3(0, 0, -1), vec2(mx.x, y0));
        u32 i1 = mb.vert(vec3(mn.x, y0, mn.z), vec3(0, 0, -1), vec2(mn.x, y0));
        u32 i2 = mb.vert(vec3(xm, y0 + rh, mn.z), vec3(0, 0, -1), vec2(xm, y0 + rh));
        mb.tri(i0, i1, i2);
        i0 = mb.vert(vec3(mn.x, y0, mx.z), vec3(0, 0, 1), vec2(mn.x, y0));
        i1 = mb.vert(vec3(mx.x, y0, mx.z), vec3(0, 0, 1), vec2(mx.x, y0));
        i2 = mb.vert(vec3(xm, y0 + rh, mx.z), vec3(0, 0, 1), vec2(xm, y0 + rh));
        mb.tri(i0, i1, i2);
    }
    // Porch light
    vec3 door((mn.x + mx.x) * 0.5f, 2.6f, mx.z + 0.5f);
    gen_light(door, 8.0f, vec3(1.0f, 0.7f, 0.4f), 5.0f);
    gen_box(door - vec3(0.12f, 0.12f, 0.1f), door + vec3(0.12f, 0.12f, 0.0f), MAT_PAINT, vec3(1.0f, 0.75f, 0.45f), MF_EMISSIVE, 1.0f, 15.0f);
}

void warehouse(vec3 mn, vec3 mx, Rng& rng, float h) {
    vec3 cols[] = {vec3(0.6f, 0.62f, 0.64f), vec3(0.45f, 0.55f, 0.65f), vec3(0.62f, 0.38f, 0.3f), vec3(0.45f, 0.55f, 0.45f), vec3(0.72f, 0.7f, 0.6f)};
    vec3 col = cols[rng.irange(0, 4)];
    gen_box(vec3(mn.x, CURB_H, mn.z), vec3(mx.x, h, mx.z), MAT_CORRUGATED, col, MF_METAL, 1.0f, 0, false);
    gen_box(vec3(mn.x - 0.3f, h, mn.z - 0.3f), vec3(mx.x + 0.3f, h + 0.4f, mx.z + 0.3f), MAT_ROOF, vec3(0.4f, 0.4f, 0.42f), 0, 1.0f, 0, true, true);
    world_add_box(vec3(mn.x, 0, mn.z), vec3(mx.x, h + 0.4f, mx.z), SURF_METAL, 1);
    // Roll-up doors and lamps on the long sides
    float len = mx.x - mn.x;
    int doors = max_t(1, (int)(len / 16.0f));
    for (int d = 0; d < doors; d++) {
        float x = mn.x + len * (d + 0.5f) / doors;
        for (int s = 0; s < 2; s++) {
            float z = s == 0 ? mn.z - 0.05f : mx.z + 0.05f;
            vec3 n(0, 0, s == 0 ? -1.0f : 1.0f);
            gen_box(vec3(x - 2.5f, CURB_H, fminf(z, z + n.z * 0.1f)), vec3(x + 2.5f, 5.0f, fmaxf(z, z + n.z * 0.1f)), MAT_STEEL, vec3(0.35f, 0.36f, 0.38f), MF_METAL, 1.4f);
            vec3 lamp(x, 6.0f, z + n.z * 0.4f);
            gen_box(lamp - vec3(0.3f, 0.1f, 0.2f), lamp + vec3(0.3f, 0.1f, 0.2f), MAT_PAINT, vec3(1.0f, 0.8f, 0.55f), MF_EMISSIVE, 1.0f, 18.0f);
            if (rng.chance(0.7f)) {
                gen_light(lamp + n * 1.0f - vec3(0, 0.5f, 0), 15.0f, vec3(1.0f, 0.75f, 0.45f), 18.0f);
                gen_beam(lamp, lamp + n * 2.5f + vec3(0, -5.5f, 0), 0.3f, 2.5f, vec3(1.0f, 0.8f, 0.5f), 0.12f);
            }
        }
    }
}

void chainlink(vec3 a, vec3 b, float h) {
    vec3 d = b - a;
    float len = length(d);
    int posts = max_t(1, (int)(len / 3.0f));
    for (int i = 0; i <= posts; i++) {
        vec3 p = a + d * ((float)i / posts);
        gen_tube(p, p + vec3(0, h, 0), 0.04f, 4, MAT_STEEL, vec3(0.4f, 0.42f, 0.43f), MF_METAL);
    }
    gen_tube(a + vec3(0, h, 0), b + vec3(0, h, 0), 0.03f, 4, MAT_STEEL, vec3(0.4f, 0.42f, 0.43f), MF_METAL);
    // Mesh panel (dark, thin)
    gen_quad(a + vec3(0, 0.05f, 0), b + vec3(0, 0.05f, 0), b + vec3(0, h, 0), a + vec3(0, h, 0), MAT_STEEL, vec3(0.18f, 0.19f, 0.2f), MF_METAL, 2.0f);
    gen_quad(b + vec3(0, 0.05f, 0), a + vec3(0, 0.05f, 0), a + vec3(0, h, 0), b + vec3(0, h, 0), MAT_STEEL, vec3(0.18f, 0.19f, 0.2f), MF_METAL, 2.0f);
    vec3 mn = vmin(a, b) - vec3(0.08f, 0, 0.08f), mx = vmax(a, b) + vec3(0.08f, h, 0.08f);
    world_add_box(mn, mx, SURF_METAL, 0);
}

// ---------------------------------------------------------------------------
// Block generators per district
// ---------------------------------------------------------------------------
static void gen_block_base(int i, int j, int d) {
    float x0 = block_min(i), x1 = block_max(i), z0 = block_min(j), z1 = block_max(j);
    // Raised block slab (sidewalk ring + lot)
    MeshBuilder& mb = gen_chunk(vec3((x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f));
    mb.set_mat(MAT_CONCRETE, 0, 1.0f);
    mb.set_color(vec3(0.6f, 0.6f, 0.58f));
    mb.box(vec3(x0, -0.2f, z0), vec3(x1, CURB_H, z1), false, false);
    float s = SIDEWALK_W;
    mb.set_mat(MAT_SIDEWALK, MF_GROUND, 1.0f);
    mb.set_color(vec3(0.82f, 0.82f, 0.8f));
    float y = CURB_H;
    mb.quad(vec3(x0, y, z0 + s), vec3(x1, y, z0 + s), vec3(x1, y, z0), vec3(x0, y, z0), vec2(x0, z0 + s), vec2(x1, z0 + s), vec2(x1, z0), vec2(x0, z0));
    mb.quad(vec3(x0, y, z1), vec3(x1, y, z1), vec3(x1, y, z1 - s), vec3(x0, y, z1 - s), vec2(x0, z1), vec2(x1, z1), vec2(x1, z1 - s), vec2(x0, z1 - s));
    mb.quad(vec3(x0, y, z1 - s), vec3(x0 + s, y, z1 - s), vec3(x0 + s, y, z0 + s), vec3(x0, y, z0 + s), vec2(x0, z1 - s), vec2(x0 + s, z1 - s), vec2(x0 + s, z0 + s), vec2(x0, z0 + s));
    mb.quad(vec3(x1 - s, y, z1 - s), vec3(x1, y, z1 - s), vec3(x1, y, z0 + s), vec3(x1 - s, y, z0 + s), vec2(x1 - s, z1 - s), vec2(x1, z1 - s), vec2(x1, z0 + s), vec2(x1 - s, z0 + s));
    int lotLayer = MAT_SIDEWALK;
    vec3 lotCol(0.7f, 0.7f, 0.7f);
    if (d == D_WESTHILLS) { lotLayer = MAT_GRASS; lotCol = vec3(1, 1, 1); }
    else if (d == D_IRONWORKS) { lotLayer = MAT_ASPHALT; lotCol = vec3(1.05f, 1.05f, 1.05f); }
    else if (d == D_BLOCKS) { lotLayer = MAT_FLOOR_CONCRETE; lotCol = vec3(0.8f, 0.8f, 0.8f); }
    else if (d == D_DOWNTOWN) { lotLayer = MAT_TILES; lotCol = vec3(0.55f, 0.55f, 0.58f); }
    mb.set_mat(lotLayer, MF_GROUND, 1.0f);
    mb.set_color(lotCol);
    mb.quad(vec3(x0 + s, y, z1 - s), vec3(x1 - s, y, z1 - s), vec3(x1 - s, y, z0 + s), vec3(x0 + s, y, z0 + s), vec2(x0 + s, z1 - s), vec2(x1 - s, z1 - s), vec2(x1 - s, z0 + s), vec2(x0 + s, z0 + s));
    world_add_box(vec3(x0, -0.2f, z0), vec3(x1, CURB_H, z1), SURF_CONCRETE, 2);

    // Walk nodes: sidewalk corners and edge midpoints
    float m = 2.0f;
    vec3 pts[8] = {vec3(x0 + m, y, z0 + m), vec3((x0 + x1) * 0.5f, y, z0 + m), vec3(x1 - m, y, z0 + m), vec3(x1 - m, y, (z0 + z1) * 0.5f),
                   vec3(x1 - m, y, z1 - m), vec3((x0 + x1) * 0.5f, y, z1 - m), vec3(x0 + m, y, z1 - m), vec3(x0 + m, y, (z0 + z1) * 0.5f)};
    int base = g_world.walkNodes.count;
    for (int k = 0; k < 8; k++) {
        NavNode n;
        memset(&n, 0, sizeof(n));
        n.pos = pts[k];
        n.district = (u8)d;
        n.links[n.linkCount++] = base + (k + 1) % 8;
        n.links[n.linkCount++] = base + (k + 7) % 8;
        g_world.walkNodes.push(n);
    }
}

static void link_walk(int a, int b) {
    NavNode& na = g_world.walkNodes[a];
    NavNode& nb = g_world.walkNodes[b];
    if (na.linkCount < 6) na.links[na.linkCount++] = b;
    if (nb.linkCount < 6) nb.links[nb.linkCount++] = a;
}

static void gen_downtown(int i, int j, Rng& rng) {
    float x0 = block_min(i) + SIDEWALK_W, x1 = block_max(i) - SIDEWALK_W;
    float z0 = block_min(j) + SIDEWALK_W, z1 = block_max(j) - SIDEWALK_W;
    float cdist = length(vec3(block_center(i, j).x - 30, 0, block_center(i, j).z + 60));
    float hScale = clampf(1.6f - cdist / 300.0f, 0.5f, 1.4f);
    int layout = rng.irange(0, 2);
    if (layout == 0) {
        tower(vec3(x0 + 3, 0, z0 + 3), vec3(x1 - 3, 0, z1 - 3), rng.range(90, 190) * hScale, rng);
    } else {
        float mx = (x0 + x1) * 0.5f + rng.range(-6, 6);
        tower(vec3(x0 + 2, 0, z0 + 2), vec3(mx - 3, 0, z1 - 2), rng.range(60, 150) * hScale, rng);
        float mz = (z0 + z1) * 0.5f;
        tower(vec3(mx + 3, 0, z0 + 2), vec3(x1 - 2, 0, mz - 3), rng.range(50, 120) * hScale, rng);
        tower(vec3(mx + 3, 0, mz + 3), vec3(x1 - 2, 0, z1 - 2), rng.range(40, 100) * hScale, rng);
    }
    // Street trees with planters along the sidewalk
    for (int k = 0; k < 4; k++) {
        float t = (k + 0.5f) / 4.0f;
        if (rng.chance(0.6f)) gen_tree(vec3(lerpf(x0, x1, t), CURB_H, z0 - 2.5f), 0.8f, rng.next());
        if (rng.chance(0.6f)) gen_tree(vec3(lerpf(x0, x1, t), CURB_H, z1 + 2.5f), 0.8f, rng.next());
    }
}

static void gen_midtown(int i, int j, Rng& rng) {
    float x0 = block_min(i) + SIDEWALK_W, x1 = block_max(i) - SIDEWALK_W;
    float z0 = block_min(j) + SIDEWALK_W, z1 = block_max(j) - SIDEWALK_W;
    // Two rows of mid-rise buildings facing north and south streets
    for (int row = 0; row < 2; row++) {
        float zA = row == 0 ? z0 : z1 - rng.range(22, 30);
        float zB = row == 0 ? z0 + rng.range(22, 30) : z1;
        float x = x0;
        while (x < x1 - 8) {
            float w = rng.range(14, 28);
            if (x + w > x1 - 6) w = x1 - x;
            float h = rng.range(14, 44);
            int layer = rng.chance(0.5f) ? MAT_BRICK : MAT_CONCRETE;
            vec3 col = layer == MAT_BRICK ? vec3(1, 1, 1) * rng.range(0.7f, 1.0f) : vec3(0.75f, 0.73f, 0.7f) * rng.range(0.8f, 1.1f);
            vec3 mn(x, CURB_H, zA), mx(x + w - 0.5f, h, zB);
            building(mn, mx, layer, col, rng, true, rng.chance(0.3f));
            storefront(mn, mx, row == 0 ? 0 : 1, rng, rng.chance(0.55f));
            x += w;
        }
    }
    // Parking lot in the middle with a couple of trees
    float zm = (z0 + z1) * 0.5f;
    gen_tree(vec3(x0 + 10, CURB_H, zm), 1.0f, rng.next());
    gen_tree(vec3(x1 - 10, CURB_H, zm), 1.0f, rng.next());
    vec4 spot(x0 + 25, CURB_H, zm, 0);
    g_world.parkingSpots.push(spot);
}

void gen_blocks(int i, int j, Rng& rng, int sideMask) {
    float x0 = block_min(i) + SIDEWALK_W, x1 = block_max(i) - SIDEWALK_W;
    float z0 = block_min(j) + SIDEWALK_W, z1 = block_max(j) - SIDEWALK_W;
    float depth = 14.0f;
    // Perimeter rows on the four sides, leaving an alley gap on each
    for (int side = 0; side < 4; side++) {
        if (!(sideMask & (1 << side))) continue;
        bool alongX = side < 2;
        float a0 = alongX ? x0 : z0 + depth, a1 = alongX ? x1 : z1 - depth;
        float gapC = lerpf(a0, a1, rng.range(0.3f, 0.7f));
        float a = a0;
        while (a < a1 - 4) {
            float w = rng.range(9, 18);
            if (a < gapC && a + w > gapC - 2.5f) w = fmaxf(gapC - 2.5f - a, 3.0f);
            if (a + w > a1) w = a1 - a;
            if (w < 3.0f) { a = gapC + 2.5f; continue; }
            float h = rng.range(9, 22);
            vec3 mn, mx;
            if (side == 0) { mn = vec3(a, CURB_H, z0); mx = vec3(a + w - 0.3f, h, z0 + depth); }
            else if (side == 1) { mn = vec3(a, CURB_H, z1 - depth); mx = vec3(a + w - 0.3f, h, z1); }
            else if (side == 2) { mn = vec3(x0, CURB_H, a); mx = vec3(x0 + depth, h, a + w - 0.3f); }
            else { mn = vec3(x1 - depth, CURB_H, a); mx = vec3(x1, h, a + w - 0.3f); }
            vec3 brickCol = vec3(1, 1, 1) * rng.range(0.55f, 1.0f);
            if (rng.chance(0.25f)) brickCol = vec3(0.75f, 0.8f, 0.85f) * rng.range(0.5f, 0.8f);
            building(mn, mx, rng.chance(0.75f) ? MAT_BRICK : MAT_CONCRETE, brickCol, rng, true, true);
            storefront(mn, mx, side, rng, rng.chance(0.6f));
            if (rng.chance(0.4f) && w > 8) {
                int floors = (int)((h - CURB_H) / 3.4f);
                vec3 o, rr, n;
                if (side == 0) { o = vec3(mn.x + 1, 0, mn.z); rr = vec3(1, 0, 0); n = vec3(0, 0, -1); }
                else if (side == 1) { o = vec3(mn.x + 1, 0, mx.z); rr = vec3(1, 0, 0); n = vec3(0, 0, 1); }
                else if (side == 2) { o = vec3(mn.x, 0, mn.z + 1); rr = vec3(0, 0, 1); n = vec3(-1, 0, 0); }
                else { o = vec3(mx.x, 0, mn.z + 1); rr = vec3(0, 0, 1); n = vec3(1, 0, 0); }
                fire_escape(o, rr, n, fminf(w - 2, 6.0f), floors);
            }
            a += w;
            if (a > gapC - 3 && a < gapC + 3) a = gapC + 2.5f;
        }
    }
    // Courtyard: dumpsters, trash, a flickering light
    vec3 c = block_center(i, j);
    for (int k = 0; k < 3; k++) {
        vec3 p = c + vec3(rng.range(-20, 20), CURB_H, rng.range(-20, 20));
        vec3 dc = rng.chance(0.5f) ? vec3(0.15f, 0.35f, 0.2f) : vec3(0.2f, 0.25f, 0.4f);
        gen_solid(p - vec3(1.0f, 0, 0.6f), p + vec3(1.0f, 1.3f, 0.6f), MAT_METAL_PANEL, dc, SURF_METAL);
        gen_box(p + vec3(-1.05f, 1.3f, -0.65f), p + vec3(1.05f, 1.38f, 0.65f), MAT_RUBBER, vec3(0.3f, 0.3f, 0.3f));
    }
    gen_light(c + vec3(0, 4.0f, 0), 18.0f, vec3(1.0f, 0.55f, 0.25f), 14.0f);
    g_world.cacheSpots.push(c + vec3(rng.range(-15, 15), CURB_H, rng.range(-15, 15)));
}

static void gen_ironworks(int i, int j, Rng& rng) {
    float x0 = block_min(i) + SIDEWALK_W, x1 = block_max(i) - SIDEWALK_W;
    float z0 = block_min(j) + SIDEWALK_W, z1 = block_max(j) - SIDEWALK_W;
    float split = (z0 + z1) * 0.5f + rng.range(-8, 8);
    warehouse(vec3(x0 + 3, 0, z0 + 3), vec3(x1 - 3, 0, split - 6), rng, rng.range(9, 14));
    // Yard with containers or a factory chimney
    if (rng.chance(0.4f)) {
        vec3 c((x0 + x1) * 0.5f + rng.range(-15, 15), CURB_H, (split + z1) * 0.5f);
        gen_cyl(c, 2.2f, rng.range(28, 45), 12, MAT_BRICK, vec3(0.6f, 0.55f, 0.5f), 0, 1.0f, 0, true);
        gen_box(c + vec3(-6, 0, -5), c + vec3(6, 8, 5), MAT_CORRUGATED, vec3(0.5f, 0.45f, 0.4f), MF_METAL);
        world_add_box(c + vec3(-6, 0, -5), c + vec3(6, 8, 5), SURF_METAL, 1);
    } else {
        vec3 cols[] = {vec3(0.6f, 0.15f, 0.1f), vec3(0.15f, 0.3f, 0.55f), vec3(0.2f, 0.45f, 0.25f), vec3(0.75f, 0.45f, 0.1f), vec3(0.5f, 0.5f, 0.5f)};
        for (int k = 0; k < 5; k++) {
            float x = x0 + 8 + k * 14.0f;
            if (x > x1 - 8) break;
            float z = split + 10 + rng.range(0, 10);
            int stack = rng.irange(1, 3);
            for (int s = 0; s < stack; s++) gen_container(vec3(x, CURB_H + s * 2.6f, z), 1.0f, cols[rng.irange(0, 4)]);
        }
    }
    for (int k = 0; k < 4; k++) gen_crate(vec3(rng.range(x0 + 4, x1 - 4), CURB_H, rng.range(split + 2, z1 - 4)), vec3(1.2f, 1.0f, 1.2f), rng.next());
    chainlink(vec3(x0 + 1, CURB_H, z1 - 1), vec3(x0 + 30, CURB_H, z1 - 1), 2.5f);
    chainlink(vec3(x1 - 30, CURB_H, z1 - 1), vec3(x1 - 1, CURB_H, z1 - 1), 2.5f);
    // Overhead yard light
    vec3 lp((x0 + x1) * 0.5f, 9.0f, (split + z1) * 0.5f);
    gen_tube(vec3(lp.x, CURB_H, lp.z), lp, 0.15f, 6, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
    gen_box(lp - vec3(0.6f, 0.15f, 0.3f), lp + vec3(0.6f, 0.15f, 0.3f), MAT_PAINT, vec3(1.0f, 0.85f, 0.6f), MF_EMISSIVE, 1.0f, 20.0f);
    gen_light(lp - vec3(0, 0.5f, 0), 26.0f, vec3(1.0f, 0.75f, 0.45f), 30.0f);
    gen_beam(lp, vec3(lp.x, 0.3f, lp.z), 0.4f, 5.0f, vec3(1.0f, 0.8f, 0.5f), 0.12f);
    world_add_box(vec3(lp.x - 0.2f, 0, lp.z - 0.2f), vec3(lp.x + 0.2f, 9, lp.z + 0.2f), SURF_METAL, 1);
    g_world.steamVents.push(vec3((x0 + x1) * 0.5f + 10, CURB_H, split + 4));
}

static void gen_westhills(int i, int j, Rng& rng) {
    float x0 = block_min(i) + SIDEWALK_W, x1 = block_max(i) - SIDEWALK_W;
    float z0 = block_min(j) + SIDEWALK_W, z1 = block_max(j) - SIDEWALK_W;
    for (int row = 0; row < 2; row++)
        for (int col = 0; col < 2; col++) {
            float cx = lerpf(x0, x1, col == 0 ? 0.28f : 0.72f);
            float cz = lerpf(z0, z1, row == 0 ? 0.28f : 0.72f);
            float w = rng.range(12, 18), d = rng.range(10, 15);
            house(vec3(cx - w * 0.5f, 0, cz - d * 0.5f), vec3(cx + w * 0.5f, 0, cz + d * 0.5f), rng, false);
            // Driveway
            float dz = row == 0 ? z0 : z1;
            gen_box(vec3(cx + w * 0.5f - 4, CURB_H, fminf(cz, dz)), vec3(cx + w * 0.5f - 0.5f, CURB_H + 0.02f, fmaxf(cz, dz)), MAT_FLOOR_CONCRETE, vec3(0.8f, 0.8f, 0.8f), MF_GROUND);
            for (int t = 0; t < 2; t++) gen_tree(vec3(cx + rng.range(-w, w) * 0.7f, CURB_H, cz + (row == 0 ? -1.0f : 1.0f) * (d * 0.5f + 4)), rng.range(0.9f, 1.4f), rng.next());
            // Hedge
            if (rng.chance(0.6f)) {
                float hz = row == 0 ? z0 + 1.0f : z1 - 1.0f;
                gen_box(vec3(cx - w * 0.5f, CURB_H, hz - 0.5f), vec3(cx + w * 0.5f - 5, CURB_H + 1.2f, hz + 0.5f), MAT_GRASS, vec3(0.3f, 0.45f, 0.25f), MF_FOLIAGE);
                world_add_box(vec3(cx - w * 0.5f, 0, hz - 0.5f), vec3(cx + w * 0.5f - 5, CURB_H + 1.2f, hz + 0.5f), SURF_DIRT, 1);
            }
            if (rng.chance(0.25f)) {
                // Swimming pool with an underwater light
                vec3 pc(cx - w * 0.2f, CURB_H + 0.02f, cz + (row == 0 ? 1.0f : -1.0f) * (d * 0.5f + 2) * -1.0f);
                (void)pc;
            }
        }
    g_world.cacheSpots.push(vec3((x0 + x1) * 0.5f, CURB_H, (z0 + z1) * 0.5f));
}

// ---------------------------------------------------------------------------
// Roads
// ---------------------------------------------------------------------------
static void road_markings(Rng& rng) {
    vec3 white(0.85f, 0.85f, 0.82f), yellow(0.9f, 0.68f, 0.12f);
    float y = 0.035f;
    for (int k = 0; k <= CITY_BLOCKS; k++) {
        float rx = road_x(k);
        for (int seg = 0; seg < CITY_BLOCKS; seg++) {
            float s0 = block_min(seg), s1 = block_max(seg);
            for (int axis = 0; axis < 2; axis++) {
                // axis 0: road along Z at x = rx; axis 1: road along X at z = rx
                auto Q = [&](float c0, float c1, float a0, float a1, vec3 col) {
                    vec3 p0, p1, p2, p3;
                    if (axis == 0) { p0 = vec3(rx + c0, y, a1); p1 = vec3(rx + c1, y, a1); p2 = vec3(rx + c1, y, a0); p3 = vec3(rx + c0, y, a0); }
                    else { p0 = vec3(a0, y, rx + c1); p1 = vec3(a1, y, rx + c1); p2 = vec3(a1, y, rx + c0); p3 = vec3(a0, y, rx + c0); }
                    MeshBuilder& mb = gen_chunk((p0 + p2) * 0.5f);
                    mb.set_mat(MAT_PAINT, MF_GROUND, 1.0f);
                    mb.set_color(col);
                    mb.quad(p0, p1, p2, p3, vec2(p0.x, p0.z), vec2(p1.x, p1.z), vec2(p2.x, p2.z), vec2(p3.x, p3.z));
                };
                float a0 = s0 + 4.0f, a1 = s1 - 4.0f;
                Q(-0.22f, -0.1f, a0, a1, yellow);
                Q(0.1f, 0.22f, a0, a1, yellow);
                for (float a = a0; a < a1 - 3; a += 9.0f) {
                    Q(-3.6f, -3.45f, a, a + 3.0f, white);
                    Q(3.45f, 3.6f, a, a + 3.0f, white);
                }
                // Crosswalks at both ends
                for (int e = 0; e < 2; e++) {
                    float c0 = e == 0 ? s0 + 0.3f : s1 - 3.3f;
                    for (float c = -6.5f; c < 6.5f; c += 1.2f) Q(c, c + 0.6f, c0, c0 + 3.0f, white);
                    float stop = e == 0 ? s0 + 3.8f : s1 - 4.3f;
                    if (e == 0) Q(-6.8f, -0.3f, stop, stop + 0.4f, white);
                    else Q(0.3f, 6.8f, stop, stop + 0.4f, white);
                }
                // Manhole covers
                if (rng.chance(0.5f)) {
                    float a = lerpf(a0, a1, rng.range(0.2f, 0.8f));
                    float c = rng.chance(0.5f) ? 3.5f : -3.5f;
                    vec3 p = axis == 0 ? vec3(rx + c, 0.02f, a) : vec3(a, 0.02f, rx + c);
                    MeshBuilder& mb = gen_chunk(p);
                    mb.set_mat(MAT_RUST, MF_METAL | MF_GROUND, 1.0f);
                    mb.set_color(vec3(0.5f, 0.5f, 0.5f));
                    mb.cylinder(p, 0.55f, 0.02f, 12, true);
                    if (rng.chance(0.4f)) g_world.steamVents.push(p);
                }
            }
        }
    }
}

static void gen_ground() {
    // Asphalt covers the road grid; outskirts are dirt / grass.
    for (int cz = 0; cz < GEN_CHUNKS; cz++)
        for (int cx = 0; cx < GEN_CHUNKS; cx++) {
            float x0 = GEN_ORIGIN + cx * GEN_CHUNK, z0 = GEN_ORIGIN + cz * GEN_CHUNK;
            float x1 = x0 + GEN_CHUNK, z1 = z0 + GEN_CHUNK;
            if (x1 < -WORLD_LIMIT - 130 || x0 > WORLD_LIMIT + 130 || z1 < -WORLD_LIMIT - 130 || z0 > WORLD_LIMIT + 130) continue;
            float cxm = (x0 + x1) * 0.5f, czm = (z0 + z1) * 0.5f;
            bool inCity = fabsf(cxm) < CITY_HALF + ROAD_W && fabsf(czm) < CITY_HALF + ROAD_W;
            MeshBuilder& mb = gen_chunk(vec3(cxm, 0, czm));
            mb.set_mat(inCity ? MAT_ASPHALT : MAT_DIRT, MF_GROUND, 1.0f);
            mb.set_color(inCity ? vec3(1, 1, 1) : vec3(0.75f, 0.72f, 0.68f));
            mb.quad(vec3(x0, 0, z1), vec3(x1, 0, z1), vec3(x1, 0, z0), vec3(x0, 0, z0), vec2(x0, z1), vec2(x1, z1), vec2(x1, z0), vec2(x0, z0));
        }
    // Perimeter barrier (concrete jersey wall + fence) bounding the playable city
    float L = CITY_HALF + ROAD_W * 0.5f + 6.0f;
    for (int side = 0; side < 4; side++) {
        vec3 mn, mx;
        if (side == 0) { mn = vec3(-L, 0, -L - 1); mx = vec3(L, 1.1f, -L); }
        else if (side == 1) { mn = vec3(-L, 0, L); mx = vec3(L, 1.1f, L + 1); }
        else if (side == 2) { mn = vec3(-L - 1, 0, -L); mx = vec3(-L, 1.1f, L); }
        else { mn = vec3(L, 0, -L); mx = vec3(L + 1, 1.1f, L); }
        // Split into segments for chunking
        int segs = 16;
        for (int s = 0; s < segs; s++) {
            vec3 a = mn, b = mx;
            if (side < 2) { a.x = lerpf(mn.x, mx.x, (float)s / segs); b.x = lerpf(mn.x, mx.x, (float)(s + 1) / segs); }
            else { a.z = lerpf(mn.z, mx.z, (float)s / segs); b.z = lerpf(mn.z, mx.z, (float)(s + 1) / segs); }
            gen_box(a, b, MAT_CONCRETE, vec3(0.7f, 0.7f, 0.68f));
            // Tall fence behind
            vec3 fa = a, fb = b;
            if (side == 0) { fa.z -= 0.5f; fb.z = fa.z + 0.05f; }
            else if (side == 1) { fa.z = b.z + 0.45f; fb.z = fa.z + 0.05f; }
            else if (side == 2) { fa.x -= 0.5f; fb.x = fa.x + 0.05f; }
            else { fa.x = b.x + 0.45f; fb.x = fa.x + 0.05f; }
            fa.y = 0; fb.y = 4.0f;
            gen_box(fa, fb, MAT_STEEL, vec3(0.15f, 0.16f, 0.17f), MF_METAL, 2.0f);
        }
        world_add_box(mn - vec3(0, 0, 0), mx + vec3(0, 6.0f, 0), SURF_CONCRETE, 0);
    }
    // Distant industrial silhouettes beyond the fence
    Rng rng;
    rng.seed(77);
    for (int k = 0; k < 40; k++) {
        float a = TAU_F * k / 40.0f;
        float dist = rng.range(560, 680);
        vec3 p(sinf(a) * dist, 0, cosf(a) * dist);
        if (fabsf(p.x - AEGIS_ORIGIN_X) < 120 && fabsf(p.z - AEGIS_ORIGIN_Z) < 120) continue;
        float h = rng.range(20, 90);
        float w = rng.range(15, 40);
        gen_box(p - vec3(w, 0, w), p + vec3(w, h, w), MAT_CONCRETE, vec3(0.3f, 0.3f, 0.32f), MF_WINDOWS, 1.0f, 0);
    }
}

static void gen_streetlights() {
    for (int k = 0; k <= CITY_BLOCKS; k++) {
        float rx = road_x(k);
        for (int seg = 0; seg < CITY_BLOCKS; seg++) {
            float s0 = block_min(seg), s1 = block_max(seg);
            for (int axis = 0; axis < 2; axis++) {
                int n = 3;
                for (int q = 0; q < n; q++) {
                    float a = lerpf(s0 + 12, s1 - 12, (q + 0.5f) / n);
                    int side = ((q + seg + k) & 1) ? 1 : -1;
                    float off = ROAD_W * 0.5f + 0.7f;
                    vec3 base, arm;
                    if (axis == 0) { base = vec3(rx + side * off, CURB_H, a); arm = vec3((float)-side, 0, 0); }
                    else { base = vec3(a, CURB_H, rx + side * off); arm = vec3(0, 0, (float)-side); }
                    if (fabsf(base.x) > CITY_HALF + 2 || fabsf(base.z) > CITY_HALF + 2) {
                        // Outer ring road: only place lamps on the city side
                        if ((axis == 0 && fabsf(base.x) > CITY_HALF) || (axis == 1 && fabsf(base.z) > CITY_HALF)) continue;
                    }
                    int i, j;
                    block_of(base, &i, &j);
                    int d = block_district(clamp_t(i, 0, 8), clamp_t(j, 0, 8));
                    gen_streetlight(base, arm, d == D_DOWNTOWN || d == D_MIDTOWN);
                }
            }
        }
    }
    // Traffic light poles at intersections (signal lamps are drawn dynamically)
    for (int kx = 1; kx < CITY_BLOCKS; kx++)
        for (int kz = 1; kz < CITY_BLOCKS; kz++) {
            vec3 c(road_x(kx), CURB_H, road_x(kz));
            for (int q = 0; q < 4; q++) {
                vec3 o((q & 1) ? 7.6f : -7.6f, 0, (q & 2) ? 7.6f : -7.6f);
                gen_tube(c + o, c + o + vec3(0, 4.2f, 0), 0.09f, 5, MAT_STEEL, vec3(0.18f, 0.18f, 0.19f), MF_METAL);
                gen_box(c + o + vec3(-0.18f, 3.2f, -0.18f), c + o + vec3(0.18f, 4.3f, 0.18f), MAT_PAINT, vec3(0.12f, 0.12f, 0.1f));
                world_add_box(c + o - vec3(0.12f, 0, 0.12f), c + o + vec3(0.12f, 4.2f, 0.12f), SURF_METAL, 1);
            }
        }
}

static void gen_props(Rng& rng) {
    // Sidewalk furniture: hydrants, trash cans, benches, newspaper boxes
    for (int i = 0; i < CITY_BLOCKS; i++)
        for (int j = 0; j < CITY_BLOCKS; j++) {
            if (gen_is_landmark_block(i, j)) continue;
            int d = block_district(i, j);
            if (d == D_WESTHILLS) continue;
            float x0 = block_min(i), x1 = block_max(i), z0 = block_min(j), z1 = block_max(j);
            for (int k = 0; k < 4; k++) {
                float t = rng.range(0.2f, 0.8f);
                vec3 p;
                switch (k) {
                case 0: p = vec3(lerpf(x0, x1, t), CURB_H, z0 + 0.8f); break;
                case 1: p = vec3(lerpf(x0, x1, t), CURB_H, z1 - 0.8f); break;
                case 2: p = vec3(x0 + 0.8f, CURB_H, lerpf(z0, z1, t)); break;
                default: p = vec3(x1 - 0.8f, CURB_H, lerpf(z0, z1, t)); break;
                }
                int type = rng.irange(0, 3);
                if (type == 0) {
                    gen_cyl(p, 0.18f, 0.7f, 8, MAT_PAINT, vec3(0.75f, 0.12f, 0.08f), MF_METAL, 0.8f, 0, true);
                } else if (type == 1) {
                    gen_cyl(p, 0.32f, 1.0f, 10, MAT_METAL_PANEL, vec3(0.2f, 0.25f, 0.22f), MF_METAL, 1.0f, 0, true);
                } else if (type == 2) {
                    bool alongX = k < 2;
                    vec3 h = alongX ? vec3(0.9f, 0, 0.25f) : vec3(0.25f, 0, 0.9f);
                    gen_box(p - h + vec3(0, 0.4f, 0), p + h + vec3(0, 0.5f, 0), MAT_WOOD, vec3(0.6f, 0.45f, 0.3f));
                    gen_box(p - h + vec3(0, 0, 0), p - h * 0.8f + vec3(0, 0.4f, 0), MAT_STEEL, vec3(0.2f, 0.2f, 0.2f), MF_METAL);
                    world_add_box(p - h, p + h + vec3(0, 0.5f, 0), SURF_WOOD, 1);
                } else {
                    vec3 col = g_neonColors[rng.irange(0, 6)] * 0.6f;
                    gen_solid(p - vec3(0.25f, 0, 0.25f), p + vec3(0.25f, 1.1f, 0.25f), MAT_METAL_PANEL, col, SURF_METAL);
                }
            }
            // Parking spots along curbs (used for parked cars)
            for (int k = 0; k < 2; k++) {
                float t = rng.range(0.25f, 0.75f);
                if (rng.chance(0.5f)) {
                    vec4 s(lerpf(x0, x1, t), 0, z0 - 2.2f, PI_F * 0.5f);
                    g_world.parkingSpots.push(s);
                } else {
                    vec4 s(x0 - 2.2f, 0, lerpf(z0, z1, t), 0);
                    g_world.parkingSpots.push(s);
                }
            }
        }
}

void world_destroy() {
    World& w = g_world;
    w.boxes.free_mem();
    w.cellStart.free_mem();
    w.cellItems.free_mem();
    w.walkNodes.free_mem();
    w.indoors.free_mem();
    w.parkingSpots.free_mem();
    w.cacheSpots.free_mem();
    w.evidenceSpots.free_mem();
    w.streetLightIds.free_mem();
    w.streetLightOrig.free_mem();
    w.steamVents.free_mem();
    w.firePoints.free_mem();
    w.relayPoints.free_mem();
    w.neonLights.free_mem();
    renderer_clear_chunks();
    renderer_clear_static_lights();
    renderer_clear_static_beams();
}

void world_generate(u32 seed) {
    double t0 = plat_time_seconds();
    World& w = g_world;
    world_destroy();
    memset(&w, 0, sizeof(w));
    w.seed = seed;
    Rng rng;
    rng.seed(seed);
    gen_ground();
    road_markings(rng);
    gen_streetlights();
    for (int j = 0; j < CITY_BLOCKS; j++)
        for (int i = 0; i < CITY_BLOCKS; i++) {
            int d = block_district(i, j);
            gen_block_base(i, j, d);
            if (gen_is_landmark_block(i, j)) continue;
            switch (d) {
            case D_DOWNTOWN: gen_downtown(i, j, rng); break;
            case D_MIDTOWN: gen_midtown(i, j, rng); break;
            case D_BLOCKS: gen_blocks(i, j, rng, 15); break;
            case D_IRONWORKS: gen_ironworks(i, j, rng); break;
            case D_WESTHILLS: gen_westhills(i, j, rng); break;
            }
        }
    // Connect sidewalk loops across intersections (corner nodes 0,2,4,6)
    for (int j = 0; j < CITY_BLOCKS; j++)
        for (int i = 0; i < CITY_BLOCKS; i++) {
            int b = (j * CITY_BLOCKS + i) * 8;
            if (i + 1 < CITY_BLOCKS) {
                int bn = (j * CITY_BLOCKS + i + 1) * 8;
                link_walk(b + 2, bn + 0);
                link_walk(b + 4, bn + 6);
            }
            if (j + 1 < CITY_BLOCKS) {
                int bn = ((j + 1) * CITY_BLOCKS + i) * 8;
                link_walk(b + 6, bn + 0);
                link_walk(b + 4, bn + 2);
            }
        }
    gen_props(rng);
    gen_landmarks(rng);
    gen_aegis_facility();
    gen_drain_tunnel();
    gen_flush_chunks();
    world_build_grid();
    log_info("Blackridge generated in %.2fs", plat_time_seconds() - t0);
}
