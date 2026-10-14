// FIGHTO - skeleton, procedural humanoid/alien meshes, animation sampling.
#include "character.h"
#include "game.h"
#include "../platform/platform.h"

static const int g_parents[B_COUNT] = {-1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0, 11, 12, 0, 14, 15};
static const char* g_boneNames[B_COUNT] = {"pelvis", "spine", "chest", "neck", "head", "lua", "lfa", "lh", "rua", "rfa", "rh", "lth", "lsh", "lft", "rth", "rsh", "rft"};

// Human rest pose for a 1.80 m character facing +Z (left side = +X)
static const vec3 g_humanRest[B_COUNT] = {
    vec3(0, 0.98f, 0), vec3(0, 1.10f, 0), vec3(0, 1.30f, 0), vec3(0, 1.50f, -0.01f), vec3(0, 1.60f, 0.0f),
    vec3(0.185f, 1.44f, -0.01f), vec3(0.215f, 1.16f, -0.02f), vec3(0.23f, 0.90f, 0.0f),
    vec3(-0.185f, 1.44f, -0.01f), vec3(-0.215f, 1.16f, -0.02f), vec3(-0.23f, 0.90f, 0.0f),
    vec3(0.095f, 0.94f, 0), vec3(0.1f, 0.52f, 0.01f), vec3(0.1f, 0.09f, -0.03f),
    vec3(-0.095f, 0.94f, 0), vec3(-0.1f, 0.52f, 0.01f), vec3(-0.1f, 0.09f, -0.03f),
};

void skeleton_build(Skeleton* s, int body, float height, float build) {
    float hs = height / 1.8f;
    for (int i = 0; i < B_COUNT; i++) {
        vec3 p = g_humanRest[i];
        s->parent[i] = g_parents[i];
        if (body == BODY_STALKER || body == BODY_OVERSEER) {
            // Long limbs, narrow torso, long neck
            if (i >= B_L_THIGH) p.y = p.y * 1.12f;
            if (i == B_PELVIS || i == B_SPINE || i == B_CHEST || i == B_NECK || i == B_HEAD) p.y = p.y * 1.1f + 0.02f;
            if (i == B_L_FOREARM || i == B_R_FOREARM) p.y -= 0.1f;
            if (i == B_L_HAND || i == B_R_HAND) p.y -= 0.32f;
            if (i == B_L_UPPERARM || i == B_R_UPPERARM) p.y += 0.06f;
            if (i == B_NECK) p.y += 0.04f;
            if (i == B_HEAD) p.y += 0.1f;
            p.x *= 0.9f;
        } else if (body == BODY_BRUTE) {
            if (i == B_L_UPPERARM || i == B_R_UPPERARM) p.x *= 1.45f;
            if (i == B_L_FOREARM || i == B_R_FOREARM || i == B_L_HAND || i == B_R_HAND) p.x *= 1.55f;
            if (i == B_L_HAND || i == B_R_HAND) p.y -= 0.15f;
            if (i >= B_L_THIGH) p.x *= 1.4f;
        }
        p.x *= build * hs;
        p.y *= hs;
        p.z *= hs;
        s->restPos[i] = p;
    }
    s->scale = hs;
}

// ---------------------------------------------------------------------------
// Animation clips
// ---------------------------------------------------------------------------
static AnimClip g_clips[CLIP_COUNT + 16];
static int g_clipCount;
static const char* g_clipIdNames[CLIP_COUNT] = {
    "idle", "fight_idle", "jab", "cross", "hook", "uppercut", "kick", "haymaker", "block", "dodge", "hit_front", "hit_head",
    "stagger", "knockdown", "getup", "death_back", "death_forward", "jump", "fall", "aim_pistol", "aim_rifle", "reload",
    "drive", "interact", "takedown", "point", "cower", "surrender", "claw", "leap", "screech", "talk", "stomp",
    "dead_back", "dead_front", "elbow", "sweep", "throw",
};
static int g_clipMap[CLIP_COUNT];

static int bone_from_name(const char* n) {
    for (int i = 0; i < B_COUNT; i++)
        if (str_eq(n, g_boneNames[i])) return i;
    return -1;
}

static const char* next_tok(const char* p, char* out, int cap) {
    while (*p == ' ' || *p == '\t' || *p == '|') p++;
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '|') {
        if (n < cap - 1) out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return p;
}

int anim_find(const char* name) {
    for (int i = 0; i < g_clipCount; i++)
        if (str_eq(g_clips[i].name, name)) return i;
    return -1;
}

bool anim_load_clips() {
    Array<u8> data = {};
    if (!plat_read_asset("animations/humanoid.anim", &data)) {
        log_error("Missing animations/humanoid.anim");
        return false;
    }
    g_clipCount = 0;
    AnimClip* cur = 0;
    const char* p = (const char*)data.data;
    char line[2048];
    while (*p) {
        int n = 0;
        while (*p && *p != '\n') { if (n < (int)sizeof(line) - 1) line[n++] = *p; p++; }
        if (*p == '\n') p++;
        line[n] = 0;
        char tok[64];
        const char* q = next_tok(line, tok, sizeof(tok));
        if (tok[0] == 0 || tok[0] == '#') continue;
        if (str_eq(tok, "clip")) {
            if (g_clipCount >= ARRAY_COUNT(g_clips)) break;
            cur = &g_clips[g_clipCount++];
            memset(cur, 0, sizeof(*cur));
            q = next_tok(q, tok, sizeof(tok));
            str_copy(cur->name, sizeof(cur->name), tok);
            q = next_tok(q, tok, sizeof(tok));
            cur->length = str_to_float(tok);
            cur->hitStart = cur->hitEnd = -1;
            for (;;) {
                q = next_tok(q, tok, sizeof(tok));
                if (!tok[0]) break;
                if (str_eq(tok, "loop")) cur->loop = true;
                else if (str_eq(tok, "hit")) {
                    q = next_tok(q, tok, sizeof(tok)); cur->hitStart = str_to_float(tok);
                    q = next_tok(q, tok, sizeof(tok)); cur->hitEnd = str_to_float(tok);
                }
            }
        } else if (str_eq(tok, "key") && cur && cur->keyCount < MAX_CLIP_KEYS) {
            ClipKey& k = cur->keys[cur->keyCount++];
            memset(&k, 0, sizeof(k));
            q = next_tok(q, tok, sizeof(tok));
            k.t = str_to_float(tok);
            for (;;) {
                q = next_tok(q, tok, sizeof(tok));
                if (!tok[0]) break;
                char a[32], b[32], c[32];
                q = next_tok(q, a, sizeof(a));
                q = next_tok(q, b, sizeof(b));
                q = next_tok(q, c, sizeof(c));
                vec3 v(str_to_float(a), str_to_float(b), str_to_float(c));
                if (str_eq(tok, "root")) { k.root = v; continue; }
                int bi = bone_from_name(tok);
                if (bi >= 0) k.euler[bi] = v;
                else log_error("Unknown bone '%s' in clip %s", tok, cur->name);
            }
        }
    }
    data.free_mem();
    for (int i = 0; i < CLIP_COUNT; i++) {
        g_clipMap[i] = anim_find(g_clipIdNames[i]);
        if (g_clipMap[i] < 0) log_error("Animation clip '%s' missing", g_clipIdNames[i]);
    }
    log_info("Loaded %d animation clips", g_clipCount);
    return true;
}

