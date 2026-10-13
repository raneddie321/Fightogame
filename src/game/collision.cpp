// FIGHTO - static world collision: spatial hash of axis aligned boxes,
// character cylinder movement with step-up, ground queries and ray casts.
#include "world.h"

static Array<u32> g_stamp;
static u32 g_stampValue = 1;

void world_add_box(vec3 mn, vec3 mx, int surface, int flags) {
    StaticBox b;
    b.box.mn = vmin(mn, mx);
    b.box.mx = vmax(mn, mx);
    b.surface = (u8)surface;
    b.flags = (u8)flags;
    g_world.boxes.push(b);
}

static inline int cell_x(float x) { return (int)floorf((x - g_world.gridOX) / g_world.gridCell); }
static inline int cell_z(float z) { return (int)floorf((z - g_world.gridOZ) / g_world.gridCell); }

void world_build_grid() {
    World& w = g_world;
    w.gridCell = 16.0f;
    w.gridOX = -720.0f;
    w.gridOZ = -720.0f;
    w.gridW = 90;
    w.gridH = 90;
    int cells = w.gridW * w.gridH;
    w.cellStart.resize(cells + 1);
    memset(w.cellStart.data, 0, sizeof(int) * (size_t)(cells + 1));
    for (int pass = 0; pass < 2; pass++) {
        Array<int> fill = {};
        if (pass == 1) {
            int total = 0;
            for (int c = 0; c < cells; c++) { int n = w.cellStart[c]; w.cellStart[c] = total; total += n; }
            w.cellStart[cells] = total;
            w.cellItems.resize(total);
            fill.resize(cells);
        }
        for (int i = 0; i < w.boxes.count; i++) {
            const AABB& b = w.boxes[i].box;
            int x0 = clamp_t(cell_x(b.mn.x), 0, w.gridW - 1), x1 = clamp_t(cell_x(b.mx.x), 0, w.gridW - 1);
            int z0 = clamp_t(cell_z(b.mn.z), 0, w.gridH - 1), z1 = clamp_t(cell_z(b.mx.z), 0, w.gridH - 1);
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++) {
                    int c = z * w.gridW + x;
                    if (pass == 0) w.cellStart[c]++;
                    else w.cellItems[w.cellStart[c] + fill[c]++] = i;
                }
        }
        fill.free_mem();
    }
    g_stamp.resize(w.boxes.count);
    memset(g_stamp.data, 0, sizeof(u32) * (size_t)w.boxes.count);
    log_info("Collision: %d boxes, %d cell refs", w.boxes.count, w.cellItems.count);
}

// Gathers candidate boxes overlapping an XZ rectangle (unique).
static int gather(float x0, float z0, float x1, float z1, int* out, int cap) {
    World& w = g_world;
    if (w.cellStart.count == 0) return 0;
    g_stampValue++;
    int n = 0;
    int cx0 = clamp_t(cell_x(x0), 0, w.gridW - 1), cx1 = clamp_t(cell_x(x1), 0, w.gridW - 1);
    int cz0 = clamp_t(cell_z(z0), 0, w.gridH - 1), cz1 = clamp_t(cell_z(z1), 0, w.gridH - 1);
    for (int z = cz0; z <= cz1; z++)
        for (int x = cx0; x <= cx1; x++) {
            int c = z * w.gridW + x;
            for (int k = w.cellStart[c]; k < w.cellStart[c + 1]; k++) {
                int bi = w.cellItems[k];
                if (g_stamp[bi] == g_stampValue) continue;
                g_stamp[bi] = g_stampValue;
                if (n < cap) out[n++] = bi;
            }
        }
    return n;
}

static float surface_ground(vec3 p) {
    // The surface plane exists everywhere above the underground areas.
    return p.y > -20.0f ? 0.0f : -1000.0f;
}

float world_ground_height(vec3 p, float radius, float stepUp) {
    int ids[256];
    int n = gather(p.x - radius, p.z - radius, p.x + radius, p.z + radius, ids, 256);
    float g = surface_ground(p);
    for (int i = 0; i < n; i++) {
        const AABB& b = g_world.boxes[ids[i]].box;
        if (b.mx.y > p.y + stepUp) continue;
        if (b.mx.y <= g) continue;
        // circle vs rect
        float cx = clampf(p.x, b.mn.x, b.mx.x), cz = clampf(p.z, b.mn.z, b.mx.z);
        float dx = p.x - cx, dz = p.z - cz;
        if (dx * dx + dz * dz <= radius * radius) g = b.mx.y;
    }
    return g;
}

