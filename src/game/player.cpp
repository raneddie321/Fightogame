// FIGHTO - Damian Cross: third-person controller, melee, firearms, interaction.
#include "game.h"
#include "../audio/audio.h"

static const int g_comboClips[4] = {CLIP_JAB, CLIP_CROSS, CLIP_HOOK, CLIP_UPPERCUT};
static float g_dodgeTimer;
static vec3 g_dodgeDir;
static float g_footstepPhase;
static float g_enterTimer;
static int g_enterVehicle = -1;
static int g_takedownVictim = -1;

float player_max_health() { return 100.0f + g.pd.upgrades[UP_HEALTH] * 25.0f; }
float player_max_stamina() { return 100.0f + g.pd.upgrades[UP_STAMINA] * 25.0f; }

void player_heal(float amount) {
    Actor& p = g.actors[PLAYER];
    p.health = fminf(p.maxHealth, p.health + amount);
}

int player_current_weapon() { return g.actors[PLAYER].weapon; }

void player_select_weapon(int w) {
    Actor& p = g.actors[PLAYER];
    if (w < 0 || w >= W_CLAWS || !g.pd.hasWeapon[w]) return;
    if (p.weapon == w) return;
    g.pd.clip[p.weapon] = p.clipAmmo;
    p.weapon = w;
    p.clipAmmo = g.pd.clip[w];
    p.reloadTimer = 0;
    if (p.clip == CLIP_RELOAD) p.clip = -1;
    audio_play(SND_RELOAD, 0.4f, 1.4f);
}

void player_give_weapon(int w, int ammo) {
    if (w <= W_FISTS || w >= W_CLAWS) return;
    bool had = g.pd.hasWeapon[w];
    g.pd.hasWeapon[w] = true;
    g.pd.reserve[w] = min_t(g.pd.reserve[w] + ammo, g_weapons[w].maxReserve);
    if (!had) {
        g.pd.clip[w] = g_weapons[w].clipSize;
        char buf[96];
        fmt_str(buf, sizeof(buf), "Acquired: %s", g_weapons[w].name);
        notify(buf, vec4(1.0f, 0.85f, 0.4f, 1));
        player_select_weapon(w);
    }
}

void player_teleport(vec3 p, float yaw) {
    Actor& a = g.actors[PLAYER];
    a.pos = p;
    a.yaw = a.desiredYaw = yaw;
    a.vel = vec3(0, 0, 0);
    g.cam.yaw = yaw;
    g.cam.smoothFocus = p + vec3(0, 1.6f, 0);
}

void player_reset(bool newGame) {
    Appearance app = appearance_preset("damian", 1);
    vec3 start = landmark_pos(LM_BUS_STOP);
    actor_spawn(AK_PLAYER, start, PI_F, app, FAC_PLAYER);
    Actor& p = g.actors[PLAYER];
    p.maxHealth = p.health = player_max_health();
    p.maxStamina = p.stamina = player_max_stamina();
    p.persistent = true;
    str_copy(p.name, sizeof(p.name), "Damian Cross");
    if (newGame) {
        memset(&g.pd, 0, sizeof(g.pd));
        g.pd.money = 60;
        g.pd.hasWeapon[W_FISTS] = true;
    }
    p.weapon = W_FISTS;
    g.lockTarget = -1;
    g_dodgeTimer = 0;
    g_enterVehicle = -1;
    g_takedownVictim = -1;
    player_teleport(start, PI_F);
}

void player_enter_vehicle(int v) {
    Actor& p = g.actors[PLAYER];
    Vehicle& ve = g.vehicles[v];
    if (ve.driver >= 0 && ve.driver != PLAYER) {
        // Carjacking: drag the driver out
        int d = ve.driver;
        Actor& dr = g.actors[d];
        bool ok;
        dr.pos = vehicle_exit_pos(ve, &ok);
        dr.state = AS_LOCOMOTION;
        dr.vehicle = -1;
        ve.driver = -1;
        if (dr.faction == FAC_POLICE) wanted_crime(CRIME_COP_ASSAULT, p.pos, true);
        else wanted_crime(CRIME_CARJACK, p.pos, false);
        ai_alert_area(p.pos, 10.0f, PLAYER, 1);
        dr.aiMode = dr.faction == FAC_CIVILIAN ? AI_FLEE : AI_COMBAT;
        dr.target = PLAYER;
        actor_set_clip(dr, CLIP_STAGGER);
        audio_play_at(SND_GRUNT, dr.pos);
    }
    ve.driver = PLAYER;
    ve.ai = VAI_NONE;
    ve.locked = false;
    p.vehicle = v;
    p.state = AS_VEHICLE;
    p.clip = -1;
    p.aimBlend = 0;
    g.lockTarget = -1;
    g.cam.vehicleYaw = ve.yaw;
    audio_play_at(SND_CAR_DOOR, ve.pos);
}

