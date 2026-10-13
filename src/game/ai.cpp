// FIGHTO - NPC artificial intelligence: perception, civilians, gangs, fighters,
// police, A.E.G.I.S. allies, alien creatures and ambient population.
#include "game.h"
#include "../audio/audio.h"

bool ai_can_see(const Actor& a, vec3 p, float range) {
    vec3 eye = a.pos + vec3(0, a.height * 0.9f, 0);
    vec3 d = p - eye;
    float dist = length(d);
    if (dist > range) return false;
    if (dist < 2.5f) return true;
    vec3 f = dir_from_yaw(a.yaw);
    float c = dot(normalize(flat(d)), f);
    float fov = a.aiMode == AI_COMBAT ? -0.2f : 0.35f;
    if (c < fov) return false;
    RayHit h;
    if (world_raycast(eye, d / dist, dist - 0.3f, &h, true)) return false;
    return true;
}

static vec3 target_pos(const Actor& a) {
    if (a.target < 0) return a.pos;
    const Actor& t = g.actors[a.target];
    if (t.state == AS_VEHICLE && t.vehicle >= 0) return g.vehicles[t.vehicle].pos;
    return t.pos;
}

// Steering with simple obstacle avoidance. Returns true when close to the goal.
static bool ai_move_to(Actor& a, vec3 goal, float speed, float stopDist, float dt) {
    vec3 d = flat(goal - a.pos);
    float dist = length(d);
    if (dist < stopDist) {
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = length(flat(a.vel));
        return true;
    }
    vec3 dir = d / dist;
    if (a.kind != AK_DRONE) {
        RayHit h;
        vec3 o = a.pos + vec3(0, 0.6f, 0);
        if (world_raycast(o, dir, 1.4f, &h, false) && h.t < fminf(1.4f, dist)) {
            // Try turning left/right
            float best = -1;
            vec3 bestDir = dir;
            for (int k = 1; k <= 4; k++) {
                for (int s = -1; s <= 1; s += 2) {
                    float ang = s * k * 0.45f;
                    vec3 nd(dir.x * cosf(ang) + dir.z * sinf(ang), 0, -dir.x * sinf(ang) + dir.z * cosf(ang));
                    if (!world_raycast(o, nd, 1.6f, 0, false)) {
                        float score = dot(nd, dir);
                        if (score > best) { best = score; bestDir = nd; }
                    }
                }
                if (best > -1) break;
            }
            dir = bestDir;
        }
        // Stuck detection
        if (a.speed < 0.3f && speed > 1.0f) a.aiTimer2 += dt;
        else a.aiTimer2 = 0;
        if (a.aiTimer2 > 1.2f) {
            float ang = g.rng.range(-2.0f, 2.0f);
            dir = vec3(dir.x * cosf(ang) + dir.z * sinf(ang), 0, -dir.x * sinf(ang) + dir.z * cosf(ang));
            if (a.aiTimer2 > 1.8f) a.aiTimer2 = 0;
        }
    }
    a.yaw = angle_approach(a.yaw, yaw_from_dir(dir), 8.0f * dt);
    vec3 wish = dir * speed;
    if (a.kind == AK_DRONE) wish.y = (goal.y - a.pos.y) * 1.5f;
    actor_move(a, wish, dt, 8.0f);
    a.speed = length(flat(a.vel));
    return false;
}

