// FIGHTO - save data serialisation. Format:
//   "FGTO" | u32 version | u32 payload size | u32 CRC32(payload) | payload
// The payload is a list of tagged fields (u16 id, u32 length, bytes) so older
// saves load with defaults for new fields and unknown fields are skipped.
#include "game.h"

#define SAVE_VERSION 1
#define SAVE_MAGIC "FGTO"

enum SaveField {
    SF_MONEY = 1, SF_HAS_WEAPON, SF_RESERVE, SF_CLIP, SF_UPGRADES, SF_REP, SF_EVIDENCE, SF_CACHES, SF_MISSIONS,
    SF_PHASE, SF_FLAGS, SF_STATS, SF_PLAYER_POS, SF_PLAYER_VITALS, SF_WEAPON, SF_INFO, SF_WORLD,
};

struct Writer {
    Array<u8>* out;
    void bytes(const void* p, int n) {
        out->reserve(out->count + n);
        memcpy(out->data + out->count, p, (size_t)n);
        out->count += n;
    }
    void u16v(u16 v) { bytes(&v, 2); }
    void u32v(u32 v) { bytes(&v, 4); }
    void field(u16 id, const void* p, int n) { u16v(id); u32v((u32)n); bytes(p, n); }
};

static void write_i32s(Writer& w, u16 id, const int* v, int n) { w.field(id, v, n * 4); }