void player_exit_vehicle() {
    Actor& p = g.actors[PLAYER];
    if (p.vehicle < 0) return;
    Vehicle& ve = g.vehicles[p.vehicle];
    bool ok = false;
    vec3 ex = vehicle_exit_pos(ve, &ok);
    if (!ok) {
        notify("No room to get out here", vec4(1, 0.6f, 0.4f, 1));
        return;
    }
    float spd = length(ve.vel);
    ve.driver = -1;
    ve.ai = VAI_PARKED;
    ve.throttle = 0;
    ve.handbrake = true;
    p.vehicle = -1;
    p.state = AS_LOCOMOTION;
    p.pos = ex;
    p.vel = ve.vel * 0.6f;
    p.yaw = ve.yaw;
    audio_play_at(SND_CAR_DOOR, ve.pos);
    if (spd > 9.0f) {
        // Bailing out of a moving car hurts
        combat_damage(PLAYER, -1, spd * 1.2f, p.pos, normalize(ve.vel), DMG_FALL, B_PELVIS);
        actor_set_clip(p, CLIP_KNOCKDOWN);
        p.state = AS_KNOCKDOWN;
        p.stateTime = 0;
    }
}

static int find_lock_target() {
    Actor& p = g.actors[PLAYER];
    vec3 fwd = camera_forward();
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.state == AS_VEHICLE) continue;
        bool enemy = actor_is_enemy(p, a) || a.aiMode == AI_COMBAT || (a.kind != AK_CIVILIAN && a.kind != AK_STORY);
        if (!enemy) continue;
        vec3 d = a.pos - p.pos;
        float dist = length(d);
        if (dist > 18.0f) continue;
        float facing = dot(normalize(flat(d)), normalize(flat(fwd)));
        if (facing < 0.3f) continue;
        float score = dist * (1.6f - facing);
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

// Finds a melee target in front of the player for auto-aim lunges.
static int melee_assist_target(float range) {
    Actor& p = g.actors[PLAYER];
    if (g.lockTarget >= 0 && actor_alive(g.actors[g.lockTarget]) && actor_dist(PLAYER, g.lockTarget) < range) return g.lockTarget;
    vec3 fwd = dir_from_yaw(p.yaw);
    vec3 camf = flat(camera_forward());
    int best = -1;
    float bestD = range;
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.state == AS_VEHICLE) continue;
        vec3 d = a.pos - p.pos;
        float dist = length(flat(d));
        if (dist > bestD || fabsf(d.y) > 1.5f) continue;
        vec3 dn = normalize(flat(d));
        if (dot(dn, fwd) < 0.2f && dot(dn, normalize(camf)) < 0.4f) continue;
        bestD = dist;
        best = i;
    }
    return best;
}

static void start_light_attack(Actor& p) {
    int clip = g_comboClips[p.combo % 4];
    if (p.combo % 4 == 3) {
        combat_start_attack(p, clip, true);
    } else {
        combat_start_attack(p, clip, false);
    }
    p.combo++;
    int t = melee_assist_target(2.6f);
    if (t >= 0) {
        actor_face(p, g.actors[t].pos, 50.0f, 1.0f);
        float d = actor_dist(PLAYER, t);
        if (d > 1.1f) p.vel = p.vel + dir_from_yaw(p.yaw) * fminf((d - 1.0f) * 6.0f, 6.0f);
    } else {
        // Attack in the camera direction when nothing is near
        vec3 cf = flat(camera_forward());
        if (length(g.ctl.move) > 0.1f) {
            vec3 fwd = dir_from_yaw(g.cam.yaw), right(-cosf(g.cam.yaw), 0, sinf(g.cam.yaw));
            cf = fwd * g.ctl.move.y + right * g.ctl.move.x;
        }
        if (length(cf) > 0.01f) p.yaw = p.desiredYaw = yaw_from_dir(cf);
    }
}

