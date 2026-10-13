// FIGHTO - third-person camera: orbit, shoulder aim, lock-on, vehicle chase,
// collision avoidance, shake and cinematic overrides.
#include "game.h"

void camera_shake(float amount) { g.cam.shake = fminf(1.5f, g.cam.shake + amount); }

void camera_cinematic(vec3 pos, vec3 target, float fov) {
    g.cam.cinematic = true;
    g.cam.cinePos = pos;
    g.cam.cineTarget = target;
    g.cam.cineFov = fov;
}

void camera_release() { g.cam.cinematic = false; }

vec3 camera_forward() { return normalize(g.cam.target - g.cam.pos); }

static vec3 collide_camera(vec3 focus, vec3 desired) {
    vec3 d = desired - focus;
    float len = length(d);
    if (len < 0.01f) return desired;
    vec3 dir = d / len;
    RayHit h;
    float best = len;
    // Three rays approximate a sphere sweep
    vec3 side = normalize(cross(dir, vec3(0, 1, 0))) * 0.2f;
    vec3 offs[3] = {vec3(0, 0, 0), side, -side};
    for (int i = 0; i < 3; i++) {
        if (world_raycast(focus + offs[i], dir, len, &h, true)) best = fminf(best, h.t);
    }
    if (best < len) return focus + dir * fmaxf(best - 0.3f, 0.25f);
    return desired;
}