bool save_serialize(Array<u8>* out) {
    Array<u8> payload = {};
    Writer w;
    w.out = &payload;
    const PlayerData& pd = g.pd;
    w.field(SF_MONEY, &pd.money, 4);
    u8 hw[W_COUNT];
    for (int i = 0; i < W_COUNT; i++) hw[i] = pd.hasWeapon[i] ? 1 : 0;
    w.field(SF_HAS_WEAPON, hw, W_COUNT);
    write_i32s(w, SF_RESERVE, pd.reserve, W_COUNT);
    int clips[W_COUNT];
    memcpy(clips, pd.clip, sizeof(clips));
    if (g.actors[PLAYER].active) clips[g.actors[PLAYER].weapon] = g.actors[PLAYER].clipAmmo;
    write_i32s(w, SF_CLIP, clips, W_COUNT);
    write_i32s(w, SF_UPGRADES, pd.upgrades, UP_COUNT);
    write_i32s(w, SF_REP, pd.rep, FAC_COUNT);
    u8 ev[MAX_EVIDENCE];
    for (int i = 0; i < MAX_EVIDENCE; i++) ev[i] = pd.evidence[i] ? 1 : 0;
    w.field(SF_EVIDENCE, ev, MAX_EVIDENCE);
    u8 ca[32];
    for (int i = 0; i < 32; i++) ca[i] = pd.cachesFound[i] ? 1 : 0;
    w.field(SF_CACHES, ca, 32);
    w.field(SF_MISSIONS, &pd.missionsDone, 4);
    w.field(SF_PHASE, &pd.phase, 4);
    // Flags
    {
        Array<u8> fl = {};
        Writer fw;
        fw.out = &fl;
        fw.u32v((u32)pd.flagCount);
        for (int i = 0; i < pd.flagCount; i++) {
            fw.bytes(pd.flags[i].name, 24);
            fw.bytes(&pd.flags[i].value, 4);
        }
        w.field(SF_FLAGS, fl.data, fl.count);
        fl.free_mem();
    }
    int stats[13] = {pd.kills, pd.knockouts, pd.shotsFired, pd.shotsHit, pd.fightsWon, pd.racesWon, pd.civiliansRescued,
                     pd.timesArrested, pd.deaths, 0, 0, pd.safehouseUnlocked, pd.pitWins};
    memcpy(&stats[9], &pd.playTime, 4);
    memcpy(&stats[10], &pd.distanceDriven, 4);
    write_i32s(w, SF_STATS, stats, 13);
    const Actor& p = g.actors[PLAYER];
    float pos[4] = {p.pos.x, p.pos.y, p.pos.z, p.yaw};
    w.field(SF_PLAYER_POS, pos, 16);
    float vit[2] = {p.health, p.armor};
    w.field(SF_PLAYER_VITALS, vit, 8);
    int weapon = p.weapon;
    w.field(SF_WEAPON, &weapon, 4);
    char info[64];
    fmt_str(info, sizeof(info), "Phase %d | $%d | %d missions", pd.phase, pd.money, __builtin_popcount(pd.missionsDone));
    w.field(SF_INFO, info, 64);
    float world[2] = {g.power ? 1.0f : 0.0f, g.rainTarget};
    w.field(SF_WORLD, world, 8);

    out->clear();
    Writer h;
    h.out = out;
    h.bytes(SAVE_MAGIC, 4);
    h.u32v(SAVE_VERSION);
    h.u32v((u32)payload.count);
    h.u32v(crc32(payload.data, (size_t)payload.count, 0));
    h.bytes(payload.data, payload.count);
    payload.free_mem();
    return true;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool save_deserialize(const u8* data, int size, char* err, int errCap) {
    if (!data || size < 16) { fmt_str(err, errCap, "Save file is too small"); return false; }
    if (memcmp(data, SAVE_MAGIC, 4) != 0) { fmt_str(err, errCap, "Not a FIGHTO save file"); return false; }
    u32 version, psize, crc;
    memcpy(&version, data + 4, 4);
    memcpy(&psize, data + 8, 4);
    memcpy(&crc, data + 12, 4);
    // Integrity first: the version field is only meaningful once length and checksum match,
    // otherwise random damage would be misreported as "made by a newer version"
    if (psize != (u32)(size - 16)) { fmt_str(err, errCap, "Save file is corrupted (truncated or padded)"); return false; }
    const u8* payload = data + 16;
    if (crc32(payload, psize, 0) != crc) { fmt_str(err, errCap, "Save file is corrupted (checksum mismatch)"); return false; }
    if (version == 0) { fmt_str(err, errCap, "Save file is corrupted (invalid version)"); return false; }
    if (version > SAVE_VERSION) { fmt_str(err, errCap, "Save was made by a newer version (v%u)", version); return false; }
    PlayerData pd;
    memset(&pd, 0, sizeof(pd));
    pd.hasWeapon[W_FISTS] = true;
    vec3 pos(0, 0, 0);
    float yaw = 0, health = 100, armor = 0;
    int weapon = W_FISTS;
    bool hasPos = false;
    float world[2] = {1, 0.7f};
    u32 off = 0;
    while (off + 6 <= psize) {
        u16 id;
        u32 len;
        memcpy(&id, payload + off, 2);
        memcpy(&len, payload + off + 2, 4);
        off += 6;
        if (len > psize - off) { fmt_str(err, errCap, "Save field %u is malformed", id); return false; }
        const u8* f = payload + off;
        switch (id) {
        case SF_MONEY: if (len >= 4) memcpy(&pd.money, f, 4); break;
        case SF_HAS_WEAPON: for (u32 i = 0; i < len && i < W_COUNT; i++) pd.hasWeapon[i] = f[i] != 0; break;
        case SF_RESERVE: memcpy(pd.reserve, f, min_t((size_t)len, sizeof(pd.reserve))); break;
        case SF_CLIP: memcpy(pd.clip, f, min_t((size_t)len, sizeof(pd.clip))); break;
        case SF_UPGRADES: memcpy(pd.upgrades, f, min_t((size_t)len, sizeof(pd.upgrades))); break;
        case SF_REP: memcpy(pd.rep, f, min_t((size_t)len, sizeof(pd.rep))); break;
        case SF_EVIDENCE: for (u32 i = 0; i < len && i < MAX_EVIDENCE; i++) pd.evidence[i] = f[i] != 0; break;
        case SF_CACHES: for (u32 i = 0; i < len && i < 32; i++) pd.cachesFound[i] = f[i] != 0; break;
        case SF_MISSIONS: if (len >= 4) memcpy(&pd.missionsDone, f, 4); break;
        case SF_PHASE: if (len >= 4) memcpy(&pd.phase, f, 4); break;
        case SF_FLAGS: {
            if (len < 4) break;
            u32 n;
            memcpy(&n, f, 4);
            if (n > MAX_FLAGS || 4 + n * 28 > len) { fmt_str(err, errCap, "Save flags are malformed"); return false; }
            for (u32 i = 0; i < n; i++) {
                memcpy(pd.flags[i].name, f + 4 + i * 28, 24);
                pd.flags[i].name[23] = 0;
                memcpy(&pd.flags[i].value, f + 4 + i * 28 + 24, 4);
            }
            pd.flagCount = (int)n;
        } break;
        case SF_STATS: {
            int st[13];
            memset(st, 0, sizeof(st));
            memcpy(st, f, min_t((size_t)len, sizeof(st)));
            pd.kills = st[0]; pd.knockouts = st[1]; pd.shotsFired = st[2]; pd.shotsHit = st[3]; pd.fightsWon = st[4];
            pd.racesWon = st[5]; pd.civiliansRescued = st[6]; pd.timesArrested = st[7]; pd.deaths = st[8];
            memcpy(&pd.playTime, &st[9], 4);
            memcpy(&pd.distanceDriven, &st[10], 4);
            pd.safehouseUnlocked = st[11];
            pd.pitWins = st[12];
        } break;
        case SF_PLAYER_POS:
            if (len >= 16) {
                float v[4];
                memcpy(v, f, 16);
                pos = vec3(v[0], v[1], v[2]);
                yaw = v[3];
                hasPos = true;
            }
            break;
        case SF_PLAYER_VITALS:
            if (len >= 8) { float v[2]; memcpy(v, f, 8); health = v[0]; armor = v[1]; }
            break;
        case SF_WEAPON: if (len >= 4) memcpy(&weapon, f, 4); break;
        case SF_WORLD: if (len >= 8) memcpy(world, f, 8); break;
        default: break; // unknown (newer) field: skip
        }
        off += len;
    }
    // Validation & clamping
    pd.money = clampi(pd.money, 0, 100000000);
    pd.phase = clampi(pd.phase, 0, 3);
    for (int i = 0; i < W_COUNT; i++) {
        pd.reserve[i] = clampi(pd.reserve[i], 0, g_weapons[i].maxReserve);
        pd.clip[i] = clampi(pd.clip[i], 0, g_weapons[i].clipSize);
    }
    for (int i = 0; i < UP_COUNT; i++) pd.upgrades[i] = clampi(pd.upgrades[i], 0, 3);
    for (int i = 0; i < FAC_COUNT; i++) pd.rep[i] = clampi(pd.rep[i], -100, 100);
    if (!(pd.playTime >= 0.0f && pd.playTime < 1e8f)) pd.playTime = 0;
    if (!(pd.distanceDriven >= 0.0f && pd.distanceDriven < 1e9f)) pd.distanceDriven = 0;
    pd.hasWeapon[W_FISTS] = true;
    if (weapon < 0 || weapon >= W_CLAWS || !pd.hasWeapon[weapon]) weapon = W_FISTS;
    bool posOk = hasPos && pos.x == pos.x && pos.y == pos.y && pos.z == pos.z && fabsf(pos.x) < 800 && fabsf(pos.z) < 800 && pos.y > -150 && pos.y < 300;
    // Commit
    g.pd = pd;
    Actor& p = g.actors[PLAYER];
    if (posOk) { p.pos = pos; p.yaw = yaw; }
    else { p.pos = landmark_pos(LM_BUS_STOP); p.yaw = PI_F; }
    p.health = (health > 0 && health <= 400) ? health : 100.0f;
    p.armor = clampf(armor, 0, 100);
    p.weapon = weapon;
    p.clipAmmo = pd.clip[weapon];
    g.power = world[0] > 0.5f;
    g.rainTarget = clampf(world[1], 0, 1);
    return true;
}

int flag_get(const char* name) {
    for (int i = 0; i < g.pd.flagCount; i++)
        if (str_eq(g.pd.flags[i].name, name)) return g.pd.flags[i].value;
    return 0;
}

void flag_set(const char* name, int value) {
    for (int i = 0; i < g.pd.flagCount; i++)
        if (str_eq(g.pd.flags[i].name, name)) { g.pd.flags[i].value = value; return; }
    if (g.pd.flagCount >= MAX_FLAGS) return;
    str_copy(g.pd.flags[g.pd.flagCount].name, 24, name);
    g.pd.flags[g.pd.flagCount].value = value;
    g.pd.flagCount++;
}