void ai_make_hostile(int idx, int target) {
    Actor& a = g.actors[idx];
    if (!actor_alive(a) || a.knockedOut > 0) return;
    if (target == PLAYER) a.hostileToPlayer = true;
    a.aiMode = AI_COMBAT;
    a.target = target;
    a.alert = 1.0f;
    a.lastKnown = g.actors[target].pos;
    a.lastSeen = g.time;
    if (a.faction == FAC_KINGS || a.faction == FAC_VARGA || a.faction == FAC_OPERATIVES || a.faction == FAC_POLICE) {
        for (int i = 1; i < MAX_ACTORS; i++) {
            Actor& b = g.actors[i];
            if (i == idx || !actor_alive(b) || b.faction != a.faction || b.aiMode == AI_COMBAT || b.knockedOut > 0) continue;
            if (length(b.pos - a.pos) < 25.0f) {
                if (target == PLAYER) b.hostileToPlayer = true;
                b.aiMode = AI_COMBAT;
                b.target = target;
                b.alert = 1.0f;
                b.lastKnown = a.lastKnown;
                b.lastSeen = g.time;
            }
        }
    }
}

void ai_alert_area(vec3 pos, float radius, int threat, int type) {
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.state == AS_VEHICLE || a.knockedOut > 0 || i == threat) continue;
        float d = length(a.pos - pos);
        if (d > radius) continue;
        if (a.aiMode == AI_HOLD || a.aiMode == AI_SPECTATE || a.aiMode == AI_FOLLOW) {
            if (a.aiMode == AI_FOLLOW && threat >= 0 && threat != PLAYER && actor_is_enemy(a, g.actors[threat])) ai_make_hostile(i, threat);
            continue;
        }
        switch (a.kind) {
        case AK_CIVILIAN:
        case AK_STORY:
            if (a.kind == AK_STORY) break;
            if (type >= 1 || (type == 0 && d < 3.0f && g.wanted.level > 0)) {
                if (a.aiMode != AI_FLEE && a.aiMode != AI_COWER) {
                    a.aiMode = (d < 12.0f && g.rng.chance(0.35f)) ? AI_COWER : AI_FLEE;
                    a.aiTimer = g.rng.range(8.0f, 16.0f);
                    a.lastKnown = pos;
                    if (g.rng.chance(0.3f)) audio_play_at(SND_SCREAM, a.pos, 0.6f, g.rng.range(0.9f, 1.3f));
                }
            }
            break;
        default:
            if (threat < 0) break;
            if (a.aiMode == AI_COMBAT) break;
            if (actor_is_enemy(a, g.actors[threat]) || (threat == PLAYER && (a.hostileToPlayer || (a.faction == FAC_POLICE && g.wanted.level > 0)))) {
                if (ai_can_see(a, g.actors[threat].pos + vec3(0, 1, 0), 40.0f) || d < 8.0f) ai_make_hostile(i, threat);
                else { a.aiMode = AI_INVESTIGATE; a.goal = pos; a.aiTimer = 12.0f; a.alert = 0.6f; }
            } else if (type >= 1 && a.faction != FAC_AEGIS && a.faction != FAC_ALIEN) {
                a.aiMode = AI_INVESTIGATE;
                a.goal = pos;
                a.aiTimer = 10.0f;
                a.alert = 0.5f;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Behaviours
// ---------------------------------------------------------------------------
static int pick_enemy(int idx, Actor& a, float range) {
    int best = -1;
    float bestD = range;
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (i == idx) continue;
        Actor& t = g.actors[i];
        if (!actor_alive(t) || t.knockedOut > 0) continue;
        bool enemy = actor_is_enemy(a, t) || (i == PLAYER && a.hostileToPlayer);
        if (!enemy) continue;
        float d = length(t.pos - a.pos);
        if (d < bestD && (d < 6.0f || ai_can_see(a, t.pos + vec3(0, 1.2f, 0), range))) { bestD = d; best = i; }
    }
    return best;
}

static void civilian_wander(int idx, Actor& a, float dt) {
    if (a.navNode < 0) {
        a.navNode = world_nearest_walk_node(a.pos);
        a.navPrev = -1;
    }
    if (a.navNode < 0) { a.aiMode = AI_STAND; return; }
    const NavNode& n = g_world.walkNodes[a.navNode];
    float speed = 1.35f + (g.rain > 0.5f ? 0.4f : 0.0f) + hash_f01((u32)idx) * 0.3f;
    if (ai_move_to(a, n.pos, speed, 0.8f, dt)) {
        // Pick next link (avoid going straight back)
        int choices[6];
        int nc = 0;
        for (int k = 0; k < n.linkCount; k++) if (n.links[k] != a.navPrev) choices[nc++] = n.links[k];
        if (nc == 0) choices[nc++] = n.links[0];
        a.navPrev = a.navNode;
        a.navNode = choices[g.rng.irange(0, nc - 1)];
        if (g.rng.chance(0.08f)) { a.aiMode = AI_STAND; a.aiTimer = g.rng.range(3, 9); }
    }
}

static void do_flee(int idx, Actor& a, float dt) {
    vec3 away = flat(a.pos - a.lastKnown);
    if (length_sq(away) < 0.01f) away = dir_from_yaw(a.yaw);
    away = normalize(away);
    ai_move_to(a, a.pos + away * 10.0f, 5.6f, 0.1f, dt);
    a.aiTimer -= dt;
    if (a.aiTimer <= 0) { a.aiMode = a.kind == AK_CIVILIAN ? AI_WANDER : AI_STAND; a.navNode = -1; }
    FIGHTO_UNUSED(idx);
}

static int choose_melee_clip(Actor& a) {
    switch (a.app.body) {
    case BODY_STALKER: return g.rng.chance(0.25f) ? CLIP_LEAP : CLIP_CLAW;
    case BODY_BRUTE: return g.rng.chance(0.5f) ? CLIP_STOMP : CLIP_CLAW;
    case BODY_TENDRIL: return CLIP_CLAW;
    case BODY_OVERSEER: return g.rng.chance(0.5f) ? CLIP_STOMP : CLIP_CLAW;
    default: break;
    }
    float r = g.rng.f01();
    if (a.kind == AK_FIGHTER) {
        if (r < 0.25f) return CLIP_JAB;
        if (r < 0.45f) return CLIP_CROSS;
        if (r < 0.65f) return CLIP_HOOK;
        if (r < 0.8f) return CLIP_KICK;
        if (r < 0.9f) return CLIP_UPPERCUT;
        return CLIP_ELBOW;
    }
    if (r < 0.35f) return CLIP_JAB;
    if (r < 0.65f) return CLIP_CROSS;
    if (r < 0.85f) return CLIP_HOOK;
    return CLIP_KICK;
}

static void combat_melee(int idx, Actor& a, Actor& t, float dt) {
    vec3 tp = target_pos(a);
    float dist = length(flat(tp - a.pos));
    float runSpeed = a.app.body == BODY_STALKER ? 6.5f : a.app.body == BODY_BRUTE ? 3.2f : a.app.body == BODY_TENDRIL ? 2.8f : 4.6f;
    float engage = a.app.body == BODY_BRUTE ? 2.2f : a.app.body == BODY_STALKER ? 1.8f : 1.3f;
    // Defensive reactions against an attacking target
    if (t.state == AS_ATTACK && dist < 2.4f && a.state == AS_LOCOMOTION && a.app.body == BODY_HUMAN && a.aiTimer2 <= 0) {
        float r = g.rng.f01();
        if (r < a.skill * 0.45f) {
            a.state = AS_BLOCK;
            a.stateTime = 0;
            a.blockStart = g.time - 0.1f;
            actor_set_clip(a, CLIP_BLOCK);
        } else if (r < a.skill * 0.6f && a.stamina > 20) {
            a.state = AS_DODGE;
            a.stateTime = 0;
            a.iframes = 0.3f;
            actor_set_clip(a, CLIP_DODGE);
            vec3 side(cosf(a.yaw), 0, -sinf(a.yaw));
            a.vel = side * (g.rng.chance(0.5f) ? 6.0f : -6.0f);
            a.stamina -= 20;
        }
        a.aiTimer2 = 0.8f;
    }
    a.aiTimer2 = fmaxf(0.0f, a.aiTimer2 - dt);
    if (a.state != AS_LOCOMOTION) {
        if (a.state == AS_ATTACK) {
            actor_face(a, tp, 5.0f, dt);
            vec3 lunge = a.attackClip == CLIP_LEAP ? dir_from_yaw(a.yaw) * 7.0f : vec3(0, 0, 0);
            actor_move(a, lunge, dt, 6.0f);
        } else if (a.state == AS_DODGE) {
            actor_move(a, a.vel, dt, 3.0f);
        } else {
            actor_move(a, a.knockVel, dt, 6.0f);
            a.knockVel = a.knockVel * (1.0f - fminf(1.0f, dt * 5.0f));
        }
        a.speed = length(flat(a.vel));
        return;
    }
    // Stalkers pounce from range
    if (a.app.body == BODY_STALKER && dist > 3.5f && dist < 7.5f && a.attackCooldown <= 0 && g.rng.chance(dt * 1.5f)) {
        actor_face(a, tp, 100.0f, 1.0f);
        combat_start_attack(a, CLIP_LEAP, true);
        a.attackCooldown = 2.5f;
        audio_play_at(SND_ALIEN_SCREECH, a.pos, 0.9f, 1.2f);
        return;
    }
    if (dist > engage + 0.4f) {
        ai_move_to(a, tp, dist > 6.0f ? runSpeed : runSpeed * 0.6f, engage, dt);
    } else {
        // Circle and look for an opening
        actor_face(a, tp, 9.0f, dt);
        a.scriptTimer -= dt;
        if (a.scriptTimer <= 0) { a.scriptTimer = g.rng.range(0.8f, 2.0f); a.goal.x = g.rng.chance(0.5f) ? 1.0f : -1.0f; }
        vec3 side(cosf(a.yaw), 0, -sinf(a.yaw));
        vec3 wish = side * (a.goal.x * 1.2f);
        if (dist < engage - 0.3f) wish = wish - dir_from_yaw(a.yaw) * 1.5f;
        actor_move(a, a.kind == AK_FIGHTER || a.app.body == BODY_HUMAN ? wish : vec3(0, 0, 0), dt, 6.0f);
        a.speed = length(flat(a.vel));
        float cooldown = lerpf(1.6f, 0.55f, a.aggression * a.skill) * (g.settings.difficulty == 0 ? 1.5f : g.settings.difficulty == 2 ? 0.75f : 1.0f);
        if (a.attackCooldown <= 0 && a.stamina > 8) {
            combat_start_attack(a, choose_melee_clip(a), false);
            a.attackHeavy = a.attackClip == CLIP_KICK || a.attackClip == CLIP_UPPERCUT || a.attackClip == CLIP_STOMP || a.attackClip == CLIP_LEAP;
            a.attackCooldown = cooldown * g.rng.range(0.7f, 1.3f);
            if (a.faction == FAC_ALIEN && g.rng.chance(0.3f)) audio_play_at(SND_ALIEN_GROWL, a.pos, 0.8f, g.rng.range(0.8f, 1.2f));
        }
    }
    a.stamina = fminf(a.maxStamina, a.stamina + 15.0f * dt);
    FIGHTO_UNUSED(idx);
}

static void combat_ranged(int idx, Actor& a, Actor& t, float dt) {
    vec3 tp = target_pos(a);
    vec3 aimP = t.state == AS_VEHICLE ? tp + vec3(0, 0.8f, 0) : actor_chest(t);
    float dist = length(tp - a.pos);
    bool sees = (g.frame + idx) % 4 == 0 ? ai_can_see(a, aimP, 90.0f) : (g.time - a.lastSeen < 0.3f);
    if (sees) { a.lastSeen = g.time; a.lastKnown = tp; }
    if (a.state != AS_LOCOMOTION) {
        actor_move(a, a.knockVel, dt, 6.0f);
        a.knockVel = a.knockVel * (1.0f - fminf(1.0f, dt * 5.0f));
        a.speed = length(flat(a.vel));
        a.aimBlend = approach(a.aimBlend, 0.0f, dt * 5.0f);
        return;
    }
    float desired = a.kind == AK_DRONE ? 9.0f : (a.kind == AK_POLICE ? 10.0f : 13.0f);
    if (a.weapon == W_SHOTGUN) desired = 6.0f;
    vec3 wish(0, 0, 0);
    if (!sees && g.time - a.lastSeen > 1.0f) {
        // Close in on the last known position
        ai_move_to(a, a.lastKnown, 4.2f, 2.0f, dt);
        a.aimBlend = approach(a.aimBlend, 0.3f, dt * 3.0f);
        return;
    }
    actor_face(a, tp, 7.0f, dt);
    a.scriptTimer -= dt;
    if (a.scriptTimer <= 0) {
        a.scriptTimer = g.rng.range(1.0f, 3.0f);
        a.goal.x = g.rng.range(-1.0f, 1.0f);
    }
    vec3 fwd = dir_from_yaw(a.yaw);
    vec3 side(cosf(a.yaw), 0, -sinf(a.yaw));
    if (dist > desired + 4.0f) wish = fwd * 3.5f;
    else if (dist < desired - 4.0f) wish = -fwd * 2.5f;
    wish = wish + side * (a.goal.x * 2.2f);
    if (a.kind == AK_DRONE) {
        vec3 goal = tp + vec3(sinf(g.time * 0.7f + idx) * 8.0f, 7.0f + sinf(g.time + idx) * 1.5f, cosf(g.time * 0.6f + idx) * 8.0f);
        ai_move_to(a, goal, 6.0f, 0.5f, dt);
        actor_face(a, tp, 6.0f, dt);
    } else {
        actor_move(a, wish, dt, 6.0f);
        a.speed = length(flat(a.vel));
    }
    a.aimBlend = approach(a.aimBlend, 1.0f, dt * 4.0f);
    // Fire in bursts
    const WeaponDef& wd = g_weapons[a.weapon];
    if (a.reloadTimer > 0) {
        a.reloadTimer -= dt;
        return;
    }
    if (a.clipAmmo <= 0) { weapons_try_reload(idx); return; }
    if (sees && a.fireCooldown <= 0 && a.aimBlend > 0.7f) {
        if (a.burst <= 0) {
            a.burst = wd.automatic ? g.rng.irange(3, 7) : g.rng.irange(1, 3);
            a.fireCooldown = g.rng.range(0.6f, 1.6f) / (0.5f + a.aggression);
            return;
        }
        vec3 origin = a.kind == AK_DRONE ? a.pos - vec3(0, 0.3f, 0) : actor_hand(a, true) + vec3(0, 0.05f, 0);
        // Aim with lead and accuracy based error
        vec3 lead = t.vel * fminf(dist / 120.0f, 0.3f);
        vec3 dir = normalize(aimP + lead - origin);
        weapons_fire(idx, origin, dir, true);
        a.clipAmmo--;
        a.burst--;
        a.fireCooldown = wd.fireInterval * (wd.automatic ? 1.3f : 2.2f);
    }
}

static void police_arrest(int idx, Actor& a, float dt) {
    Actor& p = g.actors[PLAYER];
    vec3 tp = p.pos;
    float d = length(flat(tp - a.pos));
    if (p.state == AS_VEHICLE || g.wanted.level >= 2 || p.state == AS_ATTACK) { a.aiMode = AI_COMBAT; a.target = PLAYER; return; }
    if (ai_move_to(a, tp, 4.0f, 1.3f, dt)) {
        actor_face(a, tp, 8.0f, dt);
        a.aimBlend = approach(a.aimBlend, 1.0f, dt * 3.0f);
        a.aiTimer += dt;
        if (a.shout <= 0) { a.shout = 4.0f; notify("POLICE: \"Hands where I can see them!\"", vec4(0.6f, 0.75f, 1.0f, 1)); }
        if (a.aiTimer > 2.2f && d < 1.6f && p.speed < 0.5f) {
            game_respawn_player(true);
        }
    } else {
        a.aiTimer = 0;
    }
    FIGHTO_UNUSED(idx);
}

void ai_update(int idx, Actor& a, float dt) {
    if (a.state == AS_VEHICLE) { a.speed = 0; return; }
    if (a.state == AS_TAKEN_DOWN) { a.speed = 0; return; }
    if (a.knockedOut > 0) return;
    // Stagger / knockdown states move with knockback only
    bool reacting = a.state == AS_HIT || a.state == AS_STAGGER || a.state == AS_KNOCKDOWN || a.state == AS_GETUP;
    if (reacting && a.aiMode != AI_COMBAT) {
        actor_move(a, a.knockVel, dt, 6.0f);
        a.knockVel = a.knockVel * (1.0f - fminf(1.0f, dt * 5.0f));
        a.speed = length(flat(a.vel));
        return;
    }
    a.alert = fmaxf(0.0f, a.alert - dt * 0.02f);
    // Hostile perception: guards / gangs / aliens spot enemies
    if (a.aiMode != AI_COMBAT && a.aiMode != AI_FLEE && a.aiMode != AI_COWER && a.aiMode != AI_HOLD && a.kind != AK_CIVILIAN && (g.frame + idx) % 10 == 0) {
        float range = a.faction == FAC_ALIEN ? 35.0f : 28.0f;
        if (a.aiMode == AI_SPECTATE) range = 0;
        if (range > 0) {
            int e = pick_enemy(idx, a, range);
            if (e >= 0) {
                if (e == PLAYER && g.actors[PLAYER].crouch > 0.5f && length(g.actors[PLAYER].pos - a.pos) > 7.0f) {
                    // Stealth: crouching players are noticed gradually
                    a.alert += 0.35f;
                    if (a.alert > 1.0f) ai_make_hostile(idx, e);
                } else {
                    ai_make_hostile(idx, e);
                }
            }
        }
    }
    switch (a.aiMode) {
    case AI_WANDER:
        civilian_wander(idx, a, dt);
        a.aimBlend = 0;
        break;
    case AI_STAND:
    case AI_GUARD:
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = length(flat(a.vel));
        if (a.aiMode == AI_GUARD && length(flat(a.home - a.pos)) > 1.5f) ai_move_to(a, a.home, 2.0f, 0.5f, dt);
        a.aiTimer -= dt;
        if (a.kind == AK_CIVILIAN && a.aiTimer <= 0) { a.aiMode = AI_WANDER; a.navNode = -1; }
        break;
    case AI_TALK:
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = 0;
        if (a.clip < 0 && g.rng.chance(dt * 0.5f)) actor_set_clip(a, CLIP_TALK);
        break;
    case AI_SPECTATE: {
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = 0;
        actor_face(a, a.goal, 3.0f, dt);
        if (a.clip < 0 && g.rng.chance(dt * 0.4f)) actor_set_clip(a, g.rng.chance(0.5f) ? CLIP_POINT : CLIP_TALK, 0.2f, 1.3f);
    } break;
    case AI_FLEE:
        do_flee(idx, a, dt);
        break;
    case AI_COWER:
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = 0;
        if (a.clip != CLIP_COWER) actor_set_clip(a, CLIP_COWER);
        a.aiTimer -= dt;
        if (a.aiTimer <= 0) { a.clip = -1; a.aiMode = AI_FLEE; a.aiTimer = 6.0f; }
        break;
    case AI_INVESTIGATE:
        if (ai_move_to(a, a.goal, 2.6f, 1.5f, dt)) {
            a.yaw += dt * 0.8f;
        }
        a.aiTimer -= dt;
        if (a.aiTimer <= 0) { a.aiMode = AI_GUARD; a.alert = 0.2f; }
        break;
    case AI_SEARCH:
        if (ai_move_to(a, a.goal, 2.8f, 1.5f, dt)) a.goal = a.lastKnown + vec3(g.rng.range(-8, 8), 0, g.rng.range(-8, 8));
        a.aiTimer -= dt;
        if (a.aiTimer <= 0) a.aiMode = a.kind == AK_POLICE ? AI_PATROL : AI_GUARD;
        break;
    case AI_PATROL:
        if (ai_move_to(a, a.goal, 1.6f, 1.0f, dt)) a.goal = a.home + vec3(g.rng.range(-10, 10), 0, g.rng.range(-10, 10));
        break;
    case AI_GOTO:
        if (ai_move_to(a, a.goal, a.scriptTimer > 0 ? a.scriptTimer : 4.2f, 0.8f, dt)) a.aiMode = AI_HOLD;
        break;
    case AI_FOLLOW: {
        int e = (g.frame + idx) % 15 == 0 ? pick_enemy(idx, a, 25.0f) : -1;
        if (e >= 0) { a.aiMode = AI_COMBAT; a.target = e; break; }
        vec3 lp = g.actors[PLAYER].pos;
        float d = length(flat(lp - a.pos));
        if (d > 3.5f) ai_move_to(a, lp, d > 8.0f ? 5.5f : 3.0f, 2.5f, dt);
        else { actor_move(a, vec3(0, 0, 0), dt, 8.0f); a.speed = length(flat(a.vel)); }
        a.aimBlend = approach(a.aimBlend, 0.0f, dt * 3.0f);
    } break;
    case AI_ARREST:
        police_arrest(idx, a, dt);
        break;
    case AI_COMBAT: {
        if (a.target < 0 || !g.actors[a.target].active || !actor_alive(g.actors[a.target]) || g.actors[a.target].knockedOut > 0) {
            int e = pick_enemy(idx, a, 40.0f);
            if (e >= 0) { a.target = e; }
            else {
                a.target = -1;
                a.aiMode = a.faction == FAC_AEGIS ? AI_FOLLOW : AI_SEARCH;
                a.aiTimer = 10.0f;
                a.goal = a.lastKnown;
                a.aimBlend = 0;
                break;
            }
        }
        // Police at one star try to arrest instead of shooting
        if (a.kind == AK_POLICE && a.target == PLAYER && g.wanted.level <= 1 && g.actors[PLAYER].state != AS_VEHICLE) {
            a.aiMode = AI_ARREST;
            a.aiTimer = 0;
            break;
        }
        // Retarget allies to the nearest threat occasionally
        if ((g.frame + idx) % 30 == 0 && a.faction == FAC_AEGIS) {
            int e = pick_enemy(idx, a, 35.0f);
            if (e >= 0) a.target = e;
        }
        Actor& t = g.actors[a.target];
        if (a.target == PLAYER && g.time - a.lastSeen > 25.0f && a.faction != FAC_ALIEN) {
            a.aiMode = AI_SEARCH;
            a.aiTimer = 15.0f;
            a.goal = a.lastKnown;
            break;
        }
        if (a.weapon == W_FISTS || a.weapon == W_CLAWS) {
            a.lastSeen = g.time;
            combat_melee(idx, a, t, dt);
        } else {
            combat_ranged(idx, a, t, dt);
            // Too close: switch to a melee strike
            if (length(t.pos - a.pos) < 1.6f && a.state == AS_LOCOMOTION && a.attackCooldown <= 0 && a.kind != AK_DRONE) {
                combat_start_attack(a, CLIP_ELBOW, false);
                a.attackCooldown = 1.5f;
            }
        }
    } break;
    case AI_HOLD:
    default:
        actor_move(a, vec3(0, 0, 0), dt, 8.0f);
        a.speed = length(flat(a.vel));
        break;
    }
}

// ---------------------------------------------------------------------------
// Ambient population
// ---------------------------------------------------------------------------
static bool visible_to_camera(vec3 p) {
    vec2 s;
    if (!renderer_project(p + vec3(0, 1, 0), &s)) return false;
    return s.x > -50 && s.y > -50 && s.x < g.width + 50 && s.y < g.height + 50;
}

void ai_population_update(float dt) {
    g.spawnTimer -= dt;
    Actor& p = g.actors[PLAYER];
    // Despawn distant ambient actors
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active || a.persistent || a.tag[0]) continue;
        if (a.state == AS_VEHICLE) continue;
        float d = length(a.pos - p.pos);
        if (d > 140.0f || (d > 90.0f && !visible_to_camera(a.pos))) actor_despawn(i);
    }
    if (g.spawnTimer > 0) return;
    g.spawnTimer = 0.4f;
    if (p.pos.y < -20.0f) return;  // underground: scripted only
    int count = 0, aliens = 0, gang = 0;
    for (int i = 1; i < MAX_ACTORS; i++) {
        if (!g.actors[i].active || g.actors[i].tag[0]) continue;
        count++;
        if (g.actors[i].faction == FAC_ALIEN) aliens++;
        if (g.actors[i].faction == FAC_KINGS) gang++;
    }
    int district = district_at(p.pos);
    int maxPeds = plat_is_mobile() ? 18 : 34;
    if (district == D_DOWNTOWN) maxPeds += 8;
    if (district == D_IRONWORKS || district == D_WESTHILLS) maxPeds -= 12;
    if (g.pd.phase == 2) maxPeds = maxPeds / 2;
    if (g.pd.phase >= 3) maxPeds = maxPeds / 3;
    if (count >= maxPeds) return;
    // Spawn on a sidewalk node 45-90 m away
    for (int tries = 0; tries < 8; tries++) {
        int n = g.rng.irange(0, g_world.walkNodes.count - 1);
        vec3 sp = g_world.walkNodes[n].pos;
        float d = length(sp - p.pos);
        if (d < 40.0f || d > 95.0f) continue;
        if (visible_to_camera(sp) && d < 70.0f) continue;
        int dist2 = district_at(sp);
        int kind = AK_CIVILIAN;
        const char* preset = "civilian";
        if (dist2 == D_BLOCKS && gang < 6 && g.rng.chance(0.22f) && g.pd.phase < 3) { kind = AK_THUG; preset = "kings"; }
        int id;
        if (g.pd.phase >= 3 && aliens < 5 && g.rng.chance(0.25f)) {
            id = actor_spawn_preset(g.rng.chance(0.85f) ? AK_STALKER : AK_DRONE, "stalker", sp + vec3(0, 0.0f, 0), g.rng.range(0, TAU_F), g.rng.next());
            if (id >= 0) { g.actors[id].aiMode = AI_PATROL; g.actors[id].goal = sp; if (g.actors[id].kind == AK_DRONE) g.actors[id].pos.y += 8.0f; }
            break;
        }
        id = actor_spawn_preset(kind, preset, sp, g.rng.range(0, TAU_F), g.rng.next());
        if (id < 0) break;
        Actor& a = g.actors[id];
        a.navNode = n;
        if (kind == AK_THUG) {
            a.aiMode = g.rng.chance(0.5f) ? AI_TALK : AI_GUARD;
            a.home = sp;
            if (g.rng.chance(0.35f)) a.weapon = W_PISTOL, a.kind = AK_GUNMAN, a.clipAmmo = 15;
        } else if (g.pd.phase >= 3) {
            a.aiMode = AI_FLEE;
            a.aiTimer = 30.0f;
            a.lastKnown = sp + vec3(g.rng.range(-5, 5), 0, g.rng.range(-5, 5));
        }
        break;
    }
}
