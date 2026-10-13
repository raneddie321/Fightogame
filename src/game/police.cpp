// FIGHTO - crime, witnesses and the wanted system (Blackridge PD response).
#include "game.h"
#include "../audio/audio.h"

static bool police_witness(vec3 pos) {
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.faction != FAC_POLICE) continue;
        if (length(a.pos - pos) < 45.0f && (a.state == AS_VEHICLE || ai_can_see(a, pos + vec3(0, 1, 0), 45.0f))) return true;
    }
    return false;
}

static bool civilian_witness(vec3 pos) {
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.faction != FAC_CIVILIAN) continue;
        if (length(a.pos - pos) < 25.0f) return true;
    }
    return false;
}

void wanted_set(int level) {
    level = clamp_t(level, 0, 5);
    if (level > g.wanted.level) {
        g.wanted.flash = 2.0f;
        g.wanted.spawnTimer = fminf(g.wanted.spawnTimer, 2.0f);
    }
    g.wanted.level = level;
    g.wanted.heat = (float)level;
    if (level > 0) {
        g.wanted.lastKnown = g.actors[PLAYER].pos;
        g.wanted.seen = true;
        g.wanted.sinceSeen = 0;
        g.wanted.searchTimer = 0;
    }
}

void wanted_clear() {
    g.wanted.level = 0;
    g.wanted.heat = 0;
    g.wanted.searchTimer = 0;
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (a.active && a.faction == FAC_POLICE) {
            a.hostileToPlayer = false;
            if (a.aiMode == AI_COMBAT || a.aiMode == AI_ARREST || a.aiMode == AI_SEARCH) { a.aiMode = AI_PATROL; a.goal = a.pos; a.home = a.pos; }
        }
    }
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (v.active && v.type == VT_POLICE && v.ai == VAI_CHASE) { v.ai = VAI_TRAFFIC; v.siren = false; v.toK = -1; }
    }
}

void wanted_crime(int type, vec3 pos, bool witnessed) {
    // Story phases with A.E.G.I.S. cover suppress police interest in alien fights
    if (g.pd.phase >= 3) return;
    if (g.wanted.crimeCooldown > 0 && type != CRIME_COP_MURDER && type != CRIME_MURDER) return;
    bool seen = witnessed || police_witness(pos);
    bool reported = !seen && civilian_witness(pos);
    if (!seen && !reported) return;
    int add = 0;
    switch (type) {
    case CRIME_ASSAULT: add = 1; break;
    case CRIME_SHOOTING: add = 1; break;
    case CRIME_CARJACK: add = 1; break;
    case CRIME_VEHICLE_HIT: add = 1; break;
    case CRIME_MURDER: add = 2; break;
    case CRIME_COP_ASSAULT: add = 2; break;
    case CRIME_COP_MURDER: add = 3; break;
    }
    int target = g.wanted.level == 0 ? add : g.wanted.level + (add >= 2 ? 1 : 0);
    if (type == CRIME_SHOOTING && g.wanted.level >= 1) target = max_t(g.wanted.level, 2);
    target = min_t(target, 5);
    g.wanted.crimeCooldown = 1.5f;
    if (target > g.wanted.level) {
        wanted_set(target);
        if (reported && !seen) notify("A witness called the police", vec4(0.6f, 0.75f, 1.0f, 1));
    }
}

static int count_police(int* cars) {
    int n = 0;
    *cars = 0;
    for (int i = 1; i < MAX_ACTORS; i++) if (g.actors[i].active && g.actors[i].faction == FAC_POLICE && actor_alive(g.actors[i])) n++;
    for (int i = 0; i < MAX_VEHICLES; i++) if (g.vehicles[i].active && g.vehicles[i].type == VT_POLICE && g.vehicles[i].ai == VAI_CHASE) (*cars)++;
    return n;
}

static void dispatch_unit() {
    Actor& p = g.actors[PLAYER];
    if (p.pos.y < -20.0f) return;
    // Spawn on a road 60-110 m away, preferably out of sight
    for (int tries = 0; tries < 10; tries++) {
        float ang = g.rng.range(0, TAU_F);
        vec3 sp = world_nearest_road_point(p.pos + vec3(sinf(ang), 0, cosf(ang)) * g.rng.range(60.0f, 110.0f));
        if (length(sp - p.pos) < 45.0f) continue;
        vec3 to = p.pos - sp;
        int vi = vehicle_spawn(VT_POLICE, sp, atan2f(to.x, to.z), vec3(1, 1, 1), VAI_CHASE);
        if (vi < 0) return;
        Vehicle& v = g.vehicles[vi];
        v.siren = true;
        int d = actor_spawn_preset(AK_POLICE, "police", sp, v.yaw, g.rng.next());
        if (d >= 0) {
            Actor& a = g.actors[d];
            a.state = AS_VEHICLE;
            a.vehicle = vi;
            a.hostileToPlayer = true;
            a.target = PLAYER;
            if (g.wanted.level >= 3) { a.weapon = W_SMG; a.clipAmmo = 30; }
            if (g.wanted.level >= 4) { a.weapon = W_RIFLE; a.clipAmmo = 30; a.maxHealth = a.health = 140; a.armor = 50; }
            v.driver = d;
        }
        return;
    }
}

void wanted_update(float dt) {
    Wanted& w = g.wanted;
    w.crimeCooldown = fmaxf(0.0f, w.crimeCooldown - dt);
    w.flash = fmaxf(0.0f, w.flash - dt);
    float sirenVol = 0;
    // Siren audio from the nearest chasing car
    float best = 1e9f;
    vec3 sirenPos(0, 0, 0);
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active || !v.siren) continue;
        float d = length(v.pos - g.cam.pos);
        if (d < best) { best = d; sirenPos = v.pos; }
    }
    if (best < 200.0f) sirenVol = 1.0f;
    audio_siren(sirenPos, sirenVol);
    if (w.level <= 0) return;
    Actor& p = g.actors[PLAYER];
    // Is the player currently seen by any officer?
    bool seen = false;
    for (int i = 1; i < MAX_ACTORS && !seen; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.faction != FAC_POLICE) continue;
        float d = length(a.pos - p.pos);
        if (d < 18.0f || (d < 60.0f && (g.frame + i) % 6 == 0 && ai_can_see(a, p.pos + vec3(0, 1.2f, 0), 60.0f))) seen = true;
        if (a.state == AS_VEHICLE && d < 35.0f) seen = true;
    }
    if (seen) {
        w.seen = true;
        w.sinceSeen = 0;
        w.lastKnown = p.pos;
        w.searchTimer = 0;
    } else {
        w.sinceSeen += dt;
        if (w.sinceSeen > 4.0f) {
            w.seen = false;
            w.searchTimer += dt;
            // Search area: chasers drive to the last known position
            float needed = 10.0f + w.level * 6.0f;
            if (w.searchTimer > needed) {
                notify("You lost the police", vec4(0.6f, 1.0f, 0.6f, 1));
                wanted_clear();
                audio_play(SND_CHECKPOINT, 0.6f);
                return;
            }
        }
    }
    // Keep police pressure proportional to the level
    w.spawnTimer -= dt;
    if (w.spawnTimer <= 0 && w.seen) {
        w.spawnTimer = 9.0f - w.level;
        int cars;
        int officers = count_police(&cars);
        int wantCars = 1 + w.level / 2 + (w.level >= 4 ? 1 : 0);
        if (cars < wantCars && officers < 4 + w.level * 3) dispatch_unit();
    }
}