const AnimClip* anim_clip(int id) {
    if (id < 0 || id >= CLIP_COUNT || g_clipMap[id] < 0) return 0;
    return &g_clips[g_clipMap[id]];
}

void pose_identity(Pose* p) {
    for (int i = 0; i < B_COUNT; i++) p->rot[i] = quat_identity();
    p->root = vec3(0, 0, 0);
}

static quat euler_deg(vec3 e) { return quat_euler(e.x * DEG2RAD, e.y * DEG2RAD, e.z * DEG2RAD); }

void pose_sample_clip(Pose* out, const AnimClip* c, float t) {
    pose_identity(out);
    if (!c || c->keyCount == 0) return;
    if (c->loop && c->length > 0) { t = fmodf(t, c->length); if (t < 0) t += c->length; }
    t = clampf(t, 0, c->length);
    int k1 = 0;
    while (k1 < c->keyCount && c->keys[k1].t < t) k1++;
    if (k1 == 0) {
        for (int b = 0; b < B_COUNT; b++) out->rot[b] = euler_deg(c->keys[0].euler[b]);
        out->root = c->keys[0].root;
        return;
    }
    if (k1 >= c->keyCount) {
        const ClipKey& k = c->keys[c->keyCount - 1];
        for (int b = 0; b < B_COUNT; b++) out->rot[b] = euler_deg(k.euler[b]);
        out->root = k.root;
        return;
    }
    const ClipKey& a = c->keys[k1 - 1];
    const ClipKey& b = c->keys[k1];
    float u = (t - a.t) / fmaxf(b.t - a.t, 1e-4f);
    u = u * u * (3 - 2 * u);
    for (int i = 0; i < B_COUNT; i++) out->rot[i] = euler_deg(lerp(a.euler[i], b.euler[i], u));
    out->root = lerp(a.root, b.root, u);
}

void pose_blend(Pose* out, const Pose& a, const Pose& b, float w) {
    for (int i = 0; i < B_COUNT; i++) out->rot[i] = quat_slerp(a.rot[i], b.rot[i], w);
    out->root = lerp(a.root, b.root, w);
}

void pose_blend_upper(Pose* out, const Pose& a, const Pose& b, float w) {
    for (int i = 0; i < B_COUNT; i++) {
        bool upper = (i >= B_SPINE && i <= B_R_HAND);
        float ww = upper ? w : 0.0f;
        if (i == B_SPINE) ww *= 0.5f;
        out->rot[i] = quat_slerp(a.rot[i], b.rot[i], ww);
    }
    out->root = a.root;
}

void pose_add(Pose* p, int bone, vec3 e) { p->rot[bone] = p->rot[bone] * euler_deg(e); }

void pose_locomotion(Pose* out, float phase, float speed, float crouch, float lean, int body) {
    pose_identity(out);
    float w = saturate(speed / 1.6f);
    float r = saturate((speed - 2.0f) / 3.0f);
    float s = saturate((speed - 5.0f) / 2.5f);
    float t = phase * TAU_F;
    float st = sinf(t), ct = cosf(t);
    float legA = lerpf(24.0f, 42.0f, r) + 10.0f * s;
    float kneeA = lerpf(38.0f, 85.0f, r) + 15.0f * s;
    float armA = lerpf(18.0f, 45.0f, r) + 10.0f * s;
    vec3 e[B_COUNT];
    memset(e, 0, sizeof(e));
    e[B_L_THIGH].x = -st * legA * w;
    e[B_R_THIGH].x = st * legA * w;
    e[B_L_SHIN].x = (fmaxf(0.0f, ct) * kneeA + 6.0f) * w;
    e[B_R_SHIN].x = (fmaxf(0.0f, -ct) * kneeA + 6.0f) * w;
    e[B_L_FOOT].x = (-e[B_L_THIGH].x - e[B_L_SHIN].x) * 0.6f * w + (st > 0 ? 0.0f : -st * 15.0f * w);
    e[B_R_FOOT].x = (-e[B_R_THIGH].x - e[B_R_SHIN].x) * 0.6f * w + (st < 0 ? 0.0f : st * 15.0f * w);
    e[B_L_UPPERARM].x = st * armA * w;
    e[B_R_UPPERARM].x = -st * armA * w;
    e[B_L_UPPERARM].z = 5.0f + 4.0f * r;
    e[B_R_UPPERARM].z = -5.0f - 4.0f * r;
    float elbow = 12.0f + 55.0f * r + 20.0f * s;
    e[B_L_FOREARM].x = -elbow - fmaxf(0.0f, -st) * 20.0f * r;
    e[B_R_FOREARM].x = -elbow - fmaxf(0.0f, st) * 20.0f * r;
    e[B_CHEST].y = st * 7.0f * w;
    e[B_PELVIS].y = -st * 5.0f * w;
    e[B_SPINE].x = 3.0f * w + 6.0f * r + 9.0f * s + lean;
    e[B_CHEST].x = 2.0f * r;
    e[B_NECK].x = -(3.0f * w + 6.0f * r + 9.0f * s) * 0.6f;
    out->root.y = (fabsf(ct) - 0.6f) * 0.045f * (w * 0.5f + r);
    // Crouch / stealth
    if (crouch > 0) {
        out->root.y -= 0.38f * crouch;
        e[B_L_THIGH].x += -55.0f * crouch;
        e[B_R_THIGH].x += -55.0f * crouch;
        e[B_L_SHIN].x += 85.0f * crouch;
        e[B_R_SHIN].x += 85.0f * crouch;
        e[B_L_FOOT].x += -30.0f * crouch;
        e[B_R_FOOT].x += -30.0f * crouch;
        e[B_SPINE].x += 18.0f * crouch;
        e[B_NECK].x -= 12.0f * crouch;
        e[B_L_UPPERARM].x -= 15.0f * crouch;
        e[B_R_UPPERARM].x -= 15.0f * crouch;
    }
    if (body == BODY_STALKER || body == BODY_TENDRIL) {
        e[B_SPINE].x += 20.0f;
        e[B_CHEST].x += 14.0f;
        e[B_NECK].x -= 25.0f;
        e[B_HEAD].x -= 10.0f;
        e[B_L_UPPERARM].x -= 20.0f;
        e[B_R_UPPERARM].x -= 20.0f;
        e[B_L_UPPERARM].z += 10.0f;
        e[B_R_UPPERARM].z -= 10.0f;
        e[B_L_SHIN].x += 20.0f;
        e[B_R_SHIN].x += 20.0f;
        e[B_L_THIGH].x -= 15.0f;
        e[B_R_THIGH].x -= 15.0f;
        out->root.y -= 0.12f;
    } else if (body == BODY_BRUTE) {
        e[B_L_UPPERARM].z += 14.0f;
        e[B_R_UPPERARM].z -= 14.0f;
        e[B_SPINE].x += 10.0f;
    }
    for (int i = 0; i < B_COUNT; i++) out->rot[i] = euler_deg(e[i]);
}

