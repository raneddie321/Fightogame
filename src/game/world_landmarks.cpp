// FIGHTO - hand-authored landmarks and interiors: Damian's burned home, the
// Lucky Star Diner, the Fight Pit, Halberd Shipping, the substation, the
// Nexus Logistics front, the A.E.G.I.S. underground facility and the storm drain nest.
#include "world_gen.h"

struct LandmarkBlock { int i, j; };
static const LandmarkBlock g_lmBlocks[] = {
    {1, 6}, {2, 6}, {1, 7}, {2, 7}, {0, 8}, {4, 6}, {4, 0}, {4, 3}, {5, 3},
    {7, 3}, {6, 6}, {8, 6}, {8, 8}, {8, 1}, {7, 8}, {0, 1}, {1, 2},
};

bool gen_is_landmark_block(int i, int j) {
    for (int k = 0; k < ARRAY_COUNT(g_lmBlocks); k++)
        if (g_lmBlocks[k].i == i && g_lmBlocks[k].j == j) return true;
    return false;
}

static void set_lm(int id, const char* name, vec3 pos, float radius, bool map = true) {
    Landmark& l = g_world.landmarks[id];
    l.name = name;
    l.pos = pos;
    l.radius = radius;
    l.district = district_at(pos);
    l.discovered = false;
    l.showOnMap = map;
}

vec3 landmark_pos(int id) { return g_world.landmarks[id].pos; }

struct Lot { float x0, x1, z0, z1; vec3 c; };
static Lot lot(int i, int j) {
    Lot l;
    l.x0 = block_min(i) + SIDEWALK_W;
    l.x1 = block_max(i) - SIDEWALK_W;
    l.z0 = block_min(j) + SIDEWALK_W;
    l.z1 = block_max(j) - SIDEWALK_W;
    l.c = vec3((l.x0 + l.x1) * 0.5f, CURB_H, (l.z0 + l.z1) * 0.5f);
    return l;
}


// Neon sign centred at `center` on a facade facing direction `face` (0 -Z, 1 +Z, 2 -X, 3 +X).
static void sign_text(vec3 center, int face, float h, const char* text, vec3 color, bool light, bool backing) {
    vec3 right = face == 0 ? vec3(-1, 0, 0) : face == 1 ? vec3(1, 0, 0) : face == 2 ? vec3(0, 0, 1) : vec3(0, 0, -1);
    float w = (float)strlen(text) * h * 5.2f / 6.0f - h * 1.2f / 6.0f;
    gen_neon_text(center - right * (w * 0.5f), right, vec3(0, 1, 0), h, text, color, light, backing);
}

static void ceiling_lamp(vec3 p, vec3 color, float intensity, float radius, bool beam) {
    gen_box(p - vec3(0.5f, 0.06f, 0.5f), p + vec3(0.5f, 0.06f, 0.5f), MAT_PAINT, color, MF_EMISSIVE, 1.0f, 16.0f, true, true);
    gen_light(p - vec3(0, 0.4f, 0), radius, color, intensity);
    if (beam) gen_beam(p, p - vec3(0, 4.5f, 0), 0.5f, 2.6f, color, 0.08f);
}