static void player_on_foot(Actor& p, float dt) {
    const Controls& c = g.ctl;
    float camYaw = g.cam.yaw;
    vec3 fwd = dir_from_yaw(camYaw);
    vec3 right(-cosf(camYaw), 0, sinf(camYaw));
    vec3 wish = fwd * c.move.y + right * c.move.x;
    float wl = length(wish);
    if (wl > 1.0f) { wish = wish / wl; wl = 1.0f; }
    bool armed = p.weapon != W_FISTS;
    bool aiming = armed && c.down[ACT_AIM] && p.state == AS_LOCOMOTION;
    bool blocking = !armed && c.down[ACT_AIM] && (p.state == AS_LOCOMOTION || p.state == AS_BLOCK);
    p.aiTimer2 = fmaxf(0.0f, p.aiTimer2 - dt);

    // Lock-on
    if (c.pressed[ACT_LOCKON]) {
        if (g.lockTarget >= 0) g.lockTarget = -1;
        else g.lockTarget = find_lock_target();
    }
    if (g.lockTarget >= 0 && (!actor_alive(g.actors[g.lockTarget]) || actor_dist(PLAYER, g.lockTarget) > 25.0f || g.actors[g.lockTarget].knockedOut > 0)) g.lockTarget = -1;

    // Crouch toggle
    if (c.pressed[ACT_CROUCH] && p.state == AS_LOCOMOTION) p.crouch = p.crouch > 0.5f ? 0.0f : 1.0f;
    float crouchTarget = p.crouch > 0.5f ? 1.0f : 0.0f;
    FIGHTO_UNUSED(crouchTarget);

    // Stamina
    bool sprinting = c.down[ACT_SPRINT] && wl > 0.3f && p.stamina > 1.0f && !aiming && !blocking && p.state == AS_LOCOMOTION && c.move.y > -0.2f;
    if (sprinting) {
        p.stamina -= 11.0f * dt;
        if (p.crouch > 0.5f) p.crouch = 0;
    } else if (p.state != AS_ATTACK && p.state != AS_DODGE && p.state != AS_BLOCK) {
        p.stamina = fminf(p.maxStamina, p.stamina + 20.0f * dt);
    }

    float speed = 4.4f;
    if (sprinting) speed = 7.4f;
    if (p.crouch > 0.5f) speed = 1.8f;
    if (aiming) speed = 2.4f;
    if (blocking) speed = 1.2f;
    if (c.down[ACT_HANDBRAKE] && !sprinting) speed = 1.7f; // walk (Alt)
    vec3 wishVel = wish * (speed * wl);

    switch (p.state) {
    case AS_LOCOMOTION: {
        // Facing
        if (aiming || (armed && c.down[ACT_ATTACK])) {
            p.yaw = angle_approach(p.yaw, camYaw, 18.0f * dt);
        } else if (g.lockTarget >= 0) {
            actor_face(p, g.actors[g.lockTarget].pos, 10.0f, dt);
        } else if (wl > 0.1f) {
            p.yaw = angle_approach(p.yaw, yaw_from_dir(wish), 11.0f * dt);
        }
        // Jump / vault
        if (c.pressed[ACT_JUMP] && p.grounded) {
            if (p.crouch > 0.5f) p.crouch = 0;
            vec3 f = dir_from_yaw(p.yaw);
            RayHit h;
            bool vault = false;
            if (world_raycast(p.pos + vec3(0, 0.6f, 0), f, 1.0f, &h, false)) {
                float top = world_ground_height(h.pos + f * 0.3f + vec3(0, 2.0f, 0), 0.1f, 0.0f);
                if (top - p.pos.y < 1.45f && top - p.pos.y > 0.4f && !world_raycast(p.pos + vec3(0, 1.7f, 0), f, 1.2f, 0, false)) vault = true;
            }
            p.vel.y = vault ? 6.4f : 5.3f;
            if (vault) p.vel = p.vel + f * 2.0f;
            p.grounded = false;
            actor_set_clip(p, CLIP_JUMP);
            audio_play_at(SND_WHOOSH, p.pos, 0.4f, 0.7f);
        }
        // Dodge
        if (c.pressed[ACT_DODGE] && p.stamina >= 18.0f && p.grounded) {
            p.stamina -= 18.0f;
            g_dodgeDir = wl > 0.1f ? normalize(wish) : -dir_from_yaw(p.yaw);
            g_dodgeTimer = 0.38f;
            p.iframes = 0.32f;
            p.state = AS_DODGE;
            p.stateTime = 0;
            actor_set_clip(p, CLIP_DODGE);
            audio_play_at(SND_WHOOSH, p.pos, 0.7f, 0.9f);
            break;
        }
        // Melee
        if (!armed) {
            if (c.pressed[ACT_ATTACK] && p.stamina > 5.0f) {
                p.combo = 0;
                start_light_attack(p);
                p.aiTimer2 = 4.0f;
            } else if (c.pressed[ACT_KICK] && p.stamina > 12.0f) {
                combat_start_attack(p, sprinting ? CLIP_HAYMAKER : CLIP_KICK, true);
                int t = melee_assist_target(3.0f);
                if (t >= 0) actor_face(p, g.actors[t].pos, 50.0f, 1.0f);
                p.aiTimer2 = 4.0f;
            } else if (blocking) {
                p.state = AS_BLOCK;
                p.stateTime = 0;
                p.blockStart = g.time;
                actor_set_clip(p, CLIP_BLOCK);
            }
        } else {
            // Firearms
            p.aimBlend = approach(p.aimBlend, (aiming || c.down[ACT_ATTACK] || p.fireCooldown > 0.05f) ? 1.0f : 0.0f, dt * 8.0f);
            const WeaponDef& wd = g_weapons[p.weapon];
            bool trigger = wd.automatic ? c.down[ACT_ATTACK] : c.pressed[ACT_ATTACK];
            if (p.reloadTimer > 0) {
                p.reloadTimer -= dt;
                if (p.reloadTimer <= 0) {
                    int need = wd.clipSize - p.clipAmmo;
                    int take = min_t(need, g.pd.reserve[p.weapon]);
                    p.clipAmmo += take;
                    g.pd.reserve[p.weapon] -= take;
                }
            } else if (trigger && p.fireCooldown <= 0 && p.aimBlend > 0.6f) {
                if (p.clipAmmo > 0) {
                    vec3 origin = actor_hand(p, true);
                    int hitActor;
                    vec3 target = camera_aim_point(wd.range, PLAYER, &hitActor);
                    vec3 dir = normalize(target - origin);
                    weapons_fire(PLAYER, origin, dir, aiming);
                    p.clipAmmo--;
                    g.pd.shotsFired++;
                    p.fireCooldown = wd.fireInterval;
                    g.cam.recoilPitch += wd.recoil * (aiming ? 0.6f : 1.0f);
                    camera_shake(wd.recoil * 0.8f);
                } else {
                    audio_play(SND_EMPTY, 0.6f);
                    p.fireCooldown = 0.3f;
                    weapons_try_reload(PLAYER);
                }
            }
            if (c.pressed[ACT_RELOAD]) weapons_try_reload(PLAYER);
            if (c.pressed[ACT_KICK] && p.stamina > 12.0f) {
                // Weapon-butt strike
                combat_start_attack(p, CLIP_ELBOW, false);
            }
        }
        break;
    }
    case AS_ATTACK: {
        wishVel = wishVel * 0.15f;
        const AnimClip* cl = anim_clip(p.attackClip);
        float len = cl ? cl->length : 0.5f;
        if (c.pressed[ACT_ATTACK] && p.clipTime > len * 0.3f) p.comboQueued = true;
        if (c.pressed[ACT_KICK] && p.clipTime > len * 0.3f) p.queuedHeavy = true;
        if (p.clipTime >= len * 0.72f && (p.comboQueued || p.queuedHeavy) && p.stamina > 5.0f) {
            bool heavy = p.queuedHeavy;
            p.comboQueued = false;
            p.queuedHeavy = false;
            if (heavy) combat_start_attack(p, CLIP_KICK, true);
            else start_light_attack(p);
            p.aiTimer2 = 4.0f;
        } else if (p.clipTime >= len) {
            p.state = AS_LOCOMOTION;
            p.combo = 0;
        }
        if (g.lockTarget >= 0) actor_face(p, g.actors[g.lockTarget].pos, 8.0f, dt);
        break;
    }
    case AS_BLOCK:
        if (!c.down[ACT_AIM]) {
            p.state = AS_LOCOMOTION;
            p.clip = -1;
        }
        if (g.lockTarget >= 0) actor_face(p, g.actors[g.lockTarget].pos, 10.0f, dt);
        else p.yaw = angle_approach(p.yaw, camYaw, 8.0f * dt);
        if (c.pressed[ACT_DODGE] && p.stamina >= 18.0f) {
            p.stamina -= 18.0f;
            g_dodgeDir = wl > 0.1f ? normalize(wish) : -dir_from_yaw(p.yaw);
            g_dodgeTimer = 0.38f;
            p.iframes = 0.32f;
            p.state = AS_DODGE;
            p.stateTime = 0;
            actor_set_clip(p, CLIP_DODGE);
        }
        break;
    case AS_DODGE:
        g_dodgeTimer -= dt;
        wishVel = g_dodgeDir * (g_dodgeTimer > 0.1f ? 7.8f : 2.0f);
        if (g_dodgeTimer <= 0) p.state = AS_LOCOMOTION;
        break;
    case AS_HIT:
    case AS_STAGGER:
        wishVel = p.knockVel;
        p.knockVel = p.knockVel * (1.0f - fminf(1.0f, dt * 6.0f));
        if (p.stateTime > (p.state == AS_STAGGER ? 0.75f : 0.32f)) p.state = AS_LOCOMOTION;
        break;
    case AS_KNOCKDOWN:
        wishVel = p.knockVel;
        p.knockVel = p.knockVel * (1.0f - fminf(1.0f, dt * 3.0f));
        if (p.stateTime > 1.3f && (c.anyKey || p.stateTime > 2.2f)) {
            p.state = AS_GETUP;
            p.stateTime = 0;
            actor_set_clip(p, CLIP_GETUP);
        }
        break;
    case AS_GETUP:
        wishVel = vec3(0, 0, 0);
        if (p.stateTime > 1.05f) { p.state = AS_LOCOMOTION; p.iframes = 0.5f; }
        break;
    case AS_TAKEDOWN:
        wishVel = vec3(0, 0, 0);
        if (g_takedownVictim >= 0) {
            Actor& v = g.actors[g_takedownVictim];
            v.pos = p.pos + dir_from_yaw(p.yaw) * 0.55f;
            v.yaw = p.yaw;
            if (p.stateTime > 1.1f && v.state == AS_TAKEN_DOWN) {
                bool alien = v.faction == FAC_ALIEN;
                if (alien || v.kind == AK_OPERATIVE) {
                    combat_damage(g_takedownVictim, PLAYER, 9999.0f, actor_head(v), dir_from_yaw(p.yaw), DMG_MELEE, B_NECK);
                } else {
                    v.knockedOut = 1.0f;
                    v.state = AS_DEAD;
                    v.deathTime = g.time;
                    v.poolSpawned = true;
                    actor_set_clip(v, CLIP_DEATH_FORWARD);
                    g.pd.knockouts++;
                    audio_play_at(SND_BODYFALL, v.pos);
                }
                g_takedownVictim = -1;
            }
        }
        if (p.stateTime > 1.4f) p.state = AS_LOCOMOTION;
        break;
    case AS_INTERACT:
        wishVel = vec3(0, 0, 0);
        if (g_enterVehicle >= 0) {
            g_enterTimer -= dt;
            if (g_enterTimer <= 0) {
                player_enter_vehicle(g_enterVehicle);
                g_enterVehicle = -1;
                return;
            }
        }
        if (p.stateTime > 0.8f && g_enterVehicle < 0) p.state = AS_LOCOMOTION;
        break;
    default:
        break;
    }

    if (!armed) p.aimBlend = approach(p.aimBlend, 0.0f, dt * 8.0f);
    float accel = p.grounded ? (p.state == AS_DODGE ? 20.0f : 11.0f) : 2.0f;
    actor_move(p, wishVel, dt, accel);
    p.speed = length(flat(p.vel));
    if (p.state == AS_LOCOMOTION && p.clip == CLIP_JUMP && p.grounded && p.clipTime > 0.2f) p.clip = -1;

    // Footsteps
    if (p.grounded && p.speed > 0.8f) {
        g_footstepPhase += dt * p.speed / 1.45f * 2.0f;
        if (g_footstepPhase >= 1.0f) {
            g_footstepPhase -= 1.0f;
            float vol = p.crouch > 0.5f ? 0.15f : (p.speed > 6 ? 0.6f : 0.4f);
            audio_play_at(g.rain > 0.3f && !world_is_indoors(p.pos) ? SND_FOOTSTEP_WET : SND_FOOTSTEP, p.pos, vol, g.rng.range(0.9f, 1.1f));
            if (p.speed > 6.5f) ai_alert_area(p.pos, 6.0f, PLAYER, 0);
        }
    }
}