float world_particle_ground(vec3 p) { return world_ground_height(p, 0.05f, 0.05f); }

bool world_overlap_cylinder(vec3 p, float radius, float height) {
    int ids[256];
    int n = gather(p.x - radius, p.z - radius, p.x + radius, p.z + radius, ids, 256);
    for (int i = 0; i < n; i++) {
        const AABB& b = g_world.boxes[ids[i]].box;
        if (b.mx.y <= p.y + 0.05f || b.mn.y >= p.y + height) continue;
        float cx = clampf(p.x, b.mn.x, b.mx.x), cz = clampf(p.z, b.mn.z, b.mx.z);
        float dx = p.x - cx, dz = p.z - cz;
        if (dx * dx + dz * dz < radius * radius) return true;
    }
    return false;
}

bool world_move_cylinder(vec3* pos, vec3 delta, float radius, float height, float stepUp, float* groundY) {
    vec3 p = *pos;
    // Horizontal sub-steps to avoid tunnelling
    float hl = sqrtf(delta.x * delta.x + delta.z * delta.z);
    int steps = 1 + (int)(hl / (radius * 0.8f));
    if (steps > 8) steps = 8;
    vec3 stepD(delta.x / steps, 0, delta.z / steps);
    int ids[256];
    for (int s = 0; s < steps; s++) {
        p.x += stepD.x;
        p.z += stepD.z;
        for (int iter = 0; iter < 3; iter++) {
            int n = gather(p.x - radius, p.z - radius, p.x + radius, p.z + radius, ids, 256);
            bool moved = false;
            for (int i = 0; i < n; i++) {
                const AABB& b = g_world.boxes[ids[i]].box;
                if (b.mx.y <= p.y + stepUp || b.mn.y >= p.y + height) continue;
                float cx = clampf(p.x, b.mn.x, b.mx.x), cz = clampf(p.z, b.mn.z, b.mx.z);
                float dx = p.x - cx, dz = p.z - cz;
                float d2 = dx * dx + dz * dz;
                if (d2 >= radius * radius) continue;
                if (d2 > 1e-8f) {
                    float d = sqrtf(d2);
                    float push = radius - d + 0.001f;
                    p.x += dx / d * push;
                    p.z += dz / d * push;
                } else {
                    // Centre inside the box: push out along the shallowest axis
                    float l = p.x - b.mn.x + radius, rr = b.mx.x - p.x + radius;
                    float bk = p.z - b.mn.z + radius, f = b.mx.z - p.z + radius;
                    float m = fminf(fminf(l, rr), fminf(bk, f));
                    if (m == l) p.x = b.mn.x - radius;
                    else if (m == rr) p.x = b.mx.x + radius;
                    else if (m == bk) p.z = b.mn.z - radius;
                    else p.z = b.mx.z + radius;
                }
                moved = true;
            }
            if (!moved) break;
        }
    }
    // Vertical
    float ny = p.y + delta.y;
    // Ceiling
    if (delta.y > 0) {
        int n = gather(p.x - radius * 0.7f, p.z - radius * 0.7f, p.x + radius * 0.7f, p.z + radius * 0.7f, ids, 256);
        for (int i = 0; i < n; i++) {
            const AABB& b = g_world.boxes[ids[i]].box;
            if (p.x < b.mn.x - radius * 0.5f || p.x > b.mx.x + radius * 0.5f || p.z < b.mn.z - radius * 0.5f || p.z > b.mx.z + radius * 0.5f) continue;
            if (p.y + height <= b.mn.y + 0.01f && ny + height > b.mn.y) ny = b.mn.y - height;
        }
    }
    float g = world_ground_height(vec3(p.x, fmaxf(p.y, ny), p.z), radius * 0.6f, stepUp);
    bool grounded = false;
    if (ny <= g + 0.001f) {
        ny = g;
        grounded = true;
    }
    p.y = ny;
    if (groundY) *groundY = g;
    *pos = p;
    return grounded;
}