void camera_update(float dt) {
    CameraRig& c = g.cam;
    Actor& p = g.actors[PLAYER];
    float sens = 1.0f;
    c.yaw = wrap_angle(c.yaw - g.ctl.look.x * sens);
    c.pitch = clampf(c.pitch + g.ctl.look.y * sens, -1.1f, 1.25f);
    c.recoilPitch = fmaxf(0.0f, c.recoilPitch - dt * 6.0f * (c.recoilPitch + 0.05f));
    c.shake = fmaxf(0.0f, c.shake - dt * 1.8f);

    if (c.cinematic) {
        c.pos = lerp(c.pos, c.cinePos, damp_factor(6.0f, dt));
        c.target = lerp(c.target, c.cineTarget, damp_factor(6.0f, dt));
        c.fov = lerpf(c.fov, c.cineFov, damp_factor(4.0f, dt));
        return;
    }
    if (p.state == AS_VEHICLE && p.vehicle >= 0) {
        Vehicle& v = g.vehicles[p.vehicle];
        float spd = length(v.vel);
        bool looking = fabsf(g.ctl.look.x) > 0.0005f || fabsf(g.ctl.look.y) > 0.0005f;
        if (looking) c.lookTimer = 1.6f;
        c.lookTimer = fmaxf(0.0f, c.lookTimer - dt);
        if (c.lookTimer <= 0 && spd > 2.0f) {
            // Drift back behind the car (respect reversing)
            float behind = v.yaw;
            vec3 f = vehicle_forward(v);
            if (dot(v.vel, f) < -2.0f) behind = wrap_angle(v.yaw + PI_F);
            c.yaw = angle_approach(c.yaw, behind, dt * 2.5f * fabsf(wrap_angle(behind - c.yaw)) + dt * 0.3f);
            c.pitch = lerpf(c.pitch, 0.22f, damp_factor(2.0f, dt));
        }
        vec3 focus = v.pos + vec3(0, 1.7f, 0);
        c.smoothFocus = lerp(c.smoothFocus, focus, damp_factor(12.0f, dt));
        float dist = 6.8f + spd * 0.06f;
        vec3 off = vec3(-sinf(c.yaw) * cosf(c.pitch), sinf(c.pitch), -cosf(c.yaw) * cosf(c.pitch)) * dist;
        vec3 desired = c.smoothFocus + off + vec3(0, 0.6f, 0);
        c.pos = collide_camera(c.smoothFocus, desired);
        c.target = c.smoothFocus + vec3(sinf(c.yaw), -0.05f, cosf(c.yaw)) * 4.0f;
        c.fov = lerpf(c.fov, g.settings.fov + fminf(spd * 0.35f, 14.0f), damp_factor(3.0f, dt));
        c.dist = dist;
    } else {
        bool aiming = p.weapon != W_FISTS && g.ctl.down[ACT_AIM] && p.state == AS_LOCOMOTION;
        c.aimZoom = approach(c.aimZoom, aiming ? 1.0f : 0.0f, dt * 6.0f);
        // Lock-on: steer the camera towards the target
        if (g.lockTarget >= 0 && g.ctl.look.x == 0.0f) {
            vec3 d = g.actors[g.lockTarget].pos - p.pos;
            float ty = atan2f(d.x, d.z);
            c.yaw = angle_approach(c.yaw, ty, dt * 2.5f);
            c.pitch = lerpf(c.pitch, 0.18f, damp_factor(2.0f, dt));
        }
        float crouchDrop = p.crouch > 0.5f ? 0.45f : 0.0f;
        vec3 focus = p.pos + vec3(0, 1.55f * (p.height / 1.8f) - crouchDrop, 0);
        if (p.state == AS_DEAD) focus = (p.ragdoll ? actor_chest(p) : p.pos) + vec3(0, 0.6f, 0);
        c.smoothFocus = lerp(c.smoothFocus, focus, damp_factor(14.0f, dt));
        float sprint = (p.speed > 6.5f) ? 0.4f : 0.0f;
        float dist = lerpf(3.3f + sprint, 1.45f, c.aimZoom);
        if (p.state == AS_DEAD) dist = 4.5f;
        float shoulder = lerpf(0.45f, 0.62f, c.aimZoom);
        float pitch = c.pitch + c.recoilPitch;
        vec3 fwd(sinf(c.yaw) * cosf(pitch), -sinf(pitch), cosf(c.yaw) * cosf(pitch));
        vec3 right(-cosf(c.yaw), 0, sinf(c.yaw));
        vec3 pivot = c.smoothFocus + right * shoulder;
        pivot = collide_camera(c.smoothFocus, pivot);
        vec3 desired = pivot - fwd * dist;
        c.pos = collide_camera(pivot, desired);
        c.target = c.pos + fwd * 10.0f;
        c.dist = length(c.pos - c.smoothFocus);
        float fovTarget = g.settings.fov * lerpf(1.0f, 0.78f, c.aimZoom) + sprint * 10.0f;
        c.fov = lerpf(c.fov, fovTarget, damp_factor(6.0f, dt));
    }
    if (c.shake > 0.0f) {
        float t = g.time * 37.0f;
        float s = c.shake * c.shake * 0.12f;
        vec3 o(sinf(t * 1.3f) * s, sinf(t * 1.7f + 1.0f) * s, sinf(t * 1.1f + 2.0f) * s);
        c.pos += o;
        c.target += o * 0.5f;
    }
}

CameraState camera_state() {
    CameraState s;
    s.pos = g.cam.pos;
    s.target = g.cam.target;
    s.up = vec3(0, 1, 0);
    s.fov = g.cam.fov > 10 ? g.cam.fov : 62.0f;
    s.zNear = 0.12f;
    s.zFar = renderer_settings().drawDistance + 100.0f;
    return s;
}

vec3 camera_aim_point(float maxDist, int ignore, int* hitActor) {
    vec3 o = g.cam.pos;
    vec3 d = camera_forward();
    // Start the ray at the player's depth so objects between camera and player are ignored
    float skip = fmaxf(0.0f, dot(g.actors[PLAYER].pos + vec3(0, 1.4f, 0) - o, d) - 0.3f);
    o = o + d * skip;
    float best = maxDist;
    RayHit h;
    if (world_raycast(o, d, maxDist, &h, false)) best = h.t;
    float ta;
    int bone;
    int a = ray_actors(o, d, best, ignore, &ta, &bone);
    if (hitActor) *hitActor = a;
    if (a >= 0) best = ta;
    return o + d * best;
}