void pose_compute(const Skeleton& s, const Pose& p, mat4* globals, mat4* skin) {
    for (int i = 0; i < B_COUNT; i++) {
        int par = s.parent[i];
        vec3 off = par < 0 ? s.restPos[i] + p.root * s.scale : s.restPos[i] - s.restPos[par];
        mat4 local = mat4_trs(off, p.rot[i], vec3(1, 1, 1));
        globals[i] = par < 0 ? local : globals[par] * local;
        if (skin) skin[i] = globals[i] * mat4_translate(-s.restPos[i]);
    }
}

vec3 joint_world(const mat4& model, const mat4* globals, int bone, vec3 off) {
    return transform_point(model, transform_point(globals[bone], off));
}

// ---------------------------------------------------------------------------
// Procedural body mesh
// ---------------------------------------------------------------------------
struct Part {
    int layer;
    int flags;
    float rough;
    vec3 color;
    float emissive;
};

static void use(MeshBuilder* mb, const Part& p) {
    mb->set_mat(p.layer, p.flags | MF_CHARACTER, p.rough, p.emissive);
    mb->set_color(p.color);
}

// Tapered limb with ring based skin weights (parent blend at the start, child blend at the end)
static void limb(MeshBuilder* mb, vec3 a, vec3 b, float ra, float rb, int parentBone, int bone, int childBone, int segs, float flatten = 1.0f) {
    vec3 axis = b - a;
    float len = length(axis);
    if (len < 1e-4f) return;
    vec3 dir = axis / len;
    vec3 ref = fabsf(dir.y) < 0.9f ? vec3(0, 1, 0) : vec3(0, 0, 1);
    vec3 sx = normalize(cross(dir, ref));
    vec3 sy = cross(sx, dir);
    const int rings = 7;
    u32 base = (u32)mb->verts.count;
    for (int k = 0; k < rings; k++) {
        float t = (float)k / (rings - 1);
        // Slight muscle bulge in the upper third
        float bulge = 1.0f + 0.12f * sinf(t * PI_F) * (1.0f - t * 0.5f);
        float r = lerpf(ra, rb, t) * bulge;
        vec3 c = a + axis * t;
        if (t < 0.2f && parentBone >= 0) { mb->bone = (u8)parentBone; mb->bone2 = (u8)bone; mb->boneBlend = 0.5f + t * 2.5f; }
        else if (t > 0.8f && childBone >= 0 && childBone != bone) { mb->bone = (u8)bone; mb->bone2 = (u8)childBone; mb->boneBlend = (t - 0.8f) * 2.5f; }
        else { mb->bone = mb->bone2 = (u8)bone; mb->boneBlend = 0; }
        for (int i = 0; i <= segs; i++) {
            float th = TAU_F * i / segs;
            vec3 d = sx * cosf(th) * flatten + sy * sinf(th);
            mb->vert(c + d * r, normalize(d), vec2((float)i / segs * TAU_F * r, t * len));
        }
    }
    for (int k = 0; k < rings - 1; k++)
        for (int i = 0; i < segs; i++) {
            u32 i0 = base + (u32)(k * (segs + 1) + i);
            u32 i1 = i0 + 1, j0 = i0 + (u32)(segs + 1), j1 = j0 + 1;
            mb->tri(i0, i1, j1);
            mb->tri(i0, j1, j0);
        }
    mb->set_bone(bone);
}

static void blob(MeshBuilder* mb, vec3 c, vec3 r, int bone, int seg = 10, int rings = 7) {
    mb->set_bone(bone);
    mb->ellipsoid(c, r, seg, rings);
}

// Torso loft: rings of ellipses from hips to shoulders, front opening coloured as shirt.
static void torso(MeshBuilder* mb, const Skeleton& s, const Appearance& a, const Part& top, const Part& shirt, const Part& pants, float bw) {
    struct Ring { float y; float w; float d; float zoff; int b0, b1; float blend; int region; };
    float sc = s.scale;
    Ring rings[] = {
        {0.86f, 0.165f, 0.11f, 0.0f, B_PELVIS, B_PELVIS, 0, 0},
        {0.97f, 0.175f, 0.115f, 0.0f, B_PELVIS, B_PELVIS, 0, 0},
        {1.04f, 0.16f, 0.105f, 0.0f, B_PELVIS, B_SPINE, 0.5f, 1},
        {1.13f, 0.155f, 0.10f, 0.0f, B_SPINE, B_SPINE, 0, 1},
        {1.24f, 0.17f, 0.11f, 0.01f, B_SPINE, B_CHEST, 0.5f, 1},
        {1.34f, 0.185f, 0.12f, 0.015f, B_CHEST, B_CHEST, 0, 1},
        {1.42f, 0.195f, 0.11f, 0.0f, B_CHEST, B_CHEST, 0, 1},
        {1.48f, 0.15f, 0.085f, -0.01f, B_CHEST, B_CHEST, 0, 1},
        {1.52f, 0.07f, 0.06f, -0.01f, B_CHEST, B_NECK, 0.4f, 2},
    };
    const int NR = ARRAY_COUNT(rings);
    const int seg = 24;
    u32 base = (u32)mb->verts.count;
    bool brute = a.body == BODY_BRUTE;
    for (int k = 0; k < NR; k++) {
        Ring rg = rings[k];
        float w = rg.w * bw * (brute && k > 3 ? 1.5f : 1.0f);
        float d = rg.d * (brute ? 1.3f : 1.0f) * (0.92f + bw * 0.08f);
        mb->bone = (u8)rg.b0;
        mb->bone2 = (u8)rg.b1;
        mb->boneBlend = rg.blend;
        for (int i = 0; i <= seg; i++) {
            float th = TAU_F * i / seg;
            float cx = sinf(th), cz = cosf(th);
            // Flatter back, rounder front
            float dd = cz > 0 ? d * (1.0f + 0.1f * cz) : d * 0.9f;
            vec3 p(cx * w, rg.y * sc, cz * dd + rg.zoff);
            p.x *= sc / (s.scale > 0 ? s.scale : 1.0f) * sc;
            p.z *= sc;
            vec3 n = normalize(vec3(cx / w, 0, cz / dd));
            // Region colouring
            bool front = cz > 0.82f;
            if (rg.region == 0) use(mb, pants);
            else if ((a.top == TOP_JACKET || a.top == TOP_COAT || a.top == TOP_LABCOAT || a.top == TOP_SUIT) && front && k >= 3) use(mb, shirt);
            else use(mb, top);
            mb->vert(p, n, vec2(th * w, p.y));
        }
    }
    for (int k = 0; k < NR - 1; k++)
        for (int i = 0; i < seg; i++) {
            u32 i0 = base + (u32)(k * (seg + 1) + i);
            u32 i1 = i0 + 1, j0 = i0 + (u32)(seg + 1), j1 = j0 + 1;
            mb->tri(i0, i1, j1);
            mb->tri(i0, j1, j0);
        }
    // Cap the bottom
    use(mb, pants);
    blob(mb, vec3(0, 0.86f * sc, 0), vec3(0.16f * bw * sc, 0.06f * sc, 0.1f * sc), B_PELVIS, 12, 6);
}