bool player_interact(const Interactable& it) {
    Actor& p = g.actors[PLAYER];
    switch (it.kind) {
    case IK_VEHICLE: {
        Vehicle& v = g.vehicles[it.index];
        if (v.locked && v.driver < 0) {
            notify("This car is locked", vec4(1, 0.6f, 0.4f, 1));
            return true;
        }
        g_enterVehicle = it.index;
        g_enterTimer = 0.45f;
        p.state = AS_INTERACT;
        p.stateTime = 0;
        actor_face(p, v.pos, 100.0f, 1.0f);
        actor_set_clip(p, CLIP_INTERACT);
    } break;
    case IK_TAKEDOWN: {
        Actor& v = g.actors[it.index];
        g_takedownVictim = it.index;
        p.state = AS_TAKEDOWN;
        p.stateTime = 0;
        actor_face(p, v.pos, 100.0f, 1.0f);
        actor_set_clip(p, CLIP_TAKEDOWN);
        v.state = AS_TAKEN_DOWN;
        v.stateTime = 0;
        v.aiMode = AI_HOLD;
        actor_set_clip(v, CLIP_HIT_HEAD);
        audio_play_at(SND_GRUNT, v.pos, 0.8f, 0.8f);
    } break;
    default:
        return false;
    }
    return true;
}