bool world_raycast(vec3 o, vec3 d, float maxT, RayHit* hit, bool cameraOnly) {
    World& w = g_world;
    float best = maxT;
    int bestBox = -1;
    vec3 bestN(0, 1, 0);
    vec3 inv(fabsf(d.x) > 1e-8f ? 1.0f / d.x : 1e8f, fabsf(d.y) > 1e-8f ? 1.0f / d.y : 1e8f, fabsf(d.z) > 1e-8f ? 1.0f / d.z : 1e8f);
    // Ground plane
    if (d.y < -1e-5f && o.y > -20.0f) {
        float t = -o.y / d.y;
        if (t >= 0 && t < best) { best = t; bestN = vec3(0, 1, 0); bestBox = -2; }
    }
    // DDA through the grid in XZ
    float cell = w.gridCell;
    float gx = (o.x - w.gridOX) / cell, gz = (o.z - w.gridOZ) / cell;
    int cx = (int)floorf(gx), cz = (int)floorf(gz);
    int stepX = d.x > 0 ? 1 : -1, stepZ = d.z > 0 ? 1 : -1;
    float tDeltaX = fabsf(cell * inv.x), tDeltaZ = fabsf(cell * inv.z);
    float nextX = d.x > 0 ? (cx + 1 - gx) * cell : (gx - cx) * cell;
    float nextZ = d.z > 0 ? (cz + 1 - gz) * cell : (gz - cz) * cell;
    float tMaxX = fabsf(d.x) > 1e-8f ? nextX / fabsf(d.x) : 1e9f;
    float tMaxZ = fabsf(d.z) > 1e-8f ? nextZ / fabsf(d.z) : 1e9f;
    g_stampValue++;
    float tCell = 0;
    for (int guard = 0; guard < 512; guard++) {
        if (cx >= 0 && cz >= 0 && cx < w.gridW && cz < w.gridH) {
            int c = cz * w.gridW + cx;
            for (int k = w.cellStart[c]; k < w.cellStart[c + 1]; k++) {
                int bi = w.cellItems[k];
                if (g_stamp[bi] == g_stampValue) continue;
                g_stamp[bi] = g_stampValue;
                const StaticBox& sb = w.boxes[bi];
                if (cameraOnly && !(sb.flags & 1)) continue;
                float t;
                vec3 n;
                if (ray_aabb(o, inv, sb.box, best, &t, &n) && t < best) {
                    best = t;
                    bestN = n;
                    bestBox = bi;
                }
            }
        }
        if (tCell > best) break;
        if (tMaxX < tMaxZ) { tCell = tMaxX; tMaxX += tDeltaX; cx += stepX; }
        else { tCell = tMaxZ; tMaxZ += tDeltaZ; cz += stepZ; }
        if (tCell > best || tCell > maxT) break;
    }
    if (bestBox == -1) return false;
    if (hit) {
        hit->t = best;
        hit->pos = o + d * best;
        hit->normal = bestN;
        hit->box = bestBox;
        hit->surface = bestBox >= 0 ? w.boxes[bestBox].surface : (u8)SURF_CONCRETE;
    }
    return true;
}

bool world_is_indoors(vec3 p, float* ambient) {
    for (int i = 0; i < g_world.indoors.count; i++) {
        if (aabb_contains(g_world.indoors[i].box, p)) {
            if (ambient) *ambient = g_world.indoors[i].ambient;
            return true;
        }
    }
    return false;
}

int world_nearest_walk_node(vec3 p) {
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < g_world.walkNodes.count; i++) {
        float d = dist_xz_sq(g_world.walkNodes[i].pos, p) + fabsf(g_world.walkNodes[i].pos.y - p.y) * 50.0f;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

bool world_is_road(vec3 p) {
    if (fabsf(p.x) > CITY_HALF + ROAD_W || fabsf(p.z) > CITY_HALF + ROAD_W || p.y < -10) return false;
    for (int k = 0; k <= CITY_BLOCKS; k++) {
        if (fabsf(p.x - road_x(k)) < ROAD_W * 0.5f) return true;
        if (fabsf(p.z - road_x(k)) < ROAD_W * 0.5f) return true;
    }
    return false;
}

vec3 world_nearest_road_point(vec3 p) {
    float bestD = 1e9f;
    vec3 best = p;
    for (int k = 0; k <= CITY_BLOCKS; k++) {
        float rx = road_x(k);
        float d = fabsf(p.x - rx);
        if (d < bestD) { bestD = d; best = vec3(rx, 0, clampf(p.z, -CITY_HALF, CITY_HALF)); }
        d = fabsf(p.z - rx);
        if (d < bestD) { bestD = d; best = vec3(clampf(p.x, -CITY_HALF, CITY_HALF), 0, rx); }
    }
    return best;
}