void character_build_mesh(MeshBuilder* mb, const Skeleton& s, const Appearance& a) {
    const vec3* J = s.restPos;
    float sc = s.scale;
    float bw = a.build;
    Part skin = {MAT_SKIN, 0, 1.0f, a.skin, 0};
    Part hair = {MAT_HAIR, 0, 1.0f, a.hairColor, 0};
    Part top = {MAT_FABRIC, 0, 1.0f, a.topColor, 0};
    Part shirt = {MAT_FABRIC, 0, 1.0f, a.shirtColor, 0};
    Part pants = {MAT_FABRIC, 0, 1.0f, a.pantsColor, 0};
    Part shoes = {MAT_LEATHER, 0, 1.2f, a.shoeColor, 0};
    Part accent = {MAT_PAINT, MF_EMISSIVE, 1.0f, a.accent, 12.0f};
    if (a.top == TOP_JACKET || a.top == TOP_COAT) top.layer = MAT_LEATHER;
    if (a.top == TOP_TACTICAL) { top.layer = MAT_METAL_PANEL; top.rough = 1.2f; }
    if (a.top == TOP_SHIRTLESS) top = skin;
    bool alien = a.body != BODY_HUMAN;
    if (alien) {
        skin = {MAT_ALIEN, 0, 0.8f, a.skin, 0};
        top = skin; shirt = skin; pants = skin; shoes = skin; hair = skin;
    }
    float armR = (a.top == TOP_JACKET || a.top == TOP_COAT || a.top == TOP_HOODIE || a.top == TOP_TACTICAL) ? 1.18f : 1.0f;
    if (a.body == BODY_BRUTE) armR = 1.8f;
    if (a.body == BODY_STALKER || a.body == BODY_OVERSEER) armR = 0.72f;
    float legR = a.body == BODY_BRUTE ? 1.6f : (a.body == BODY_STALKER ? 0.75f : 1.0f);

    // Torso
    torso(mb, s, a, top, shirt, pants, bw);

    // Neck and head
    use(mb, skin);
    limb(mb, J[B_NECK] - vec3(0, 0.04f * sc, 0), J[B_HEAD] + vec3(0, 0.03f * sc, 0), 0.055f * sc, 0.05f * sc, B_CHEST, B_NECK, B_HEAD, 8);
    vec3 hc = J[B_HEAD] + vec3(0, 0.1f * sc, 0.015f * sc);
    float hr = 0.1f * sc;
    if (a.body == BODY_STALKER) {
        // Elongated skull swept backwards
        use(mb, skin);
        blob(mb, hc + vec3(0, 0.02f, -0.12f) * sc, vec3(0.08f, 0.09f, 0.24f) * sc, B_HEAD, 12, 8);
        blob(mb, hc + vec3(0, -0.03f, 0.06f) * sc, vec3(0.07f, 0.08f, 0.09f) * sc, B_HEAD);
        use(mb, accent);
        blob(mb, hc + vec3(0.04f, 0.0f, 0.12f) * sc, vec3(0.018f, 0.012f, 0.012f) * sc, B_HEAD, 6, 4);
        blob(mb, hc + vec3(-0.04f, 0.0f, 0.12f) * sc, vec3(0.018f, 0.012f, 0.012f) * sc, B_HEAD, 6, 4);
        // Spine ridge glow
        for (int k = 0; k < 5; k++) blob(mb, vec3(0, (1.1f + k * 0.09f) * sc, -0.12f * sc), vec3(0.025f, 0.025f, 0.02f) * sc, k < 2 ? B_SPINE : B_CHEST, 6, 4);
    } else if (a.body == BODY_OVERSEER) {
        use(mb, skin);
        blob(mb, hc + vec3(0, 0.08f, -0.04f) * sc, vec3(0.09f, 0.2f, 0.12f) * sc, B_HEAD, 12, 8);
        use(mb, accent);
        for (int k = 0; k < 5; k++) blob(mb, hc + vec3((k - 2) * 0.035f, 0.26f, -0.02f) * sc, vec3(0.015f, 0.05f, 0.015f) * sc, B_HEAD, 6, 4);
        blob(mb, hc + vec3(0, 0.0f, 0.1f) * sc, vec3(0.07f, 0.012f, 0.012f) * sc, B_HEAD, 8, 4);
    } else if (a.body == BODY_BRUTE) {
        use(mb, skin);
        blob(mb, hc + vec3(0, -0.02f, 0.02f) * sc, vec3(0.13f, 0.12f, 0.14f) * sc, B_HEAD, 12, 8);
        use(mb, accent);
        blob(mb, hc + vec3(0.05f, 0.0f, 0.13f) * sc, vec3(0.02f, 0.015f, 0.01f) * sc, B_HEAD, 6, 4);
        blob(mb, hc + vec3(-0.05f, 0.0f, 0.13f) * sc, vec3(0.02f, 0.015f, 0.01f) * sc, B_HEAD, 6, 4);
    } else if (a.body == BODY_TENDRIL) {
        use(mb, accent);
        blob(mb, J[B_CHEST] + vec3(0, 0, 0.05f) * sc, vec3(0.12f, 0.12f, 0.12f) * sc, B_CHEST, 12, 8);
        use(mb, skin);
        blob(mb, hc, vec3(0.08f, 0.1f, 0.09f) * sc, B_HEAD);
    } else {
        // Human head: skull, jaw, nose, ears, eyes
        use(mb, skin);
        blob(mb, hc, vec3(hr * 0.92f, hr * 1.12f, hr * 1.05f), B_HEAD, 14, 10);
        blob(mb, hc + vec3(0, -0.06f, 0.035f) * sc, vec3(0.072f, 0.05f, 0.07f) * sc, B_HEAD, 10, 6);
        blob(mb, hc + vec3(0, -0.005f, 0.1f) * sc, vec3(0.017f, 0.03f, 0.022f) * sc, B_HEAD, 6, 5);
        blob(mb, hc + vec3(0.092f, 0.0f, 0.0f) * sc, vec3(0.012f, 0.03f, 0.02f) * sc, B_HEAD, 6, 4);
        blob(mb, hc + vec3(-0.092f, 0.0f, 0.0f) * sc, vec3(0.012f, 0.03f, 0.02f) * sc, B_HEAD, 6, 4);
        // Cheekbones and chin give the face planes instead of a featureless egg
        blob(mb, hc + vec3(0.048f, -0.012f, 0.066f) * sc, vec3(0.03f, 0.024f, 0.022f) * sc, B_HEAD, 8, 5);
        blob(mb, hc + vec3(-0.048f, -0.012f, 0.066f) * sc, vec3(0.03f, 0.024f, 0.022f) * sc, B_HEAD, 8, 5);
        blob(mb, hc + vec3(0, -0.092f, 0.068f) * sc, vec3(0.03f, 0.024f, 0.026f) * sc, B_HEAD, 8, 5);
        Part lips = {MAT_SKIN, 0, 0.7f, a.skin * vec3(0.78f, 0.55f, 0.52f), 0};
        use(mb, lips);
        blob(mb, hc + vec3(0, -0.056f, 0.093f) * sc, vec3(0.025f, 0.0075f, 0.01f) * sc, B_HEAD, 8, 4);
        // Eyes: white sclera with a dark iris, set under the brow
        Part sclera = {MAT_PAINT, 0, 0.15f, vec3(0.78f, 0.76f, 0.72f), 0};
        Part iris = {MAT_PAINT, 0, 0.1f, vec3(0.07f, 0.05f, 0.04f), 0};
        for (int e = 0; e < 2; e++) {
            float ex = e == 0 ? 0.035f : -0.035f;
            use(mb, sclera);
            blob(mb, hc + vec3(ex, 0.024f, 0.086f) * sc, vec3(0.0135f, 0.0085f, 0.008f) * sc, B_HEAD, 8, 5);
            use(mb, iris);
            blob(mb, hc + vec3(ex, 0.024f, 0.0925f) * sc, vec3(0.0062f, 0.0062f, 0.0028f) * sc, B_HEAD, 8, 4);
        }
        Part brow = {MAT_HAIR, 0, 1.0f, a.hairColor * 0.8f, 0};
        use(mb, brow);
        blob(mb, hc + vec3(0.035f, 0.045f, 0.09f) * sc, vec3(0.025f, 0.006f, 0.008f) * sc, B_HEAD, 6, 4);
        blob(mb, hc + vec3(-0.035f, 0.045f, 0.09f) * sc, vec3(0.025f, 0.006f, 0.008f) * sc, B_HEAD, 6, 4);
        if (a.beard) {
            Part bd = {MAT_HAIR, 0, 1.0f, lerp(a.hairColor, a.skin, a.beard == 1 ? 0.55f : 0.15f), 0};
            use(mb, bd);
            blob(mb, hc + vec3(0, -0.07f, 0.04f) * sc, vec3(0.075f, 0.045f, 0.068f) * sc, B_HEAD, 10, 6);
        }
        // Hair / headwear
        if (a.hair == HAIR_SHORT || a.hair == HAIR_BUZZ) {
            use(mb, hair);
            // Shell shifted up and back so it caps the skull and leaves forehead, eyes and temples bare
            float th = a.hair == HAIR_BUZZ ? 1.0f : 1.04f;
            blob(mb, hc + vec3(0, 0.04f, -0.03f) * sc, vec3(hr * 0.98f * th, hr * 0.95f * th, hr * 1.0f * th), B_HEAD, 14, 8);
            if (a.hair == HAIR_SHORT) {
                // Volume on top, swept back, and sideburns
                blob(mb, hc + vec3(0, 0.078f, -0.022f) * sc, vec3(hr * 0.84f, hr * 0.4f, hr * 0.9f), B_HEAD, 12, 6);
                blob(mb, hc + vec3(0.083f, -0.025f, 0.02f) * sc, vec3(0.012f, 0.03f, 0.018f) * sc, B_HEAD, 6, 4);
                blob(mb, hc + vec3(-0.083f, -0.025f, 0.02f) * sc, vec3(0.012f, 0.03f, 0.018f) * sc, B_HEAD, 6, 4);
            }
        } else if (a.hair == HAIR_LONG) {
            use(mb, hair);
            blob(mb, hc + vec3(0, 0.042f, -0.03f) * sc, vec3(hr * 1.02f, hr * 0.98f, hr * 1.03f), B_HEAD, 14, 8);
            // Hair falling to the shoulders, with strands framing the face
            blob(mb, hc + vec3(0, -0.11f, -0.055f) * sc, vec3(0.095f, 0.15f, 0.06f) * sc, B_HEAD, 12, 7);
            blob(mb, hc + vec3(0.075f, -0.07f, 0.02f) * sc, vec3(0.025f, 0.09f, 0.04f) * sc, B_HEAD, 8, 5);
            blob(mb, hc + vec3(-0.075f, -0.07f, 0.02f) * sc, vec3(0.025f, 0.09f, 0.04f) * sc, B_HEAD, 8, 5);
        } else if (a.hair == HAIR_CAP || a.hair == HAIR_POLICECAP) {
            Part cap = {MAT_FABRIC, 0, 1.0f, a.hair == HAIR_POLICECAP ? vec3(0.06f, 0.07f, 0.15f) : a.accent * 0.6f, 0};
            use(mb, cap);
            blob(mb, hc + vec3(0, 0.062f, -0.012f) * sc, vec3(hr * 1.02f, hr * 0.7f, hr * 1.04f), B_HEAD, 14, 8);
            mb->set_bone(B_HEAD);
            mb->cone_frustum(hc + vec3(0, 0.065f, 0.06f) * sc, 0.075f * sc, 0.07f * sc, 0.01f * sc, 10, true);
            if (a.hair == HAIR_POLICECAP) {
                mb->cone_frustum(hc + vec3(0, 0.07f, 0) * sc, 0.11f * sc, 0.12f * sc, 0.05f * sc, 12, true);
                Part badge = {MAT_STEEL, MF_METAL, 0.4f, vec3(0.9f, 0.75f, 0.3f), 0};
                use(mb, badge);
                blob(mb, hc + vec3(0, 0.1f, 0.11f) * sc, vec3(0.015f, 0.015f, 0.006f) * sc, B_HEAD, 6, 4);
            }
        } else if (a.hair == HAIR_HELMET) {
            Part helm = {MAT_METAL_PANEL, MF_METAL, 0.9f, a.topColor * 1.2f, 0};
            use(mb, helm);
            blob(mb, hc + vec3(0, 0.03f, -0.005f) * sc, vec3(hr * 1.18f, hr * 1.15f, hr * 1.2f), B_HEAD, 14, 8);
            use(mb, accent);
            blob(mb, hc + vec3(0, 0.02f, 0.105f) * sc, vec3(0.075f, 0.018f, 0.02f) * sc, B_HEAD, 10, 4);
        } else if (a.hair == HAIR_BANDANA) {
            use(mb, hair);
            blob(mb, hc + vec3(0, 0.04f, -0.03f) * sc, vec3(hr * 1.0f, hr * 0.96f, hr * 1.02f), B_HEAD, 14, 8);
            Part band = {MAT_FABRIC, 0, 1.0f, a.accent, 0};
            use(mb, band);
            blob(mb, hc + vec3(0, -0.055f, 0.03f) * sc, vec3(0.085f, 0.04f, 0.08f) * sc, B_HEAD, 10, 6);
        }
    }

    // Arms
    for (int side = 0; side < 2; side++) {
        int ua = side == 0 ? B_L_UPPERARM : B_R_UPPERARM;
        int fa = side == 0 ? B_L_FOREARM : B_R_FOREARM;
        int hb = side == 0 ? B_L_HAND : B_R_HAND;
        bool bareArms = a.top == TOP_TSHIRT || a.top == TOP_SHIRTLESS;
        use(mb, a.top == TOP_SHIRTLESS ? skin : top);
        // Deltoid: flattened and tucked into the torso so the shoulder reads as one shape
        blob(mb, J[ua] + vec3(side == 0 ? -0.025f : 0.025f, 0.012f, -0.004f) * sc, vec3(0.06f, 0.055f, 0.064f) * sc * armR, ua, 12, 7);
        limb(mb, J[ua], J[fa], 0.055f * sc * armR, 0.045f * sc * armR, B_CHEST, ua, fa, 12);
        use(mb, bareArms ? skin : top);
        limb(mb, J[fa], J[hb], 0.046f * sc * armR, 0.036f * sc * armR, ua, fa, hb, 12);
        // Hand / claws
        if (alien && a.body != BODY_BRUTE) {
            use(mb, skin);
            vec3 h = J[hb];
            for (int c = 0; c < 3; c++) {
                vec3 o((c - 1) * 0.02f, -0.06f, 0.02f);
                mb->set_bone(hb);
                mb->tube(h + o * sc, h + (o * 2.0f + vec3(0, -0.14f, 0.05f)) * sc, 0.012f * sc, 0.002f * sc, 5, false, false);
            }
        } else {
            Part hand = skin;
            if (a.top == TOP_TACTICAL) hand = Part{MAT_LEATHER, 0, 1.0f, vec3(0.08f, 0.08f, 0.08f), 0};
            if (a.body == BODY_BRUTE) hand = skin;
            use(mb, hand);
            float hs = sc * (a.body == BODY_BRUTE ? armR : 1.0f);
            vec3 h = J[hb];
            // Palm, four slightly curled fingers and an opposed thumb
            blob(mb, h + vec3(0, -0.045f, 0.004f) * hs, vec3(0.036f, 0.048f, 0.017f) * hs, hb, 8, 6);
            mb->set_bone(hb);
            for (int fi = 0; fi < 4; fi++) {
                float xo = (fi - 1.5f) * 0.0165f;
                float len = fi == 0 || fi == 3 ? 0.062f : 0.075f;
                vec3 f0 = h + vec3(xo, -0.088f, 0.006f) * hs;
                vec3 f1 = f0 + vec3(xo * 0.12f, -len * 0.62f, 0.012f) * hs;
                vec3 f2 = f1 + vec3(0, -len * 0.38f, 0.016f) * hs;
                mb->tube(f0, f1, 0.0085f * hs, 0.0078f * hs, 6, false, false);
                mb->tube(f1, f2, 0.0078f * hs, 0.0068f * hs, 6, false, true);
            }
            vec3 t0 = h + vec3(0, -0.04f, 0.022f) * hs;
            mb->tube(t0, t0 + vec3(0, -0.035f, 0.035f) * hs, 0.011f * hs, 0.008f * hs, 6, false, false);
            mb->tube(t0 + vec3(0, -0.035f, 0.035f) * hs, t0 + vec3(0, -0.06f, 0.05f) * hs, 0.008f * hs, 0.007f * hs, 6, false, true);
        }
    }

    // Legs
    for (int side = 0; side < 2; side++) {
        int th = side == 0 ? B_L_THIGH : B_R_THIGH;
        int sh = side == 0 ? B_L_SHIN : B_R_SHIN;
        int ft = side == 0 ? B_L_FOOT : B_R_FOOT;
        use(mb, pants);
        limb(mb, J[th] + vec3(0, 0.02f, 0) * sc, J[sh], 0.085f * sc * legR, 0.058f * sc * legR, B_PELVIS, th, sh, 12);
        limb(mb, J[sh], J[ft], 0.058f * sc * legR, 0.042f * sc * legR, th, sh, ft, 12);
        use(mb, shoes);
        vec3 f = J[ft];
        if (alien) {
            mb->set_bone(ft);
            mb->tube(f, f + vec3(0, -0.07f, 0.2f) * sc, 0.035f * sc, 0.01f * sc, 6, true, true);
        } else {
            blob(mb, f + vec3(0, -0.035f, 0.07f) * sc, vec3(0.05f, 0.045f, 0.14f) * sc, ft, 10, 6);
            Part sole = {MAT_RUBBER, 0, 1.0f, vec3(0.05f, 0.05f, 0.05f), 0};
            use(mb, sole);
            blob(mb, f + vec3(0, -0.068f, 0.07f) * sc, vec3(0.054f, 0.014f, 0.148f) * sc, ft, 10, 4);
        }
    }

    // Tendril creature: wiry tendrils from every limb
    if (a.body == BODY_TENDRIL) {
        Rng r;
        r.seed(4242);
        use(mb, skin);
        for (int k = 0; k < 40; k++) {
            int b = r.irange(0, B_COUNT - 1);
            vec3 st = J[b] + vec3(r.range(-0.08f, 0.08f), r.range(-0.08f, 0.08f), r.range(-0.08f, 0.08f)) * sc;
            vec3 dir = normalize(vec3(r.range(-1, 1), r.range(-0.6f, 1), r.range(-1, 1)));
            float len = r.range(0.3f, 1.1f) * sc;
            mb->set_bone(b);
            vec3 mid = st + dir * len * 0.5f + vec3(0, r.range(-0.2f, 0.2f), 0) * sc;
            mb->tube(st, mid, 0.018f * sc, 0.012f * sc, 4, false, false);
            mb->tube(mid, mid + normalize(dir + vec3(0, -0.5f, 0)) * len * 0.6f, 0.012f * sc, 0.003f * sc, 4, false, false);
        }
    }
    // Tactical gear: vest plates, backpack and shoulder light
    if (a.top == TOP_TACTICAL) {
        Part plate = {MAT_METAL_PANEL, MF_METAL, 1.0f, a.topColor * 1.4f, 0};
        use(mb, plate);
        int s0 = mb->vcount();
        mb->set_bone(B_CHEST);
        mb->box(vec3(-0.15f, 1.18f, 0.08f) * sc, vec3(0.15f, 1.44f, 0.15f) * sc);
        mb->box(vec3(-0.14f, 1.12f, -0.2f) * sc, vec3(0.14f, 1.42f, -0.1f) * sc);
        FIGHTO_UNUSED(s0);
        use(mb, accent);
        mb->set_bone(B_CHEST);
        mb->box(vec3(-0.13f, 1.3f, 0.151f) * sc, vec3(-0.03f, 1.33f, 0.155f) * sc);
        mb->box(vec3(0.16f, 1.4f, -0.02f) * sc, vec3(0.19f, 1.43f, 0.02f) * sc);
    }
    if (!alien) {
        // Collar for jackets, coats and suits; a bunched hood for hoodies; a belt on every pair of trousers
        if (a.top == TOP_JACKET || a.top == TOP_COAT || a.top == TOP_SUIT || a.top == TOP_LABCOAT || a.top == TOP_UNIFORM) {
            use(mb, top);
            mb->set_bone(B_CHEST);
            mb->cone_frustum(vec3(0, 1.455f * sc, -0.008f * sc), 0.088f * sc, 0.072f * sc, 0.065f * sc, 16, false);
        }
        if (a.top == TOP_HOODIE) {
            use(mb, top);
            blob(mb, vec3(0, 1.5f * sc, -0.085f * sc), vec3(0.115f, 0.07f, 0.06f) * sc, B_CHEST, 12, 6);
            Part str = {MAT_FABRIC, 0, 1.0f, vec3(0.85f, 0.85f, 0.85f), 0};
            use(mb, str);
            mb->set_bone(B_CHEST);
            mb->tube(vec3(0.03f, 1.44f, 0.115f) * sc, vec3(0.032f, 1.33f, 0.125f) * sc, 0.005f * sc, 0.005f * sc, 4, false, true);
            mb->tube(vec3(-0.03f, 1.44f, 0.115f) * sc, vec3(-0.032f, 1.33f, 0.125f) * sc, 0.005f * sc, 0.005f * sc, 4, false, true);
        }
        if (a.top != TOP_TACTICAL && a.top != TOP_LABCOAT && a.top != TOP_COAT && a.top != TOP_UNIFORM) {
            Part belt = {MAT_LEATHER, 0, 0.8f, vec3(0.06f, 0.045f, 0.035f), 0};
            use(mb, belt);
            blob(mb, vec3(0, 0.985f * sc, 0.0f), vec3(0.181f * bw, 0.022f, 0.121f) * sc, B_PELVIS, 16, 4);
            Part buckle = {MAT_STEEL, MF_METAL, 0.4f, vec3(0.7f, 0.68f, 0.62f), 0};
            use(mb, buckle);
            blob(mb, vec3(0, 0.985f * sc, 0.121f * sc), vec3(0.022f, 0.016f, 0.006f) * sc, B_PELVIS, 6, 4);
        }
    }
    if (a.top == TOP_UNIFORM) {
        Part badge = {MAT_STEEL, MF_METAL, 0.4f, vec3(0.9f, 0.75f, 0.3f), 0};
        use(mb, badge);
        blob(mb, vec3(0.08f, 1.38f, 0.125f) * sc, vec3(0.022f, 0.026f, 0.008f) * sc, B_CHEST, 6, 4);
        Part belt = {MAT_LEATHER, 0, 1.0f, vec3(0.05f, 0.05f, 0.05f), 0};
        use(mb, belt);
        blob(mb, vec3(0, 0.99f, 0.0f) * sc, vec3(0.18f * bw, 0.03f, 0.12f) * sc, B_PELVIS, 12, 4);
    }
    if (a.top == TOP_LABCOAT || a.top == TOP_COAT) {
        // Coat skirt
        use(mb, top);
        mb->set_bone(B_PELVIS);
        mb->cone_frustum(vec3(0, 0.6f * sc, -0.01f), 0.24f * sc * bw, 0.19f * sc * bw, 0.42f * sc, 14, false);
    }
}