void player_update(float dt) {
    Actor& p = g.actors[PLAYER];
    p.maxHealth = player_max_health();
    p.maxStamina = player_max_stamina();
    if (p.state == AS_DEAD) {
        p.speed = 0;
        if (!p.ragdoll) actor_move(p, vec3(0, 0, 0), dt, 3.0f);
        return;
    }
    // Health regeneration up to 50% after a short break from damage
    if (g.time - p.lastDamageTime > 6.0f && p.health < p.maxHealth * 0.5f) p.health = fminf(p.maxHealth * 0.5f, p.health + 5.0f * dt);
    if (p.state == AS_VEHICLE) {
        if (p.vehicle < 0 || !g.vehicles[p.vehicle].active) {
            p.state = AS_LOCOMOTION;
            p.vehicle = -1;
            return;
        }
        Vehicle& v = g.vehicles[p.vehicle];
        p.pos = vehicle_seat_pos(v, 0);
        p.yaw = v.yaw;
        p.speed = 0;
        p.vel = v.vel;
        if (g.ctl.pressed[ACT_INTERACT] && !mission_choice_active()) player_exit_vehicle();
        if (v.destroyed && v.burnTime > 0.5f) {
            player_exit_vehicle();
            if (p.state == AS_VEHICLE) {
                p.state = AS_LOCOMOTION;
                p.vehicle = -1;
                p.pos = v.pos + vec3(2.5f, 0, 0);
            }
        }
        return;
    }
    player_on_foot(p, dt);
    // Weapon switching
    if (p.state == AS_LOCOMOTION && p.reloadTimer <= 0) {
        int dir = (g.ctl.pressed[ACT_WEAPON_NEXT] ? 1 : 0) - (g.ctl.pressed[ACT_WEAPON_PREV] ? 1 : 0) + (g.ctl.wheel < 0 ? 1 : 0) - (g.ctl.wheel > 0 ? 1 : 0);
        if (dir != 0) {
            int w = p.weapon;
            for (int k = 0; k < W_CLAWS; k++) {
                w = (w + dir + W_CLAWS) % W_CLAWS;
                if (g.pd.hasWeapon[w]) break;
            }
            player_select_weapon(w);
        }
        if (g.ctl.weaponSlot > 0) {
            static const int slots[9] = {W_FISTS, W_FISTS, W_PISTOL, W_SHOTGUN, W_SMG, W_RIFLE, W_MAGNUM, W_ARC, W_LANCE};
            int s = clamp_t(g.ctl.weaponSlot, 1, 8);
            int w = slots[s];
            if (s == 8 && !g.pd.hasWeapon[W_LANCE]) w = W_SHARD;
            player_select_weapon(w);
        }
    }
    // Interaction prompt (chosen by game.cpp; executed here)
    if (g.ctl.pressed[ACT_INTERACT] && g.interactBest >= 0 && p.state == AS_LOCOMOTION && !mission_choice_active()) {
        interact_execute(g.interactables[g.interactBest]);
    }
}