static void police_tape(vec3 a, vec3 b) {
    gen_tube(a, a + vec3(0, 1.0f, 0), 0.03f, 4, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
    gen_tube(b, b + vec3(0, 1.0f, 0), 0.03f, 4, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
    vec3 d = normalize(b - a);
    vec3 n = normalize(cross(d, vec3(0, 1, 0))) * 0.002f;
    vec3 a1 = a + vec3(0, 0.95f, 0), b1 = b + vec3(0, 0.95f, 0);
    gen_quad(a1 + n, b1 + n, b1 + n + vec3(0, 0.08f, 0), a1 + n + vec3(0, 0.08f, 0), MAT_PAINT, vec3(0.95f, 0.8f, 0.05f), 0, 0.8f);
    gen_quad(b1 - n, a1 - n, a1 - n + vec3(0, 0.08f, 0), b1 - n + vec3(0, 0.08f, 0), MAT_PAINT, vec3(0.95f, 0.8f, 0.05f), 0, 0.8f);
}

// ---------------------------------------------------------------------------
// The Blocks landmarks
// ---------------------------------------------------------------------------
static void lm_burned_home(Rng& rng) {
    gen_blocks(1, 6, rng, 2 | 4 | 8);
    Lot L = lot(1, 6);
    float cx = L.c.x;
    vec3 mn(cx - 7, CURB_H, L.z0 + 1), mx(cx + 7, CURB_H + 6.5f, L.z0 + 13);
    vec3 charred(0.16f, 0.14f, 0.13f);
    gen_room(mn, mx, 0.35f, MAT_BRICK, charred, MAT_FLOOR_CONCRETE, vec3(0.2f, 0.18f, 0.17f), 1, 1.6f, 2.4f, false, MAT_BRICK, vec3(0.28f, 0.22f, 0.2f), MF_WINDOWS);
    // Partial second floor and collapsed roof beams
    gen_solid(vec3(mn.x, mn.y + 3.2f, mn.z + 7), vec3(mx.x, mn.y + 3.45f, mx.z), MAT_WOOD, vec3(0.15f, 0.12f, 0.1f), SURF_WOOD);
    gen_tube(vec3(mn.x + 1, mn.y + 6.0f, mn.z + 2), vec3(mx.x - 2, mn.y + 0.3f, mn.z + 9), 0.18f, 5, MAT_WOOD, vec3(0.08f, 0.07f, 0.06f));
    gen_tube(vec3(mx.x - 1, mn.y + 6.2f, mn.z + 3), vec3(mn.x + 3, mn.y + 0.2f, mx.z - 2), 0.15f, 5, MAT_WOOD, vec3(0.08f, 0.07f, 0.06f));
    for (int k = 0; k < 7; k++) {
        vec3 p(rng.range(mn.x + 1.5f, mx.x - 1.5f), mn.y, rng.range(mn.z + 2, mx.z - 1.5f));
        vec3 s(rng.range(0.4f, 1.4f), rng.range(0.2f, 0.7f), rng.range(0.4f, 1.4f));
        gen_box(p - vec3(s.x, 0, s.z) * 0.5f, p + vec3(s.x * 0.5f, s.y, s.z * 0.5f), MAT_WOOD, vec3(0.1f, 0.08f, 0.07f));
    }
    // Police tape across the front
    float z = L.z0 - 1.5f;
    police_tape(vec3(cx - 8, CURB_H, z), vec3(cx - 2, CURB_H, z));
    police_tape(vec3(cx + 2, CURB_H, z), vec3(cx + 8, CURB_H, z));
    // Lonely porch light still flickering
    gen_light(vec3(cx + 1.5f, 3.0f, L.z0 - 0.5f), 7.0f, vec3(1.0f, 0.6f, 0.3f), 3.0f);
    set_lm(LM_HOME, "Cross Family Home", vec3(cx, CURB_H, L.z0 - 2.0f), 4.0f);
    g_world.evidenceSpots.push(vec3(cx + 3.0f, CURB_H, L.z0 + 9.0f));
    // Bus stop where Damian arrives (south sidewalk of block 1,5)
    float bz = block_max(5) - 1.8f;
    vec3 bs(block_center(1, 5).x + 12, CURB_H, bz);
    gen_box(bs + vec3(-2.5f, 2.4f, -1.0f), bs + vec3(2.5f, 2.55f, 1.0f), MAT_STEEL, vec3(0.25f, 0.27f, 0.3f), MF_METAL);
    gen_tube(bs + vec3(-2.3f, 0, -0.9f), bs + vec3(-2.3f, 2.4f, -0.9f), 0.05f, 5, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
    gen_tube(bs + vec3(2.3f, 0, -0.9f), bs + vec3(2.3f, 2.4f, -0.9f), 0.05f, 5, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
    gen_box(bs + vec3(-2.3f, 0.2f, -1.0f), bs + vec3(2.3f, 2.3f, -0.95f), MAT_PAINT, vec3(0.1f, 0.12f, 0.14f), MF_GLASS);
    gen_box(bs + vec3(-1.6f, 0.45f, -0.8f), bs + vec3(1.6f, 0.55f, -0.3f), MAT_WOOD, vec3(0.5f, 0.4f, 0.3f));
    gen_box(bs + vec3(0.8f, 0.6f, -0.98f), bs + vec3(2.1f, 2.1f, -0.96f), MAT_PAINT, vec3(0.8f, 0.85f, 1.0f), MF_EMISSIVE, 1.0f, 8.0f);
    gen_light(bs + vec3(0, 2.2f, 0), 8.0f, vec3(0.8f, 0.9f, 1.0f), 5.0f);
    set_lm(LM_BUS_STOP, "Harlan St. Bus Stop", bs + vec3(0, 0, 1.5f), 3.0f, false);
}

static void lm_diner(Rng& rng) {
    gen_blocks(2, 6, rng, 2 | 4 | 8);
    Lot L = lot(2, 6);
    float cx = L.c.x - 8;
    vec3 mn(cx - 11, CURB_H, L.z0 + 1), mx(cx + 11, CURB_H + 4.6f, L.z0 + 14);
    gen_room(mn, mx, 0.3f, MAT_TILES, vec3(0.85f, 0.85f, 0.8f), MAT_TILES, vec3(0.9f, 0.88f, 0.82f), 1, 2.0f, 2.6f, true, MAT_PAINT, vec3(0.75f, 0.72f, 0.68f), MF_WINDOWS);
    // Chrome trim and big windows (glass band)
    gen_box(vec3(mn.x - 0.05f, mn.y + 1.0f, mn.z - 0.06f), vec3(mx.x + 0.05f, mn.y + 2.7f, mn.z - 0.01f), MAT_PAINT, vec3(0.9f, 0.75f, 0.45f), MF_EMISSIVE, 1.0f, 3.0f, false);
    gen_box(vec3(mn.x - 0.05f, mx.y - 0.4f, mn.z - 0.15f), vec3(mx.x + 0.05f, mx.y, mn.z), MAT_STEEL, vec3(0.8f, 0.8f, 0.82f), MF_METAL, 0.5f);
    // Counter, stools and booths
    gen_solid(vec3(mn.x + 3, mn.y, mx.z - 4.0f), vec3(mx.x - 3, mn.y + 1.1f, mx.z - 3.2f), MAT_STEEL, vec3(0.7f, 0.7f, 0.72f), SURF_METAL);
    for (int k = 0; k < 8; k++) gen_cyl(vec3(mn.x + 4 + k * 1.8f, mn.y, mx.z - 5.0f), 0.22f, 0.75f, 8, MAT_LEATHER, vec3(0.7f, 0.1f, 0.1f));
    for (int k = 0; k < 4; k++) {
        float x = mn.x + 3 + k * 4.5f;
        gen_solid(vec3(x, mn.y, mn.z + 1.0f), vec3(x + 1.2f, mn.y + 0.75f, mn.z + 2.8f), MAT_WOOD, vec3(0.6f, 0.4f, 0.25f), SURF_WOOD);
        gen_box(vec3(x - 0.7f, mn.y, mn.z + 0.9f), vec3(x - 0.1f, mn.y + 1.2f, mn.z + 2.9f), MAT_LEATHER, vec3(0.65f, 0.08f, 0.08f));
        gen_box(vec3(x + 1.3f, mn.y, mn.z + 0.9f), vec3(x + 1.9f, mn.y + 1.2f, mn.z + 2.9f), MAT_LEATHER, vec3(0.65f, 0.08f, 0.08f));
    }
    for (int k = 0; k < 3; k++) ceiling_lamp(vec3(mn.x + 5 + k * 6.0f, mx.y - 0.1f, (mn.z + mx.z) * 0.5f), vec3(1.0f, 0.8f, 0.55f), 9.0f, 9.0f, false);
    gen_indoor(mn, mx, 0.25f);
    // Rooftop sign
    sign_text(vec3(cx, mx.y + 0.6f, mn.z + 0.2f), 0, 1.5f, "LUCKY STAR", vec3(1.0f, 0.2f, 0.35f), true, true);
    sign_text(vec3(cx + 6.0f, mx.y - 1.5f, mn.z - 0.2f), 0, 0.8f, "DINER", vec3(0.3f, 0.9f, 1.0f), true, false);
    set_lm(LM_DINER, "Lucky Star Diner", vec3(cx, CURB_H, L.z0 - 2.0f), 4.0f);
}

static void lm_laundromat(Rng& rng) {
    gen_blocks(1, 7, rng, 2 | 4 | 8);
    Lot L = lot(1, 7);
    vec3 mn(L.c.x - 9, CURB_H, L.z0), mx(L.c.x + 9, 12.0f, L.z0 + 14);
    building(mn, mx, MAT_BRICK, vec3(0.75f, 0.75f, 0.8f), rng, true, true);
    storefront(mn, mx, 0, rng, false);
    sign_text(vec3(L.c.x, CURB_H + 4.3f, L.z0 - 0.3f), 0, 0.9f, "LAUNDROMAT", vec3(0.4f, 0.8f, 1.0f), true, true);
    // Alley beside it
    gen_light(vec3(L.c.x + 11, 4.0f, L.z0 + 6), 10.0f, vec3(1.0f, 0.6f, 0.3f), 6.0f);
    set_lm(LM_LAUNDROMAT, "Suds City Laundromat", vec3(L.c.x, CURB_H, L.z0 - 2.0f), 4.0f);
    g_world.evidenceSpots.push(vec3(L.c.x + 12, CURB_H, L.z0 + 8));
}

static void lm_pawn(Rng& rng) {
    gen_blocks(2, 7, rng, 2 | 4 | 8);
    Lot L = lot(2, 7);
    vec3 mn(L.c.x - 8, CURB_H, L.z0 + 1), mx(L.c.x + 8, CURB_H + 4.2f, L.z0 + 12);
    gen_room(mn, mx, 0.3f, MAT_PLASTER, vec3(0.55f, 0.5f, 0.45f), MAT_WOOD, vec3(0.6f, 0.5f, 0.4f), 1, 1.8f, 2.6f, true, MAT_BRICK, vec3(0.6f, 0.55f, 0.55f), MF_WINDOWS);
    gen_solid(vec3(mn.x + 2, mn.y, mx.z - 3.2f), vec3(mx.x - 2, mn.y + 1.1f, mx.z - 2.4f), MAT_PAINT, vec3(0.15f, 0.15f, 0.17f), SURF_WOOD);
    // Glass display case glow and gun rack
    gen_box(vec3(mn.x + 2.2f, mn.y + 1.1f, mx.z - 3.1f), vec3(mx.x - 2.2f, mn.y + 1.15f, mx.z - 2.5f), MAT_PAINT, vec3(0.8f, 0.9f, 1.0f), MF_EMISSIVE, 1.0f, 4.0f);
    for (int k = 0; k < 6; k++) gen_box(vec3(mn.x + 3 + k * 1.6f, mn.y + 1.8f, mx.z - 0.4f), vec3(mn.x + 3.15f + k * 1.6f, mn.y + 2.9f, mx.z - 0.32f), MAT_STEEL, vec3(0.15f, 0.15f, 0.15f), MF_METAL);
    ceiling_lamp(vec3((mn.x + mx.x) * 0.5f, mx.y - 0.1f, (mn.z + mx.z) * 0.5f), vec3(1.0f, 0.85f, 0.6f), 10.0f, 10.0f, false);
    gen_indoor(mn, mx, 0.25f);
    sign_text(vec3(L.c.x - 1.0f, mx.y + 0.4f, mn.z + 0.1f), 0, 1.3f, "PAWN", vec3(1.0f, 0.75f, 0.1f), true, true);
    sign_text(vec3(L.c.x + 4.0f, mn.y + 2.9f, mn.z - 0.2f), 0, 0.7f, "GUNS", vec3(1.0f, 0.15f, 0.1f), true, false);
    set_lm(LM_PAWN, "Ace Pawn & Arms", vec3(L.c.x, CURB_H, mn.z + 4.0f), 3.0f);
}

static void lm_safehouse(Rng& rng) {
    gen_blocks(0, 8, rng, 2 | 4 | 8);
    Lot L = lot(0, 8);
    vec3 mn(L.c.x - 6, CURB_H, L.z0 + 1), mx(L.c.x + 6, CURB_H + 3.4f, L.z0 + 10);
    gen_room(mn, mx, 0.3f, MAT_PLASTER, vec3(0.5f, 0.48f, 0.45f), MAT_WOOD, vec3(0.5f, 0.38f, 0.28f), 1, 1.4f, 2.4f, true, MAT_BRICK, vec3(0.7f, 0.6f, 0.55f), MF_WINDOWS);
    // Upper floors of the apartment block above the unit
    building(vec3(mn.x, mx.y + 0.3f, mn.z), vec3(mx.x, 16.0f, mx.z), MAT_BRICK, vec3(0.7f, 0.6f, 0.55f), rng, true, true);
    // Bed, table, lamp, TV glow
    gen_solid(vec3(mx.x - 3.0f, mn.y, mx.z - 2.6f), vec3(mx.x - 0.6f, mn.y + 0.55f, mx.z - 0.4f), MAT_FABRIC, vec3(0.35f, 0.38f, 0.45f), SURF_WOOD);
    gen_solid(vec3(mn.x + 1, mn.y, mx.z - 2.2f), vec3(mn.x + 2.4f, mn.y + 0.8f, mx.z - 1.0f), MAT_WOOD, vec3(0.45f, 0.32f, 0.2f), SURF_WOOD);
    gen_box(vec3(mn.x + 0.4f, mn.y + 1.0f, mn.z + 3.0f), vec3(mn.x + 0.5f, mn.y + 1.8f, mn.z + 4.3f), MAT_PAINT, vec3(0.4f, 0.55f, 1.0f), MF_EMISSIVE | MF_PULSE, 1.0f, 6.0f);
    gen_light(vec3(mn.x + 1.2f, mn.y + 1.4f, mn.z + 3.6f), 5.0f, vec3(0.4f, 0.55f, 1.0f), 2.5f);
    gen_light(vec3(mn.x + 1.7f, mn.y + 1.3f, mx.z - 1.6f), 6.0f, vec3(1.0f, 0.7f, 0.4f), 4.0f);
    gen_indoor(mn, mx, 0.2f);
    set_lm(LM_SAFEHOUSE, "Safehouse", vec3(mx.x - 1.8f, CURB_H, mx.z - 3.2f), 2.0f);
}

// ---------------------------------------------------------------------------
// Midtown / Downtown landmarks
// ---------------------------------------------------------------------------
static void lm_police(Rng& rng) {
    Lot L = lot(4, 6);
    vec3 mn(L.x0 + 4, CURB_H, L.z0 + 2), mx(L.x1 - 4, 15.0f, L.z0 + 26);
    building(mn, mx, MAT_CONCRETE, vec3(0.6f, 0.62f, 0.66f), rng, true, false);
    gen_box(vec3(L.c.x - 4, CURB_H, mn.z - 0.1f), vec3(L.c.x + 4, 3.5f, mn.z), MAT_PAINT, vec3(0.06f, 0.07f, 0.08f), MF_GLASS);
    sign_text(vec3(L.c.x, 5.5f, mn.z - 0.3f), 0, 1.6f, "POLICE", vec3(0.2f, 0.45f, 1.0f), true, true);
    sign_text(vec3(L.c.x, 4.0f, mn.z - 0.25f), 0, 0.6f, "4TH PRECINCT", vec3(0.9f, 0.9f, 1.0f), false, false);
    // Parking lot
    for (int k = 0; k < 4; k++) {
        vec4 s(L.x0 + 12 + k * 6.0f, CURB_H, L.z1 - 12, PI_F * 0.5f);
        g_world.parkingSpots.push(s);
    }
    set_lm(LM_POLICE, "BRPD 4th Precinct", vec3(L.c.x, CURB_H, mn.z - 3.0f), 6.0f);
}

static void lm_hospital(Rng& rng) {
    Lot L = lot(4, 0);
    vec3 mn(L.x0 + 6, CURB_H, L.z0 + 8), mx(L.x1 - 6, 28.0f, L.z1 - 10);
    building(mn, mx, MAT_PLASTER, vec3(0.9f, 0.9f, 0.88f), rng, true, false);
    vec3 cr(L.c.x, 22.0f, mx.z + 0.3f);
    gen_box(cr - vec3(2.5f, 0.7f, 0), cr + vec3(2.5f, 0.7f, 0.15f), MAT_PAINT, vec3(1.0f, 0.1f, 0.1f), MF_EMISSIVE, 1.0f, 20.0f);
    gen_box(cr - vec3(0.7f, 2.5f, 0), cr + vec3(0.7f, 2.5f, 0.15f), MAT_PAINT, vec3(1.0f, 0.1f, 0.1f), MF_EMISSIVE, 1.0f, 20.0f);
    gen_light(cr + vec3(0, 0, 2), 18.0f, vec3(1.0f, 0.15f, 0.1f), 14.0f);
    sign_text(vec3(L.c.x, 6.0f, mx.z + 0.3f), 1, 1.4f, "ST AGNES HOSPITAL", vec3(1.0f, 1.0f, 1.0f), true, true);
    gen_box(vec3(L.c.x - 6, 4.2f, mx.z), vec3(L.c.x + 6, 4.5f, mx.z + 5), MAT_CONCRETE, vec3(0.8f, 0.8f, 0.8f));
    ceiling_lamp(vec3(L.c.x, 4.1f, mx.z + 2.5f), vec3(0.9f, 0.95f, 1.0f), 14.0f, 12.0f, false);
    set_lm(LM_HOSPITAL, "St. Agnes Hospital", vec3(L.c.x, CURB_H, mx.z + 4.0f), 6.0f);
}

static void lm_plaza(Rng& rng) {
    Lot L = lot(4, 3);
    vec3 c = L.c;
    // Fountain
    gen_cyl(c, 9.0f, 0.7f, 24, MAT_CONCRETE, vec3(0.7f, 0.7f, 0.68f), 0, 1.0f, 0, false);
    world_add_box(c - vec3(9, 0, 9), c + vec3(9, 0.7f, 9), SURF_CONCRETE, 2);
    gen_cyl(c + vec3(0, 0.66f, 0), 8.4f, 0.06f, 24, MAT_PAINT, vec3(0.05f, 0.25f, 0.3f), MF_GLASS | MF_EMISSIVE, 0.1f, 2.0f);
    gen_cyl(c, 1.2f, 3.0f, 12, MAT_CONCRETE, vec3(0.75f, 0.75f, 0.72f), 0, 1.0f, 0, true);
    // Statue: a figure raising a torch (Liberty)
    vec3 s = c + vec3(0, 3.0f, 0);
    gen_box(s + vec3(-0.6f, 0, -0.4f), s + vec3(0.6f, 2.2f, 0.4f), MAT_STEEL, vec3(0.35f, 0.5f, 0.45f), MF_METAL);
    gen_ellipsoid(s + vec3(0, 2.6f, 0), vec3(0.35f, 0.42f, 0.35f), MAT_STEEL, vec3(0.35f, 0.5f, 0.45f), MF_METAL);
    gen_tube(s + vec3(0.5f, 2.0f, 0), s + vec3(0.8f, 3.8f, 0), 0.15f, 6, MAT_STEEL, vec3(0.35f, 0.5f, 0.45f), MF_METAL);
    gen_ellipsoid(s + vec3(0.85f, 4.1f, 0), vec3(0.22f, 0.3f, 0.22f), MAT_PAINT, vec3(1.0f, 0.75f, 0.4f), MF_EMISSIVE, 1.0f, 25.0f);
    gen_light(s + vec3(0.85f, 4.6f, 0), 22.0f, vec3(1.0f, 0.75f, 0.4f), 22.0f);
    for (int k = 0; k < 4; k++) {
        vec3 o((k & 1) ? 1.0f : -1.0f, 0, (k & 2) ? 1.0f : -1.0f);
        gen_spot(c + o * 12.0f + vec3(0, 0.3f, 0), normalize(vec3(-o.x, 1.4f, -o.z)), 22.0f, vec3(0.8f, 0.9f, 1.0f), 30.0f, 0.85f);
        gen_box(c + o * 12.0f - vec3(0.3f, 0, 0.3f), c + o * 12.0f + vec3(0.3f, 0.35f, 0.3f), MAT_STEEL, vec3(0.2f, 0.2f, 0.2f), MF_METAL);
    }
    // Tree grid and benches
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++) {
            if ((a == 1 || a == 2) && (b == 1 || b == 2)) continue;
            vec3 p(lerpf(L.x0 + 8, L.x1 - 8, a / 3.0f), CURB_H, lerpf(L.z0 + 8, L.z1 - 8, b / 3.0f));
            gen_tree(p, 1.1f, rng.next());
            gen_cyl(p, 1.6f, 0.45f, 12, MAT_CONCRETE, vec3(0.6f, 0.6f, 0.58f));
        }
    for (int k = 0; k < 6; k++) {
        float a = TAU_F * k / 6.0f;
        vec3 p = c + vec3(sinf(a) * 14.0f, 0, cosf(a) * 14.0f);
        gen_streetlight(p, normalize(vec3(-sinf(a), 0, -cosf(a))), true);
    }
    set_lm(LM_PLAZA, "Liberty Plaza", c + vec3(0, 0, 16), 20.0f);
}

static void lm_meridian(Rng& rng) {
    Lot L = lot(5, 3);
    tower(vec3(L.x0 + 6, 0, L.z0 + 6), vec3(L.x1 - 6, 0, L.z1 - 6), 240.0f, rng);
    sign_text(vec3(L.c.x, 7.0f, L.z0 + 5.6f), 0, 1.6f, "MERIDIAN TOWER", vec3(0.9f, 0.95f, 1.0f), true, false);
    set_lm(LM_CROSS_TOWER, "Meridian Tower", vec3(L.c.x, CURB_H, L.z0 - 1.0f), 10.0f);
}

// ---------------------------------------------------------------------------
// Ironworks landmarks
// ---------------------------------------------------------------------------
static void lm_fight_pit(Rng& rng) {
    Lot L = lot(7, 3);
    vec3 mn(L.c.x - 26, CURB_H, L.c.z - 18), mx(L.c.x + 26, CURB_H + 10.0f, L.c.z + 18);
    gen_room(mn, mx, 0.4f, MAT_CORRUGATED, vec3(0.35f, 0.33f, 0.3f), MAT_FLOOR_CONCRETE, vec3(0.5f, 0.48f, 0.45f), 1, 3.0f, 3.4f, true, MAT_CORRUGATED, vec3(0.55f, 0.35f, 0.28f), 0);
    // Ring
    vec3 rc(L.c.x, CURB_H, L.c.z + 2);
    float rs = 5.5f;
    gen_solid(rc - vec3(rs + 0.5f, 0, rs + 0.5f), rc + vec3(rs + 0.5f, 0.5f, rs + 0.5f), MAT_FABRIC, vec3(0.45f, 0.42f, 0.38f), SURF_WOOD, 0, 1.0f);
    for (int k = 0; k < 4; k++) {
        vec3 o((k & 1) ? rs : -rs, 0.5f, (k & 2) ? rs : -rs);
        gen_tube(rc + o, rc + o + vec3(0, 1.4f, 0), 0.08f, 6, MAT_STEEL, vec3(0.6f, 0.1f, 0.1f), MF_METAL);
    }
    for (int lvl = 0; lvl < 3; lvl++) {
        float y = 0.9f + lvl * 0.35f;
        vec3 a(-rs, y, -rs), b(rs, y, -rs), c2(rs, y, rs), d(-rs, y, rs);
        gen_tube(rc + a, rc + b, 0.03f, 4, MAT_LEATHER, vec3(0.75f, 0.1f, 0.1f));
        gen_tube(rc + b, rc + c2, 0.03f, 4, MAT_LEATHER, vec3(0.75f, 0.1f, 0.1f));
        gen_tube(rc + c2, rc + d, 0.03f, 4, MAT_LEATHER, vec3(0.75f, 0.1f, 0.1f));
        gen_tube(rc + d, rc + a, 0.03f, 4, MAT_LEATHER, vec3(0.75f, 0.1f, 0.1f));
    }
    // Ring ropes collide (low walls) except the entrance gap on -Z
    world_add_box(rc + vec3(-rs - 0.1f, 0.5f, -rs - 0.1f), rc + vec3(-1.0f, 1.9f, -rs + 0.1f), SURF_WOOD, 0);
    world_add_box(rc + vec3(1.0f, 0.5f, -rs - 0.1f), rc + vec3(rs + 0.1f, 1.9f, -rs + 0.1f), SURF_WOOD, 0);
    world_add_box(rc + vec3(-rs - 0.1f, 0.5f, rs - 0.1f), rc + vec3(rs + 0.1f, 1.9f, rs + 0.1f), SURF_WOOD, 0);
    world_add_box(rc + vec3(-rs - 0.1f, 0.5f, -rs), rc + vec3(-rs + 0.1f, 1.9f, rs), SURF_WOOD, 0);
    world_add_box(rc + vec3(rs - 0.1f, 0.5f, -rs), rc + vec3(rs + 0.1f, 1.9f, rs), SURF_WOOD, 0);
    // Hanging lights over the ring
    for (int k = 0; k < 4; k++) {
        vec3 p = rc + vec3((k & 1) ? 2.5f : -2.5f, 7.0f, (k & 2) ? 2.5f : -2.5f);
        gen_tube(p, p + vec3(0, 2.9f, 0), 0.02f, 4, MAT_STEEL, vec3(0.2f, 0.2f, 0.2f));
        MeshBuilder& mb = gen_chunk(p);
        mb.set_mat(MAT_STEEL, MF_METAL, 1.0f);
        mb.set_color(vec3(0.15f, 0.15f, 0.15f));
        mb.cone_frustum(p - vec3(0, 0.5f, 0), 0.6f, 0.15f, 0.5f, 10, false);
        gen_box(p - vec3(0.35f, 0.55f, 0.35f), p + vec3(0.35f, -0.5f, 0.35f), MAT_PAINT, vec3(1.0f, 0.9f, 0.75f), MF_EMISSIVE, 1.0f, 25.0f);
        gen_spot(p - vec3(0, 0.6f, 0), vec3(0, -1, 0), 14.0f, vec3(1.0f, 0.9f, 0.75f), 26.0f, 0.72f);
        gen_beam(p - vec3(0, 0.55f, 0), p - vec3(0, 6.3f, 0), 0.5f, 3.0f, vec3(1.0f, 0.9f, 0.7f), 0.2f);
    }
    // Fire barrels and crowd barriers
    for (int k = 0; k < 4; k++) {
        vec3 p = rc + vec3((k & 1) ? 13.0f : -13.0f, 0, (k & 2) ? 9.0f : -9.0f);
        gen_cyl(p, 0.35f, 0.9f, 10, MAT_RUST, vec3(0.5f, 0.4f, 0.35f), MF_METAL, 1.0f, 0, true);
        gen_light(p + vec3(0, 1.4f, 0), 12.0f, vec3(1.0f, 0.45f, 0.12f), 14.0f);
        g_world.firePoints.push(p + vec3(0, 0.95f, 0));
    }
    for (int k = 0; k < 6; k++) {
        float x = L.c.x - 18 + k * 7.2f;
        gen_solid(vec3(x, CURB_H, L.c.z + 12), vec3(x + 5.5f, CURB_H + 1.0f, L.c.z + 12.3f), MAT_STEEL, vec3(0.4f, 0.4f, 0.42f), SURF_METAL);
    }
    gen_indoor(mn, mx, 0.15f);
    // Outer storage yard
    warehouse(vec3(L.x0 + 2, 0, L.z1 - 16), vec3(L.x0 + 30, 0, L.z1 - 2), rng, 9.0f);
    set_lm(LM_FIGHT_PIT, "The Fight Pit", vec3(L.c.x, CURB_H, mn.z - 3.0f), 5.0f);
    g_world.evidenceSpots.push(vec3(mx.x - 3, CURB_H, mx.z - 3));
}

static void lm_chop_shop(Rng& rng) {
    Lot L = lot(6, 6);
    vec3 mn(L.c.x - 15, CURB_H, L.z0 + 2), mx(L.c.x + 15, CURB_H + 7.0f, L.z0 + 24);
    gen_room(mn, mx, 0.4f, MAT_CONCRETE, vec3(0.45f, 0.45f, 0.45f), MAT_FLOOR_CONCRETE, vec3(0.45f, 0.45f, 0.45f), 1, 7.0f, 5.0f, true, MAT_BRICK, vec3(0.55f, 0.5f, 0.5f), MF_WINDOWS);
    for (int k = 0; k < 3; k++) ceiling_lamp(vec3(mn.x + 7 + k * 8.0f, mx.y - 0.1f, (mn.z + mx.z) * 0.5f), vec3(0.85f, 0.95f, 1.0f), 12.0f, 12.0f, true);
    gen_solid(vec3(mx.x - 6, mn.y, mx.z - 3), vec3(mx.x - 1, mn.y + 1.0f, mx.z - 1), MAT_STEEL, vec3(0.6f, 0.2f, 0.1f), SURF_METAL);
    gen_indoor(mn, mx, 0.2f);
    sign_text(vec3(L.c.x, mx.y + 0.5f, mn.z + 0.2f), 0, 1.2f, "GARAGE", vec3(1.0f, 0.5f, 0.1f), true, true);
    warehouse(vec3(L.x0 + 2, 0, L.z1 - 30), vec3(L.x1 - 2, 0, L.z1 - 4), rng, 11.0f);
    set_lm(LM_CHOP_SHOP, "Varga's Garage", vec3(L.c.x, CURB_H, mn.z + 6.0f), 6.0f);
}

static void lm_warehouse(Rng& rng) {
    Lot L = lot(8, 6);
    vec3 mn(L.c.x - 32, CURB_H, L.c.z - 22), mx(L.c.x + 32, CURB_H + 12.0f, L.c.z + 22);
    gen_room(mn, mx, 0.4f, MAT_CORRUGATED, vec3(0.4f, 0.42f, 0.45f), MAT_FLOOR_CONCRETE, vec3(0.5f, 0.5f, 0.5f), 1 | 4, 4.0f, 4.5f, true, MAT_CORRUGATED, vec3(0.3f, 0.4f, 0.5f), MF_METAL);
    // Storage racks
    for (int r = 0; r < 4; r++) {
        float z = mn.z + 8 + r * 8.0f;
        for (int s = 0; s < 2; s++) {
            float x0 = s == 0 ? mn.x + 6 : L.c.x + 4;
            float x1 = x0 + 18;
            gen_solid(vec3(x0, mn.y, z), vec3(x1, mn.y + 0.15f, z + 2.0f), MAT_STEEL, vec3(0.2f, 0.3f, 0.6f), SURF_METAL);
            for (int lv = 1; lv <= 3; lv++) gen_box(vec3(x0, mn.y + lv * 1.6f, z), vec3(x1, mn.y + lv * 1.6f + 0.1f, z + 2.0f), MAT_STEEL, vec3(0.75f, 0.45f, 0.1f), MF_METAL);
            for (int p = 0; p <= 3; p++) gen_box(vec3(x0 + p * 6.0f - 0.1f, mn.y, z), vec3(x0 + p * 6.0f + 0.1f, mn.y + 5.0f, z + 2.0f), MAT_STEEL, vec3(0.2f, 0.3f, 0.6f), MF_METAL);
            world_add_box(vec3(x0, mn.y, z), vec3(x1, mn.y + 5.0f, z + 2.0f), SURF_METAL, 1);
            for (int c = 0; c < 6; c++) {
                float cxp = x0 + 1.5f + c * 2.9f;
                int lv = rng.irange(0, 3);
                vec3 b(cxp, mn.y + 0.15f + lv * 1.6f, z + 1.0f);
                gen_box(b - vec3(0.7f, 0, 0.7f), b + vec3(0.7f, 1.2f, 0.7f), MAT_WOOD, vec3(0.65f, 0.5f, 0.35f));
            }
        }
    }
    // The strange equipment: alien-blue pods, cables and a containment crate
    vec3 lab(L.c.x + 18, mn.y, mx.z - 9);
    for (int k = 0; k < 3; k++) {
        vec3 p = lab + vec3(-6.0f + k * 4.0f, 0, 0);
        gen_cyl(p, 0.9f, 0.4f, 12, MAT_METAL_PANEL, vec3(0.3f, 0.3f, 0.32f), MF_METAL, 1.0f, 0, true);
        gen_cyl(p + vec3(0, 0.4f, 0), 0.7f, 2.4f, 12, MAT_PAINT, vec3(0.2f, 0.75f, 1.0f), MF_EMISSIVE | MF_PULSE | MF_GLASS, 0.2f, 6.0f, false);
        gen_cyl(p + vec3(0, 2.8f, 0), 0.9f, 0.3f, 12, MAT_METAL_PANEL, vec3(0.3f, 0.3f, 0.32f), MF_METAL);
        gen_light(p + vec3(0, 1.6f, 1.2f), 8.0f, vec3(0.2f, 0.7f, 1.0f), 9.0f);
        gen_tube(p + vec3(0, 3.1f, 0), vec3(p.x + rng.range(-3, 3), mx.y - 0.2f, p.z + rng.range(-3, 3)), 0.06f, 5, MAT_RUBBER, vec3(0.2f, 0.2f, 0.2f));
    }
    gen_solid(lab + vec3(6, 0, -1.5f), lab + vec3(9, 2.0f, 1.5f), MAT_METAL_PANEL, vec3(0.25f, 0.27f, 0.25f), SURF_METAL);
    gen_box(lab + vec3(5.95f, 1.0f, -0.4f), lab + vec3(6.0f, 1.3f, 0.4f), MAT_PAINT, vec3(0.3f, 1.0f, 0.5f), MF_EMISSIVE | MF_PULSE, 1.0f, 10.0f);
    // Office in the corner
    gen_room(vec3(mn.x + 1, mn.y, mx.z - 9), vec3(mn.x + 10, mn.y + 3.0f, mx.z - 1), 0.2f, MAT_PLASTER, vec3(0.6f, 0.6f, 0.58f), MAT_TILES, vec3(0.6f, 0.6f, 0.6f), 8, 1.2f, 2.3f, true);
    for (int k = 0; k < 4; k++) ceiling_lamp(vec3(mn.x + 10 + k * 14.0f, mx.y - 0.2f, L.c.z), vec3(0.9f, 0.95f, 1.0f), 9.0f, 16.0f, true);
    gen_indoor(mn, mx, 0.12f);
    sign_text(vec3(L.c.x, mx.y - 3.0f, mn.z - 0.3f), 0, 1.3f, "HALBERD SHIPPING", vec3(0.85f, 0.9f, 1.0f), true, true);
    set_lm(LM_WAREHOUSE, "Halberd Shipping Warehouse", vec3(L.c.x, CURB_H, mn.z - 4.0f), 6.0f);
    g_world.evidenceSpots.push(vec3(mn.x + 5, CURB_H, mx.z - 5));
}

static void lm_substation(Rng& rng) {
    Lot L = lot(8, 8);
    float x0 = L.x0 + 4, x1 = L.x1 - 4, z0 = L.z0 + 4, z1 = L.z1 - 4;
    chainlink(vec3(x0, CURB_H, z0), vec3(L.c.x - 4, CURB_H, z0), 3.0f);
    chainlink(vec3(L.c.x + 4, CURB_H, z0), vec3(x1, CURB_H, z0), 3.0f);
    chainlink(vec3(x0, CURB_H, z1), vec3(x1, CURB_H, z1), 3.0f);
    chainlink(vec3(x0, CURB_H, z0), vec3(x0, CURB_H, z1), 3.0f);
    chainlink(vec3(x1, CURB_H, z0), vec3(x1, CURB_H, z1), 3.0f);
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 2; b++) {
            vec3 p(lerpf(x0 + 12, x1 - 12, a / 2.0f), CURB_H, lerpf(z0 + 18, z1 - 12, (float)b));
            gen_solid(p - vec3(2.0f, 0, 1.5f), p + vec3(2.0f, 3.2f, 1.5f), MAT_METAL_PANEL, vec3(0.45f, 0.5f, 0.45f), SURF_METAL);
            for (int f = 0; f < 5; f++) gen_box(p + vec3(-2.3f, 0.3f, -1.2f + f * 0.6f), p + vec3(-2.0f, 2.8f, -1.1f + f * 0.6f), MAT_METAL_PANEL, vec3(0.4f, 0.45f, 0.4f), MF_METAL);
            for (int k = 0; k < 3; k++) {
                vec3 q = p + vec3(-1.2f + k * 1.2f, 3.2f, 0);
                gen_cyl(q, 0.18f, 1.4f, 8, MAT_PAINT, vec3(0.55f, 0.35f, 0.25f), 0, 0.4f);
                gen_tube(q + vec3(0, 1.4f, 0), vec3(q.x, 12.0f, z1 - 2), 0.025f, 4, MAT_STEEL, vec3(0.1f, 0.1f, 0.1f));
            }
        }
    // Pylon
    vec3 py(L.c.x, CURB_H, z1 - 2);
    for (int k = 0; k < 4; k++) {
        vec3 o((k & 1) ? 2.0f : -2.0f, 0, (k & 2) ? 2.0f : -2.0f);
        gen_tube(py + o, py + o * 0.25f + vec3(0, 16, 0), 0.12f, 4, MAT_STEEL, vec3(0.5f, 0.52f, 0.55f), MF_METAL);
    }
    gen_tube(py + vec3(-6, 13, 0), py + vec3(6, 13, 0), 0.15f, 4, MAT_STEEL, vec3(0.5f, 0.52f, 0.55f), MF_METAL);
    world_add_box(py - vec3(2.2f, 0, 2.2f), py + vec3(2.2f, 16, 2.2f), SURF_METAL, 1);
    // Control hut with three relay consoles
    vec3 hm(x0 + 2, CURB_H, z0 + 2), hx(x0 + 12, CURB_H + 3.2f, z0 + 10);
    gen_room(hm, hx, 0.25f, MAT_CONCRETE, vec3(0.55f, 0.55f, 0.55f), MAT_FLOOR_CONCRETE, vec3(0.5f, 0.5f, 0.5f), 8, 1.4f, 2.4f, true);
    ceiling_lamp(vec3((hm.x + hx.x) * 0.5f, hx.y - 0.1f, (hm.z + hx.z) * 0.5f), vec3(0.9f, 1.0f, 0.9f), 8.0f, 8.0f, false);
    gen_indoor(hm, hx, 0.2f);
    for (int k = 0; k < 3; k++) {
        vec3 p = vec3(lerpf(x0 + 12, x1 - 12, k / 2.0f), CURB_H, z0 + 10);
        gen_solid(p - vec3(0.6f, 0, 0.3f), p + vec3(0.6f, 1.5f, 0.3f), MAT_METAL_PANEL, vec3(0.3f, 0.32f, 0.35f), SURF_METAL);
        gen_box(p + vec3(-0.4f, 1.0f, -0.31f), p + vec3(0.4f, 1.35f, -0.3f), MAT_PAINT, vec3(1.0f, 0.6f, 0.1f), MF_EMISSIVE | MF_PULSE, 1.0f, 8.0f);
        g_world.relayPoints.push(p + vec3(0, 0, -1.2f));
    }
    gen_light(vec3(L.c.x, 8, L.c.z), 30.0f, vec3(1.0f, 0.8f, 0.5f), 30.0f);
    set_lm(LM_SUBSTATION, "Ironworks Substation", vec3(L.c.x, CURB_H, z0 - 3.0f), 8.0f);
    FIGHTO_UNUSED(rng);
}

static void lm_nexus(Rng& rng) {
    Lot L = lot(8, 1);
    vec3 mn(L.c.x - 18, CURB_H, L.z0 + 4), mx(L.c.x + 18, CURB_H + 5.0f, L.z0 + 30);
    gen_room(mn, mx, 0.3f, MAT_METAL_PANEL, vec3(0.3f, 0.32f, 0.36f), MAT_TILES, vec3(0.35f, 0.36f, 0.4f), 1, 2.4f, 3.0f, true, MAT_GLASS_FACADE, vec3(0.3f, 0.35f, 0.42f), MF_WINDOWS);
    building(vec3(mn.x, mx.y + 0.3f, mn.z), vec3(mx.x, 34.0f, mx.z), MAT_GLASS_FACADE, vec3(0.28f, 0.32f, 0.4f), rng, true, false);
    // Reception desk and elevator
    gen_solid(vec3(L.c.x - 3, mn.y, mn.z + 8), vec3(L.c.x + 3, mn.y + 1.1f, mn.z + 9.2f), MAT_STEEL, vec3(0.2f, 0.2f, 0.22f), SURF_METAL);
    vec3 el(L.c.x, mn.y, mx.z - 0.35f);
    gen_box(el + vec3(-1.6f, 0, -0.1f), el + vec3(1.6f, 3.0f, 0), MAT_STEEL, vec3(0.6f, 0.62f, 0.65f), MF_METAL, 0.5f);
    gen_box(el + vec3(-1.8f, 3.0f, -0.12f), el + vec3(1.8f, 3.15f, 0), MAT_PAINT, vec3(0.3f, 0.8f, 1.0f), MF_EMISSIVE, 1.0f, 10.0f);
    gen_box(el + vec3(-0.02f, 0, -0.12f), el + vec3(0.02f, 3.0f, -0.1f), MAT_PAINT, vec3(0.1f, 0.1f, 0.1f));
    for (int k = 0; k < 3; k++) ceiling_lamp(vec3(mn.x + 9 + k * 9.0f, mx.y - 0.1f, (mn.z + mx.z) * 0.5f), vec3(0.85f, 0.9f, 1.0f), 9.0f, 10.0f, false);
    gen_indoor(mn, mx, 0.2f);
    sign_text(vec3(L.c.x, mx.y + 0.8f, mn.z - 0.3f), 0, 1.2f, "NEXUS LOGISTICS", vec3(0.8f, 0.9f, 1.0f), true, false);
    set_lm(LM_NEXUS, "Nexus Logistics", vec3(L.c.x, CURB_H, mx.z - 2.0f), 2.5f);
}

static void lm_drain_hatch(Rng& rng) {
    Lot L = lot(7, 8);
    warehouse(vec3(L.x0 + 4, 0, L.z0 + 4), vec3(L.x1 - 4, 0, L.c.z - 6), rng, 11.0f);
    vec3 h(L.c.x + 12, CURB_H, L.z1 - 14);
    gen_solid(h - vec3(2.0f, 0, 2.0f), h + vec3(2.0f, 3.0f, 2.0f), MAT_CONCRETE, vec3(0.5f, 0.5f, 0.48f));
    gen_box(h + vec3(-0.7f, 0, -2.06f), h + vec3(0.7f, 2.2f, -2.0f), MAT_RUST, vec3(0.6f, 0.5f, 0.45f), MF_METAL);
    gen_box(h + vec3(-0.2f, 2.4f, -2.1f), h + vec3(0.2f, 2.6f, -2.0f), MAT_PAINT, vec3(1.0f, 0.5f, 0.1f), MF_EMISSIVE, 1.0f, 12.0f);
    gen_light(h + vec3(0, 2.5f, -2.8f), 8.0f, vec3(1.0f, 0.5f, 0.15f), 6.0f);
    chainlink(vec3(h.x - 6, CURB_H, h.z - 6), vec3(h.x + 6, CURB_H, h.z - 6), 2.5f);
    set_lm(LM_DRAIN_HATCH, "Storm Drain Access", h + vec3(0, 0, -3.0f), 2.0f);
}

static void lm_mansion(Rng& rng) {
    Lot L = lot(0, 1);
    house(vec3(L.c.x - 15, 0, L.c.z - 8), vec3(L.c.x + 15, 0, L.c.z + 8), rng, true);
    // Estate wall with gate gap
    vec3 wc(0.7f, 0.68f, 0.62f);
    gen_solid(vec3(L.x0, CURB_H, L.z0), vec3(L.x1, 2.4f, L.z0 + 0.5f), MAT_CONCRETE, wc);
    gen_solid(vec3(L.x0, CURB_H, L.z0), vec3(L.x0 + 0.5f, 2.4f, L.z1), MAT_CONCRETE, wc);
    gen_solid(vec3(L.x1 - 0.5f, CURB_H, L.z0), vec3(L.x1, 2.4f, L.z1), MAT_CONCRETE, wc);
    gen_solid(vec3(L.x0, CURB_H, L.z1 - 0.5f), vec3(L.c.x - 4, 2.4f, L.z1), MAT_CONCRETE, wc);
    gen_solid(vec3(L.c.x + 4, CURB_H, L.z1 - 0.5f), vec3(L.x1, 2.4f, L.z1), MAT_CONCRETE, wc);
    // Pool
    vec3 pc(L.c.x, CURB_H + 0.02f, L.c.z - 20);
    gen_box(pc - vec3(8, 0, 3.5f), pc + vec3(8, 0.02f, 3.5f), MAT_TILES, vec3(0.2f, 0.65f, 0.75f), MF_EMISSIVE | MF_GLASS, 0.1f, 2.5f);
    gen_light(pc + vec3(0, 0.5f, 0), 12.0f, vec3(0.2f, 0.8f, 1.0f), 10.0f);
    for (int k = 0; k < 6; k++) gen_tree(vec3(rng.range(L.x0 + 4, L.x1 - 4), CURB_H, rng.range(L.z0 + 4, L.z0 + 12)), 1.3f, rng.next());
    for (int k = 0; k < 2; k++) {
        vec3 gp(L.c.x + (k ? 4.5f : -4.5f), CURB_H, L.z1 - 0.25f);
        gen_box(gp - vec3(0.5f, 0, 0.5f), gp + vec3(0.5f, 3.0f, 0.5f), MAT_CONCRETE, wc * 0.9f);
        gen_light(gp + vec3(0, 3.3f, 0.8f), 9.0f, vec3(1.0f, 0.75f, 0.45f), 8.0f);
        gen_ellipsoid(gp + vec3(0, 3.2f, 0), vec3(0.25f, 0.25f, 0.25f), MAT_PAINT, vec3(1.0f, 0.8f, 0.5f), MF_EMISSIVE, 1.0f, 18.0f);
    }
    set_lm(LM_MANSION, "Varga Estate", vec3(L.c.x, CURB_H, L.z1 + 2.0f), 6.0f);
    g_world.evidenceSpots.push(vec3(L.c.x + 13, CURB_H, L.c.z + 11));
}

static void lm_car_lot(Rng& rng) {
    Lot L = lot(1, 2);
    vec3 mn(L.c.x - 15, CURB_H, L.z0 + 4), mx(L.c.x + 15, CURB_H + 6.0f, L.z0 + 20);
    gen_box(mn, mx, MAT_PAINT, vec3(0.05f, 0.06f, 0.07f), MF_GLASS, 0.1f, 0, false);
    gen_box(vec3(mn.x, mx.y, mn.z), vec3(mx.x, mx.y + 0.5f, mx.z), MAT_STEEL, vec3(0.75f, 0.76f, 0.8f), MF_METAL, 0.5f, 0, true, true);
    world_add_box(mn, mx + vec3(0, 0.5f, 0), SURF_GLASS, 1);
    gen_box(vec3(mn.x - 0.05f, mx.y - 0.4f, mn.z - 0.05f), vec3(mx.x + 0.05f, mx.y - 0.2f, mx.z + 0.05f), MAT_PAINT, vec3(0.8f, 0.9f, 1.0f), MF_EMISSIVE, 1.0f, 12.0f, false);
    sign_text(vec3(L.c.x, mx.y + 0.9f, mx.z + 0.2f), 1, 1.3f, "SILVERLINE MOTORS", vec3(0.9f, 0.95f, 1.0f), true, false);
    for (int k = 0; k < 5; k++) {
        vec4 s(L.x0 + 10 + k * 14.0f, CURB_H, L.z1 - 12, 0);
        g_world.parkingSpots.push(s);
        vec3 lp(L.x0 + 10 + k * 14.0f, 6.0f, L.z1 - 6);
        gen_tube(vec3(lp.x, CURB_H, lp.z), lp, 0.08f, 5, MAT_STEEL, vec3(0.3f, 0.3f, 0.3f), MF_METAL);
        gen_box(lp - vec3(0.3f, 0.1f, 0.3f), lp + vec3(0.3f, 0.05f, 0.3f), MAT_PAINT, vec3(0.9f, 0.95f, 1.0f), MF_EMISSIVE, 1.0f, 20.0f);
        gen_light(lp - vec3(0, 0.4f, 0), 14.0f, vec3(0.9f, 0.95f, 1.0f), 16.0f);
    }
    set_lm(LM_CAR_LOT, "Silverline Motors", vec3(L.c.x, CURB_H, L.z1 - 8), 8.0f);
}

void gen_landmarks(Rng& rng) {
    lm_burned_home(rng);
    lm_diner(rng);
    lm_laundromat(rng);
    lm_pawn(rng);
    lm_safehouse(rng);
    lm_police(rng);
    lm_hospital(rng);
    lm_plaza(rng);
    lm_meridian(rng);
    lm_fight_pit(rng);
    lm_chop_shop(rng);
    lm_warehouse(rng);
    lm_substation(rng);
    lm_nexus(rng);
    lm_drain_hatch(rng);
    lm_mansion(rng);
    lm_car_lot(rng);
    // Remaining evidence spots in alleys across the city
    const int extra[][2] = {{0, 5}, {3, 7}, {6, 1}, {7, 5}, {2, 3}, {5, 7}};
    for (int k = 0; k < ARRAY_COUNT(extra); k++) {
        vec3 c = block_center(extra[k][0], extra[k][1]);
        g_world.evidenceSpots.push(c + vec3(rng.range(-12, 12), CURB_H, rng.range(-12, 12)));
    }
}

// ---------------------------------------------------------------------------
// A.E.G.I.S. underground facility
// ---------------------------------------------------------------------------
static vec3 F(float x, float y, float z) { return vec3(AEGIS_ORIGIN_X + x, AEGIS_ORIGIN_Y + y, AEGIS_ORIGIN_Z + z); }

static void fwall(vec3 a, vec3 b, float h, int layer, vec3 col) {
    vec3 mn = vmin(a, b), mx = vmax(a, b);
    if (mx.x - mn.x < 0.3f) { mn.x -= 0.2f; mx.x += 0.2f; }
    if (mx.z - mn.z < 0.3f) { mn.z -= 0.2f; mx.z += 0.2f; }
    mx.y = mn.y + h;
    gen_solid(mn, mx, layer, col, SURF_METAL, 0, 1.0f);
}

static void panel_light(vec3 p, vec3 col, float intensity, float radius) {
    gen_box(p - vec3(1.2f, 0.04f, 0.25f), p + vec3(1.2f, 0.04f, 0.25f), MAT_PAINT, col, MF_EMISSIVE, 1.0f, 14.0f, true, true);
    gen_light(p - vec3(0, 0.5f, 0), radius, col, intensity);
}

void gen_aegis_facility() {
    vec3 wallCol(0.42f, 0.45f, 0.5f);
    vec3 floorCol(0.5f, 0.52f, 0.55f);
    vec3 white(0.85f, 0.92f, 1.0f);
    float H = 6.0f;
    // Base slab and ceiling over the full footprint
    vec3 fmn = F(-46, 0, -100), fmx = F(52, 0, 82);
    gen_box(vec3(fmn.x, fmn.y - 1.0f, fmn.z), vec3(fmx.x, fmn.y, fmx.z), MAT_TILES, floorCol, MF_GROUND, 0.6f);
    world_add_box(vec3(fmn.x, fmn.y - 1.0f, fmn.z), vec3(fmx.x, fmn.y, fmx.z), SURF_METAL, 1);
    gen_box(vec3(fmn.x, fmn.y + H + 4.0f, fmn.z), vec3(fmx.x, fmn.y + H + 5.0f, fmx.z), MAT_METAL_PANEL, vec3(0.2f, 0.21f, 0.24f), 0, 1.0f, 0, true, true);
    world_add_box(vec3(fmn.x, fmn.y + H + 4.0f, fmn.z), vec3(fmx.x, fmn.y + H + 5.0f, fmx.z), SURF_METAL, 1);
    // Outer shell
    fwall(F(-46, 0, -100), F(52, 0, -100), H + 4, MAT_CONCRETE, vec3(0.3f, 0.3f, 0.3f));
    fwall(F(-46, 0, 82), F(52, 0, 82), H + 4, MAT_CONCRETE, vec3(0.3f, 0.3f, 0.3f));
    fwall(F(-46, 0, -100), F(-46, 0, 82), H + 4, MAT_CONCRETE, vec3(0.3f, 0.3f, 0.3f));
    fwall(F(52, 0, -100), F(52, 0, 82), H + 4, MAT_CONCRETE, vec3(0.3f, 0.3f, 0.3f));
    gen_indoor(F(-46, -1, -100), F(52, 12, 82), 0.18f);

    // Elevator lobby (south): x -12..12, z 60..80
    fwall(F(-12, 0, 60), F(-4, 0, 60), H, MAT_METAL_PANEL, wallCol);
    fwall(F(4, 0, 60), F(12, 0, 60), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-12, 0, 60), F(-12, 0, 82), H, MAT_METAL_PANEL, wallCol);
    fwall(F(12, 0, 60), F(12, 0, 82), H, MAT_METAL_PANEL, wallCol);
    vec3 el = F(0, 0, 81.4f);
    gen_box(el + vec3(-1.6f, 0, -0.2f), el + vec3(1.6f, 3.0f, 0), MAT_STEEL, vec3(0.6f, 0.62f, 0.65f), MF_METAL, 0.5f);
    gen_box(el + vec3(-1.8f, 3.0f, -0.25f), el + vec3(1.8f, 3.15f, 0), MAT_PAINT, vec3(0.3f, 0.8f, 1.0f), MF_EMISSIVE, 1.0f, 10.0f);
    panel_light(F(0, H - 0.1f, 70), white, 12.0f, 14.0f);
    sign_text(F(0, 3.6f, 60.3f), 1, 0.9f, "A.E.G.I.S.", vec3(0.95f, 0.75f, 0.2f), true, false);
    set_lm(LM_AEGIS_LOBBY, "A.E.G.I.S. Elevator", F(0, 0, 78), 2.5f, false);

    // Main corridor x -4..4, z -45..60 with door gaps
    struct Gap { float z0, z1; };
    // West wall (x = -4): gaps for lab (z 30..34) and armory (z -6..-2)
    fwall(F(-4, 0, -45), F(-4, 0, -6), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-4, 0, -2), F(-4, 0, 30), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-4, 0, 34), F(-4, 0, 60), H, MAT_METAL_PANEL, wallCol);
    // East wall (x = 4): gaps for briefing (z 32..36) and range (z -12..-8)
    fwall(F(4, 0, -45), F(4, 0, -12), H, MAT_METAL_PANEL, wallCol);
    fwall(F(4, 0, -8), F(4, 0, 32), H, MAT_METAL_PANEL, wallCol);
    fwall(F(4, 0, 36), F(4, 0, 60), H, MAT_METAL_PANEL, wallCol);
    for (int k = 0; k < 9; k++) panel_light(F(0, H - 0.1f, -40 + k * 12.0f), white, 7.0f, 10.0f);
    // Floor guide strips
    gen_box(F(-3.2f, 0.01f, -45), F(-3.0f, 0.03f, 60), MAT_PAINT, vec3(0.95f, 0.7f, 0.15f), MF_EMISSIVE, 1.0f, 4.0f);
    gen_box(F(3.0f, 0.01f, -45), F(3.2f, 0.03f, 60), MAT_PAINT, vec3(0.95f, 0.7f, 0.15f), MF_EMISSIVE, 1.0f, 4.0f);
    // Security checkpoint: scanner arch
    for (int k = 0; k < 2; k++) {
        float x = k == 0 ? -2.2f : 2.2f;
        gen_box(F(x - 0.25f, 0, 49), F(x + 0.25f, 3.2f, 50), MAT_METAL_PANEL, vec3(0.25f, 0.27f, 0.3f), MF_METAL);
        gen_box(F(x - 0.27f, 0.2f, 49.4f), F(x + 0.27f, 3.0f, 49.6f), MAT_PAINT, vec3(1.0f, 0.2f, 0.15f), MF_EMISSIVE | MF_PULSE, 1.0f, 10.0f);
    }
    gen_box(F(-2.2f, 3.2f, 49), F(2.2f, 3.6f, 50), MAT_METAL_PANEL, vec3(0.25f, 0.27f, 0.3f), MF_METAL);
    gen_light(F(0, 2.5f, 49.5f), 6.0f, vec3(1.0f, 0.25f, 0.2f), 6.0f);

    // Briefing room (east): x 4..36, z 22..48 — holographic globe at the centre
    fwall(F(4, 0, 22), F(36, 0, 22), H + 2, MAT_METAL_PANEL, vec3(0.22f, 0.22f, 0.25f));
    fwall(F(4, 0, 48), F(36, 0, 48), H + 2, MAT_METAL_PANEL, vec3(0.22f, 0.22f, 0.25f));
    fwall(F(36, 0, 22), F(36, 0, 48), H + 2, MAT_METAL_PANEL, vec3(0.22f, 0.22f, 0.25f));
    vec3 gc = F(21, 0, 35);
    MeshBuilder& mb = gen_chunk(gc);
    mb.set_mat(MAT_STEEL, MF_METAL, 0.8f);
    mb.set_color(vec3(0.18f, 0.18f, 0.2f));
    mb.cone_frustum(gc, 5.5f, 5.0f, 1.1f, 32, true);
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 6.0f);
    mb.set_color(vec3(0.95f, 0.65f, 0.25f));
    mb.cone_frustum(gc + vec3(0, 1.1f, 0), 5.05f, 5.0f, 0.04f, 32, false);
    world_add_box(gc - vec3(5.0f, 0, 5.0f), gc + vec3(5.0f, 1.1f, 5.0f), SURF_METAL, 1);
    gen_light(gc + vec3(0, 3.5f, 0), 16.0f, vec3(0.4f, 0.75f, 1.0f), 16.0f);
    panel_light(F(12, H + 1.9f, 30), vec3(1.0f, 0.75f, 0.4f), 5.0f, 10.0f);
    panel_light(F(30, H + 1.9f, 40), vec3(1.0f, 0.75f, 0.4f), 5.0f, 10.0f);
    // Wall screens
    for (int k = 0; k < 3; k++) gen_box(F(35.8f, 2.0f, 26 + k * 8.0f), F(35.9f, 4.5f, 31 + k * 8.0f), MAT_PAINT, vec3(0.3f, 0.6f, 1.0f), MF_EMISSIVE | MF_PULSE, 1.0f, 5.0f);
    set_lm(LM_AEGIS_BRIEFING, "Briefing Room", F(10, 0, 34), 4.0f, false);

    // Laboratory (west): x -40..-4, z 14..50
    fwall(F(-40, 0, 14), F(-4, 0, 14), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-40, 0, 50), F(-4, 0, 50), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-40, 0, 14), F(-40, 0, 50), H, MAT_METAL_PANEL, wallCol);
    for (int k = 0; k < 4; k++) {
        vec3 p = F(-34 + k * 7.0f, 0, 44);
        gen_cyl(p, 1.1f, 0.5f, 14, MAT_METAL_PANEL, vec3(0.3f, 0.3f, 0.33f), MF_METAL, 1.0f, 0, true);
        gen_cyl(p + vec3(0, 0.5f, 0), 0.9f, 3.0f, 14, MAT_PAINT, vec3(0.25f, 0.95f, 0.6f), MF_EMISSIVE | MF_GLASS | MF_PULSE, 0.1f, 4.0f);
        gen_cyl(p + vec3(0, 3.5f, 0), 1.1f, 0.5f, 14, MAT_METAL_PANEL, vec3(0.3f, 0.3f, 0.33f), MF_METAL);
        gen_ellipsoid(p + vec3(0, 2.0f, 0), vec3(0.35f, 0.6f, 0.3f), MAT_ALIEN, vec3(0.6f, 0.6f, 0.7f), MF_EMISSIVE | MF_PULSE, 0.4f, 2.0f);
        gen_light(p + vec3(0, 2.0f, -1.6f), 8.0f, vec3(0.3f, 1.0f, 0.6f), 8.0f);
    }
    // Scanner bed where the Shard is examined
    gen_solid(F(-26, 0, 22), F(-20, 1.0f, 26), MAT_STEEL, vec3(0.75f, 0.77f, 0.8f), SURF_METAL);
    gen_box(F(-25.5f, 2.8f, 21.5f), F(-20.5f, 3.0f, 26.5f), MAT_PAINT, vec3(0.5f, 0.9f, 1.0f), MF_EMISSIVE | MF_PULSE, 1.0f, 8.0f);
    gen_light(F(-23, 2.5f, 24), 10.0f, vec3(0.5f, 0.9f, 1.0f), 10.0f);
    for (int k = 0; k < 3; k++) panel_light(F(-30 + k * 10.0f, H - 0.1f, 30), white, 7.0f, 12.0f);
    set_lm(LM_AEGIS_LAB, "Xenotech Laboratory", F(-23, 0, 28), 3.0f, false);

    // Training range (east): x 4..48, z -36..2
    fwall(F(4, 0, -36), F(48, 0, -36), H, MAT_CONCRETE, vec3(0.4f, 0.4f, 0.4f));
    fwall(F(4, 0, 2), F(48, 0, 2), H, MAT_CONCRETE, vec3(0.4f, 0.4f, 0.4f));
    fwall(F(48, 0, -36), F(48, 0, 2), H, MAT_CONCRETE, vec3(0.4f, 0.4f, 0.4f));
    for (int k = 0; k < 4; k++) {
        float z = -30 + k * 8.0f;
        gen_box(F(10, 0, z - 0.1f), F(14, 1.1f, z + 0.1f), MAT_METAL_PANEL, vec3(0.3f, 0.3f, 0.32f), MF_METAL);
        world_add_box(F(10, 0, z - 0.1f), F(14, 1.1f, z + 0.1f), SURF_METAL, 0);
        gen_box(F(10, 0.01f, z - 3.8f), F(46, 0.03f, z - 3.7f), MAT_PAINT, vec3(0.9f, 0.7f, 0.2f), MF_GROUND);
    }
    for (int k = 0; k < 4; k++) panel_light(F(26, H - 0.1f, -30 + k * 9.0f), white, 8.0f, 14.0f);
    set_lm(LM_AEGIS_RANGE, "Training Range", F(9, 0, -10), 3.0f, false);

    // Armory (west): x -30..-4, z -20..8
    fwall(F(-30, 0, -20), F(-4, 0, -20), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-30, 0, 8), F(-4, 0, 8), H, MAT_METAL_PANEL, wallCol);
    fwall(F(-30, 0, -20), F(-30, 0, 8), H, MAT_METAL_PANEL, wallCol);
    for (int k = 0; k < 5; k++) {
        gen_solid(F(-29.5f, 0, -18 + k * 5.0f), F(-28.5f, 2.6f, -15 + k * 5.0f), MAT_METAL_PANEL, vec3(0.2f, 0.22f, 0.24f), SURF_METAL);
        gen_box(F(-28.49f, 1.2f, -17.5f + k * 5.0f), F(-28.45f, 1.4f, -15.5f + k * 5.0f), MAT_PAINT, vec3(0.95f, 0.7f, 0.2f), MF_EMISSIVE, 1.0f, 6.0f);
    }
    panel_light(F(-17, H - 0.1f, -6), white, 8.0f, 14.0f);

    // Archive / server cave (north-west): x -42..-4, z -95..-45  (rock walls, blue monitors)
    fwall(F(-42, 0, -45), F(-12, 0, -45), H, MAT_DIRT, vec3(0.32f, 0.3f, 0.32f));
    fwall(F(-42, 0, -95), F(-4, 0, -95), H, MAT_DIRT, vec3(0.32f, 0.3f, 0.32f));
    fwall(F(-42, 0, -95), F(-42, 0, -45), H, MAT_DIRT, vec3(0.32f, 0.3f, 0.32f));
    fwall(F(-4, 0, -95), F(-4, 0, -60), H, MAT_DIRT, vec3(0.32f, 0.3f, 0.32f));
    Rng rr;
    rr.seed(4242);
    for (int k = 0; k < 14; k++) {
        vec3 p = F(rr.range(-41, -6), rr.range(0, 4), rr.range(-94, -47));
        gen_ellipsoid(p, vec3(rr.range(1, 3), rr.range(1, 3), rr.range(1, 3)), MAT_DIRT, vec3(0.3f, 0.28f, 0.3f));
    }
    for (int k = 0; k < 6; k++) {
        vec3 d = F(-36 + k * 5.0f, 0, -80);
        gen_solid(d, d + vec3(3.5f, 0.9f, 1.2f), MAT_STEEL, vec3(0.12f, 0.13f, 0.15f), SURF_METAL);
        for (int m = 0; m < 2; m++) {
            vec3 sp = d + vec3(0.4f + m * 1.6f, 0.95f, 0.9f);
            gen_box(sp, sp + vec3(1.2f, 0.8f, 0.05f), MAT_PAINT, vec3(0.35f, 0.65f, 1.0f), MF_EMISSIVE | MF_PULSE, 1.0f, 6.0f);
        }
        gen_light(d + vec3(1.7f, 1.4f, 2.0f), 7.0f, vec3(0.35f, 0.6f, 1.0f), 6.0f);
    }
    for (int k = 0; k < 4; k++) gen_box(F(-40 + k * 9.0f, 0, -93), F(-34 + k * 9.0f, 4.5f, -91), MAT_STEEL, vec3(0.1f, 0.1f, 0.12f), MF_METAL);
    vec3 skylight = F(-22, H + 3.5f, -70);
    gen_cyl(skylight, 3.0f, 0.2f, 20, MAT_PAINT, vec3(0.6f, 0.75f, 1.0f), MF_EMISSIVE, 1.0f, 10.0f);
    gen_light(skylight - vec3(0, 1.5f, 0), 16.0f, vec3(0.5f, 0.7f, 1.0f), 10.0f);
    gen_beam(skylight, skylight - vec3(0, 9.0f, 0), 2.6f, 4.0f, vec3(0.5f, 0.7f, 1.0f), 0.25f);
    set_lm(LM_AEGIS_ARCHIVE, "Archive Vault", F(-20, 0, -76), 3.0f, false);

    // Restricted reactor shaft (Project LANTERN): x 4..48, z -98..-45
    fwall(F(4, 0, -45), F(20, 0, -45), H, MAT_METAL_PANEL, vec3(0.25f, 0.2f, 0.2f));
    fwall(F(28, 0, -45), F(48, 0, -45), H, MAT_METAL_PANEL, vec3(0.25f, 0.2f, 0.2f));
    fwall(F(48, 0, -98), F(48, 0, -45), H, MAT_METAL_PANEL, vec3(0.25f, 0.2f, 0.2f));
    fwall(F(4, 0, -98), F(48, 0, -98), H, MAT_METAL_PANEL, vec3(0.25f, 0.2f, 0.2f));
    fwall(F(4, 0, -98), F(4, 0, -45), H, MAT_METAL_PANEL, vec3(0.25f, 0.2f, 0.2f));
    // Corridor end wall with the restricted door to the reactor (via x 20..28 gap on z=-45)
    fwall(F(-4, 0, -45), F(-4, 0, -45), H, MAT_METAL_PANEL, wallCol);
    vec3 rc = F(26, 0, -72);
    for (int k = 0; k < 4; k++) {
        float r0 = 6.0f - k * 1.2f;
        gen_cyl(rc + vec3(0, k * 2.2f, 0), r0, 2.2f, 20, MAT_METAL_PANEL, vec3(0.2f, 0.2f, 0.22f), MF_METAL, 0.8f, 0, k == 0);
        gen_cyl(rc + vec3(0, k * 2.2f + 2.0f, 0), r0 + 0.08f, 0.12f, 20, MAT_PAINT, vec3(1.0f, 0.12f, 0.08f), MF_EMISSIVE | MF_PULSE, 1.0f, 12.0f);
    }
    for (int k = 0; k < 10; k++) {
        float a = TAU_F * k / 10.0f;
        vec3 o(sinf(a), 0, cosf(a));
        gen_tube(rc + o * 3.0f + vec3(0, 7.5f, 0), rc + o * 20.0f + vec3(0, 9.0f, 0), 0.25f, 6, MAT_RUBBER, vec3(0.15f, 0.15f, 0.15f));
        gen_tube(rc + o * 20.0f + vec3(0, 9.0f, 0), rc + o * 20.0f, 0.25f, 6, MAT_RUBBER, vec3(0.15f, 0.15f, 0.15f));
    }
    gen_ellipsoid(rc + vec3(-1.5f, 4.0f, -6.0f), vec3(0.35f, 0.35f, 0.35f), MAT_PAINT, vec3(1.0f, 0.1f, 0.05f), MF_EMISSIVE, 1.0f, 25.0f);
    gen_ellipsoid(rc + vec3(1.5f, 4.0f, -6.0f), vec3(0.35f, 0.35f, 0.35f), MAT_PAINT, vec3(1.0f, 0.1f, 0.05f), MF_EMISSIVE, 1.0f, 25.0f);
    gen_light(rc + vec3(0, 5.0f, -8.0f), 26.0f, vec3(1.0f, 0.15f, 0.08f), 26.0f);
    gen_light(rc + vec3(0, 9.0f, 8.0f), 24.0f, vec3(0.3f, 0.6f, 0.7f), 10.0f);
    set_lm(LM_AEGIS_REACTOR, "Project LANTERN", F(24, 0, -50), 4.0f, false);
}