// ---------------------------------------------------------------------------
// Model cache and presets
// ---------------------------------------------------------------------------
// More slots than actors: when the cache is full there is always a model that no
// active actor references, so a live actor's mesh is never replaced underneath it.
#define MAX_MODELS (MAX_ACTORS + 8)
static CharacterModel g_models[MAX_MODELS];

static u32 appearance_hash(const Appearance& a) { return crc32(&a, sizeof(a), 0); }

CharacterModel* character_model_get(const Appearance& a) {
    u32 h = appearance_hash(a);
    for (int i = 0; i < MAX_MODELS; i++)
        if (g_models[i].used && g_models[i].hash == h) return &g_models[i];
    int slot = -1;
    for (int i = 0; i < MAX_MODELS; i++) if (!g_models[i].used) { slot = i; break; }
    if (slot < 0) {
        static bool inUse[MAX_MODELS];
        memset(inUse, 0, sizeof(inUse));
        for (int i = 0; i < MAX_ACTORS; i++)
            if (g.actors[i].active && g.actors[i].model) inUse[g.actors[i].model - g_models] = true;
        int start = (int)(h % MAX_MODELS);
        for (int k = 0; k < MAX_MODELS && slot < 0; k++)
            if (!inUse[(start + k) % MAX_MODELS]) slot = (start + k) % MAX_MODELS;
        if (slot < 0) slot = start;  // unreachable: MAX_MODELS > MAX_ACTORS
    }
    CharacterModel& m = g_models[slot];
    if (m.used) mesh_destroy(&m.mesh);
    skeleton_build(&m.skel, a.body, a.height, a.build);
    MeshBuilder mb;
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    character_build_mesh(&mb, m.skel, a);
    m.mesh = mesh_create(mb);
    mb.free_mem();
    m.hash = h;
    m.used = true;
    return &m;
}

void character_models_clear() {
    for (int i = 0; i < MAX_MODELS; i++) {
        if (g_models[i].used) mesh_destroy(&g_models[i].mesh);
        g_models[i].used = false;
    }
}

static vec3 pick(Rng& r, const vec3* arr, int n) { return arr[r.irange(0, n - 1)]; }

Appearance appearance_preset(const char* name, u32 seed) {
    Rng r;
    r.seed(seed + hash_str(name));
    static const vec3 skins[] = {vec3(0.94f, 0.76f, 0.64f), vec3(0.85f, 0.64f, 0.5f), vec3(0.7f, 0.5f, 0.38f), vec3(0.5f, 0.34f, 0.24f), vec3(0.36f, 0.24f, 0.17f), vec3(0.95f, 0.82f, 0.7f)};
    static const vec3 hairs[] = {vec3(0.06f, 0.05f, 0.04f), vec3(0.2f, 0.13f, 0.08f), vec3(0.35f, 0.25f, 0.15f), vec3(0.55f, 0.45f, 0.3f), vec3(0.5f, 0.5f, 0.5f), vec3(0.12f, 0.1f, 0.08f)};
    static const vec3 tops[] = {vec3(0.5f, 0.12f, 0.1f), vec3(0.15f, 0.2f, 0.35f), vec3(0.25f, 0.3f, 0.2f), vec3(0.6f, 0.55f, 0.45f), vec3(0.12f, 0.12f, 0.13f), vec3(0.45f, 0.45f, 0.5f), vec3(0.6f, 0.4f, 0.2f), vec3(0.3f, 0.15f, 0.35f)};
    static const vec3 pantsCols[] = {vec3(0.14f, 0.17f, 0.26f), vec3(0.1f, 0.1f, 0.11f), vec3(0.3f, 0.27f, 0.22f), vec3(0.22f, 0.22f, 0.24f), vec3(0.25f, 0.3f, 0.4f)};
    Appearance a;
    memset(&a, 0, sizeof(a));
    a.body = BODY_HUMAN;
    a.height = r.range(1.68f, 1.9f);
    a.build = r.range(0.92f, 1.12f);
    a.skin = pick(r, skins, ARRAY_COUNT(skins));
    a.hairColor = pick(r, hairs, ARRAY_COUNT(hairs));
    a.hair = (u8)(r.chance(0.15f) ? HAIR_BALD : r.chance(0.2f) ? HAIR_LONG : r.chance(0.2f) ? HAIR_CAP : HAIR_SHORT);
    a.top = (u8)(r.chance(0.35f) ? TOP_JACKET : r.chance(0.4f) ? TOP_HOODIE : r.chance(0.3f) ? TOP_COAT : TOP_TSHIRT);
    a.topColor = pick(r, tops, ARRAY_COUNT(tops));
    a.shirtColor = pick(r, tops, ARRAY_COUNT(tops)) * 1.2f;
    a.pantsColor = pick(r, pantsCols, ARRAY_COUNT(pantsCols));
    a.shoeColor = r.chance(0.5f) ? vec3(0.08f, 0.08f, 0.08f) : vec3(0.35f, 0.25f, 0.18f);
    a.accent = pick(r, tops, ARRAY_COUNT(tops));
    a.beard = (u8)(r.chance(0.3f) ? r.irange(1, 2) : 0);

    if (str_eq(name, "damian")) {
        a.height = 1.83f; a.build = 1.06f; a.skin = vec3(0.86f, 0.66f, 0.54f); a.hairColor = vec3(0.06f, 0.05f, 0.045f);
        a.hair = HAIR_SHORT; a.top = TOP_JACKET; a.topColor = vec3(0.11f, 0.105f, 0.11f); a.shirtColor = vec3(0.28f, 0.29f, 0.31f);
        a.pantsColor = vec3(0.13f, 0.16f, 0.24f); a.shoeColor = vec3(0.22f, 0.15f, 0.1f); a.beard = 1;
    } else if (str_eq(name, "marcus")) {
        a.height = 1.8f; a.build = 1.1f; a.skin = vec3(0.45f, 0.3f, 0.21f); a.hair = HAIR_BUZZ; a.hairColor = vec3(0.05f, 0.04f, 0.04f);
        a.top = TOP_COAT; a.topColor = vec3(0.35f, 0.24f, 0.15f); a.shirtColor = vec3(0.7f, 0.7f, 0.68f); a.pantsColor = vec3(0.12f, 0.12f, 0.13f); a.beard = 2;
    } else if (str_eq(name, "eli")) {
        a.height = 1.76f; a.build = 0.95f; a.skin = vec3(0.86f, 0.66f, 0.54f); a.hair = HAIR_SHORT; a.hairColor = vec3(0.08f, 0.06f, 0.05f);
        a.top = TOP_HOODIE; a.topColor = vec3(0.4f, 0.42f, 0.45f); a.pantsColor = vec3(0.15f, 0.18f, 0.28f); a.beard = 0;
    } else if (str_eq(name, "tommy")) {
        a.height = 1.75f; a.build = 1.28f; a.skin = vec3(0.92f, 0.74f, 0.62f); a.hair = HAIR_BALD; a.top = TOP_TSHIRT;
        a.topColor = vec3(0.75f, 0.72f, 0.65f); a.pantsColor = vec3(0.25f, 0.25f, 0.27f); a.beard = 2; a.hairColor = vec3(0.3f, 0.2f, 0.12f);
    } else if (str_eq(name, "fighter")) {
        a.top = TOP_SHIRTLESS; a.build = r.range(1.05f, 1.22f); a.pantsColor = pick(r, tops, ARRAY_COUNT(tops)); a.shoeColor = vec3(0.1f, 0.1f, 0.1f);
        a.hair = r.chance(0.5f) ? HAIR_BUZZ : HAIR_BALD;
    } else if (str_eq(name, "brick")) {
        a.top = TOP_SHIRTLESS; a.height = 1.96f; a.build = 1.32f; a.skin = vec3(0.92f, 0.72f, 0.6f); a.hair = HAIR_BUZZ;
        a.hairColor = vec3(0.45f, 0.25f, 0.12f); a.pantsColor = vec3(0.55f, 0.08f, 0.06f); a.beard = 2;
    } else if (str_eq(name, "kings")) {
        a.top = TOP_HOODIE; a.topColor = r.chance(0.5f) ? vec3(0.55f, 0.06f, 0.05f) : vec3(0.1f, 0.1f, 0.1f); a.hair = HAIR_BANDANA;
        a.accent = vec3(0.7f, 0.05f, 0.05f); a.pantsColor = vec3(0.14f, 0.17f, 0.26f);
    } else if (str_eq(name, "varga_thug")) {
        a.top = r.chance(0.5f) ? TOP_SUIT : TOP_JACKET; a.topColor = vec3(0.06f, 0.06f, 0.07f); a.shirtColor = vec3(0.55f, 0.1f, 0.1f);
        a.pantsColor = vec3(0.07f, 0.07f, 0.08f); a.hair = r.chance(0.5f) ? HAIR_BUZZ : HAIR_SHORT; a.build = r.range(1.05f, 1.2f);
    } else if (str_eq(name, "varga")) {
        a.height = 1.86f; a.build = 1.1f; a.hair = HAIR_BALD; a.top = TOP_COAT; a.topColor = vec3(0.05f, 0.05f, 0.06f);
        a.shirtColor = vec3(0.8f, 0.78f, 0.72f); a.pantsColor = vec3(0.06f, 0.06f, 0.07f); a.beard = 2; a.hairColor = vec3(0.4f, 0.4f, 0.4f);
        a.skin = vec3(0.9f, 0.75f, 0.65f);
    } else if (str_eq(name, "police")) {
        a.top = TOP_UNIFORM; a.topColor = vec3(0.08f, 0.1f, 0.2f); a.shirtColor = a.topColor; a.pantsColor = vec3(0.06f, 0.07f, 0.13f);
        a.hair = HAIR_POLICECAP; a.shoeColor = vec3(0.05f, 0.05f, 0.05f);
    } else if (str_eq(name, "aegis") || str_eq(name, "operative") || str_eq(name, "kincaid")) {
        a.top = TOP_TACTICAL; a.topColor = str_eq(name, "operative") ? vec3(0.22f, 0.23f, 0.25f) : vec3(0.08f, 0.08f, 0.09f);
        a.pantsColor = a.topColor * 0.9f; a.shoeColor = vec3(0.06f, 0.06f, 0.06f); a.hair = HAIR_HELMET;
        a.accent = str_eq(name, "operative") ? vec3(1.0f, 0.2f, 0.1f) : vec3(0.95f, 0.7f, 0.15f);
        if (str_eq(name, "kincaid")) { a.hair = HAIR_LONG; a.height = 1.73f; a.build = 0.9f; a.hairColor = vec3(0.12f, 0.08f, 0.05f); a.skin = vec3(0.8f, 0.6f, 0.48f); a.beard = 0; }
    } else if (str_eq(name, "thorne")) {
        a.height = 1.84f; a.build = 1.0f; a.hair = HAIR_SHORT; a.hairColor = vec3(0.6f, 0.6f, 0.6f); a.top = TOP_SUIT;
        a.topColor = vec3(0.2f, 0.21f, 0.23f); a.shirtColor = vec3(0.85f, 0.86f, 0.88f); a.pantsColor = vec3(0.18f, 0.19f, 0.21f); a.beard = 0;
        a.skin = vec3(0.9f, 0.76f, 0.66f);
    } else if (str_eq(name, "okafor")) {
        a.height = 1.7f; a.build = 0.9f; a.hair = HAIR_LONG; a.hairColor = vec3(0.04f, 0.03f, 0.03f); a.top = TOP_LABCOAT;
        a.topColor = vec3(0.88f, 0.9f, 0.92f); a.shirtColor = vec3(0.3f, 0.45f, 0.55f); a.pantsColor = vec3(0.2f, 0.2f, 0.22f);
        a.skin = vec3(0.4f, 0.27f, 0.19f); a.beard = 0;
    } else if (str_eq(name, "scientist")) {
        a.top = TOP_LABCOAT; a.topColor = vec3(0.85f, 0.87f, 0.9f);
    } else if (str_eq(name, "stalker")) {
        a.body = BODY_STALKER; a.height = 2.25f; a.build = 0.9f; a.skin = vec3(0.32f, 0.3f, 0.36f); a.accent = vec3(0.3f, 1.0f, 0.55f);
    } else if (str_eq(name, "brute")) {
        a.body = BODY_BRUTE; a.height = 2.55f; a.build = 1.2f; a.skin = vec3(0.3f, 0.26f, 0.3f); a.accent = vec3(1.0f, 0.35f, 0.15f);
    } else if (str_eq(name, "tendril")) {
        a.body = BODY_TENDRIL; a.height = 1.9f; a.build = 0.8f; a.skin = vec3(0.55f, 0.48f, 0.4f); a.accent = vec3(1.0f, 0.85f, 0.4f);
    } else if (str_eq(name, "overseer")) {
        a.body = BODY_OVERSEER; a.height = 2.8f; a.build = 0.95f; a.skin = vec3(0.25f, 0.24f, 0.3f); a.accent = vec3(0.3f, 0.75f, 1.0f);
    }
    return a;
}