// ---------------------------------------------------------------------------
// Storm drain tunnel and alien nest
// ---------------------------------------------------------------------------
static vec3 D(float x, float y, float z) { return vec3(DRAIN_ORIGIN_X + x, DRAIN_ORIGIN_Y + y, DRAIN_ORIGIN_Z + z); }

void gen_drain_tunnel() {
    vec3 conc(0.38f, 0.38f, 0.36f);
    // Tunnel along +Z from z=-90 to z=30, 8 wide, 6 tall
    for (int s = 0; s < 6; s++) {
        float z0 = -90 + s * 20.0f, z1 = z0 + 20.0f;
        gen_solid(D(-4, -1, z0), D(-1.2f, 0, z1), MAT_FLOOR_CONCRETE, conc, SURF_CONCRETE);
        gen_solid(D(1.2f, -1, z0), D(4, 0, z1), MAT_FLOOR_CONCRETE, conc, SURF_CONCRETE);
        gen_solid(D(-1.2f, -1.5f, z0), D(1.2f, -0.4f, z1), MAT_FLOOR_CONCRETE, conc * 0.8f, SURF_CONCRETE);
        gen_box(D(-1.2f, -0.42f, z0), D(1.2f, -0.38f, z1), MAT_PAINT, vec3(0.06f, 0.08f, 0.07f), MF_GLASS | MF_GROUND, 0.05f);
        gen_solid(D(-5, -1, z0), D(-4, 6, z1), MAT_CONCRETE, conc, SURF_CONCRETE);
        gen_solid(D(4, -1, z0), D(5, 6, z1), MAT_CONCRETE, conc, SURF_CONCRETE);
        gen_solid(D(-5, 6, z0), D(5, 7, z1), MAT_CONCRETE, conc * 0.8f, SURF_CONCRETE);
        // Ribs
        gen_box(D(-4, 0, z0 + 9.5f), D(-3.6f, 6, z0 + 10.5f), MAT_CONCRETE, conc * 0.7f);
        gen_box(D(3.6f, 0, z0 + 9.5f), D(4, 6, z0 + 10.5f), MAT_CONCRETE, conc * 0.7f);
        gen_box(D(-4, 5.6f, z0 + 9.5f), D(4, 6, z0 + 10.5f), MAT_CONCRETE, conc * 0.7f);
        // Caged maintenance lamps (some broken)
        if (s % 2 == 0) {
            vec3 lp = D(-3.7f, 4.5f, z0 + 5);
            gen_box(lp - vec3(0.1f, 0.2f, 0.3f), lp + vec3(0.1f, 0.2f, 0.3f), MAT_PAINT, vec3(1.0f, 0.75f, 0.4f), MF_EMISSIVE, 1.0f, 14.0f);
            gen_light(lp + vec3(1.0f, -0.5f, 0), 12.0f, vec3(1.0f, 0.7f, 0.35f), 8.0f);
        }
        // Alien growth creeping in towards the nest
        if (s >= 3) {
            Rng r2;
            r2.seed(900 + s);
            for (int k = 0; k < 6; k++) {
                vec3 p = D(r2.chance(0.5f) ? -3.8f : 3.8f, r2.range(0, 5), r2.range(z0, z1));
                gen_ellipsoid(p, vec3(0.5f, r2.range(0.6f, 1.8f), 0.6f), MAT_ALIEN, vec3(0.8f, 0.75f, 0.9f), MF_EMISSIVE | MF_PULSE, 0.4f, 0.8f);
            }
        }
    }
    // Back wall + ladder up to the hatch
    gen_solid(D(-5, -1, -92), D(5, 7, -90), MAT_CONCRETE, conc, SURF_CONCRETE);
    for (int k = 0; k < 10; k++) gen_box(D(-0.5f, 0.3f + k * 0.55f, -89.95f), D(0.5f, 0.35f + k * 0.55f, -89.7f), MAT_STEEL, vec3(0.5f, 0.45f, 0.4f), MF_METAL);
    gen_light(D(0, 5.0f, -86), 10.0f, vec3(0.9f, 0.95f, 1.0f), 6.0f);
    gen_indoor(D(-5, -2, -92), D(5, 8, 30), 0.08f);

    // Nest chamber: z 30..90, x -30..30, high ceiling
    vec3 nm = D(-30, -1, 30), nx = D(30, 18, 92);
    gen_box(vec3(nm.x, nm.y, nm.z), vec3(nx.x, nm.y + 1.0f, nx.z), MAT_ALIEN, vec3(0.55f, 0.5f, 0.6f), MF_GROUND, 0.6f);
    world_add_box(vec3(nm.x, nm.y, nm.z), vec3(nx.x, nm.y + 1.0f, nx.z), SURF_ALIEN, 1);
    gen_solid(vec3(nm.x, nm.y, nm.z - 1), vec3(-4 + DRAIN_ORIGIN_X, nx.y, nm.z), MAT_CONCRETE, conc, SURF_CONCRETE);
    gen_solid(vec3(4 + DRAIN_ORIGIN_X, nm.y, nm.z - 1), vec3(nx.x, nx.y, nm.z), MAT_CONCRETE, conc, SURF_CONCRETE);
    gen_solid(vec3(-4 + DRAIN_ORIGIN_X, nm.y + 7, nm.z - 1), vec3(4 + DRAIN_ORIGIN_X, nx.y, nm.z), MAT_CONCRETE, conc, SURF_CONCRETE);
    gen_solid(vec3(nm.x, nm.y, nx.z), vec3(nx.x, nx.y, nx.z + 1), MAT_ALIEN, vec3(0.5f, 0.45f, 0.55f), SURF_ALIEN);
    gen_solid(vec3(nm.x - 1, nm.y, nm.z), vec3(nm.x, nx.y, nx.z), MAT_ALIEN, vec3(0.5f, 0.45f, 0.55f), SURF_ALIEN);
    gen_solid(vec3(nx.x, nm.y, nm.z), vec3(nx.x + 1, nx.y, nx.z), MAT_ALIEN, vec3(0.5f, 0.45f, 0.55f), SURF_ALIEN);
    gen_solid(vec3(nm.x, nx.y, nm.z), vec3(nx.x, nx.y + 1, nx.z), MAT_ALIEN, vec3(0.4f, 0.35f, 0.45f), SURF_ALIEN);
    // Central alien pylon (like a buried machine)
    vec3 pc = D(0, 0, 66);
    for (int k = 0; k < 5; k++) {
        float r0 = 5.0f - k * 0.8f;
        gen_cyl(pc + vec3(0, k * 2.8f, 0), r0, 2.8f, 16, MAT_ALIEN, vec3(0.35f, 0.32f, 0.4f), 0, 0.5f, 0, k == 0);
        gen_cyl(pc + vec3(0, k * 2.8f + 2.6f, 0), r0 + 0.1f, 0.15f, 16, MAT_PAINT, vec3(0.2f, 1.0f, 0.6f), MF_EMISSIVE | MF_PULSE, 1.0f, 10.0f);
    }
    gen_ellipsoid(pc + vec3(0, 15.0f, 0), vec3(1.2f, 1.2f, 1.2f), MAT_PAINT, vec3(0.3f, 1.0f, 0.7f), MF_EMISSIVE | MF_PULSE, 1.0f, 30.0f);
    gen_light(pc + vec3(0, 12.0f, 0), 40.0f, vec3(0.2f, 1.0f, 0.55f), 40.0f);
    for (int k = 0; k < 12; k++) {
        float a = TAU_F * k / 12.0f;
        vec3 o(sinf(a), 0, cosf(a));
        gen_tube(pc + o * 2.0f + vec3(0, 12.0f, 0), pc + o * 26.0f + vec3(0, 16.0f, 0), 0.4f, 6, MAT_ALIEN, vec3(0.45f, 0.4f, 0.5f));
    }
    // Egg pods
    Rng r3;
    r3.seed(3131);
    for (int k = 0; k < 18; k++) {
        vec3 p = D(r3.range(-26, 26), 0, r3.range(36, 88));
        if (length(p - pc) < 9.0f) continue;
        gen_ellipsoid(p + vec3(0, 0.9f, 0), vec3(0.7f, 1.0f, 0.7f), MAT_ALIEN, vec3(0.7f, 0.9f, 0.7f), MF_EMISSIVE | MF_PULSE, 0.3f, 1.5f);
        world_add_box(p - vec3(0.5f, 0, 0.5f), p + vec3(0.5f, 1.8f, 0.5f), SURF_ALIEN, 1);
        if (k % 3 == 0) gen_light(p + vec3(0, 1.0f, 0), 7.0f, vec3(0.3f, 1.0f, 0.5f), 4.0f);
    }
    gen_indoor(nm - vec3(0, 1, 0), nx, 0.1f);
    set_lm(LM_DRAIN_NEST, "The Nest", D(0, 0, 50), 8.0f, false);
}

void world_set_power(bool on) {
    for (int i = 0; i < g_world.streetLightIds.count; i++) {
        Light l = g_world.streetLightOrig[i];
        if (!on) l.radius = 0;
        renderer_set_static_light(g_world.streetLightIds[i], l);
    }
    renderer_set_static_beams_enabled(on);
}

void world_set_phase_visuals(int phase) {
    world_set_power(phase != 2);
}
