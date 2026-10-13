// FIGHTO - math implementation.
#include "vmath.h"

mat4 mat4_inverse(const mat4& mm) {
    const float* m = mm.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (fabsf(det) < 1e-12f) return mat4_identity();
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

bool ray_aabb(vec3 o, vec3 inv, const AABB& b, float maxT, float* tOut, vec3* nOut) {
    float tx1 = (b.mn.x - o.x) * inv.x, tx2 = (b.mx.x - o.x) * inv.x;
    float ty1 = (b.mn.y - o.y) * inv.y, ty2 = (b.mx.y - o.y) * inv.y;
    float tz1 = (b.mn.z - o.z) * inv.z, tz2 = (b.mx.z - o.z) * inv.z;
    float txn = fminf(tx1, tx2), txf = fmaxf(tx1, tx2);
    float tyn = fminf(ty1, ty2), tyf = fmaxf(ty1, ty2);
    float tzn = fminf(tz1, tz2), tzf = fmaxf(tz1, tz2);
    float tn = fmaxf(fmaxf(txn, tyn), tzn);
    float tf = fminf(fminf(txf, tyf), tzf);
    if (tf < 0 || tn > tf || tn > maxT) return false;
    if (tn < 0) {
        // Origin inside: report a hit at t=0 with an upward normal.
        *tOut = 0;
        if (nOut) *nOut = vec3(0, 1, 0);
        return true;
    }
    *tOut = tn;
    if (nOut) {
        if (tn == txn) *nOut = vec3(inv.x > 0 ? -1.0f : 1.0f, 0, 0);
        else if (tn == tyn) *nOut = vec3(0, inv.y > 0 ? -1.0f : 1.0f, 0);
        else *nOut = vec3(0, 0, inv.z > 0 ? -1.0f : 1.0f);
    }
    return true;
}

bool ray_sphere(vec3 o, vec3 d, vec3 c, float r, float maxT, float* tOut) {
    vec3 m = o - c;
    float b = dot(m, d);
    float cc = dot(m, m) - r * r;
    if (cc > 0 && b > 0) return false;
    float disc = b * b - cc;
    if (disc < 0) return false;
    float t = -b - sqrtf(disc);
    if (t < 0) t = 0;
    if (t > maxT) return false;
    *tOut = t;
    return true;
}

float dist_point_segment(vec3 p, vec3 a, vec3 b, vec3* closest) {
    vec3 ab = b - a;
    float l2 = dot(ab, ab);
    float t = l2 > 1e-8f ? saturate(dot(p - a, ab) / l2) : 0.0f;
    vec3 c = a + ab * t;
    if (closest) *closest = c;
    return length(p - c);
}

Frustum frustum_from_matrix(const mat4& vp) {
    Frustum f;
    const float* m = vp.m;
    // rows of the matrix
    vec4 r0(m[0], m[4], m[8], m[12]);
    vec4 r1(m[1], m[5], m[9], m[13]);
    vec4 r2(m[2], m[6], m[10], m[14]);
    vec4 r3(m[3], m[7], m[11], m[15]);
    f.planes[0] = vec4(r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w);
    f.planes[1] = vec4(r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w);
    f.planes[2] = vec4(r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w);
    f.planes[3] = vec4(r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w);
    f.planes[4] = vec4(r3.x + r2.x, r3.y + r2.y, r3.z + r2.z, r3.w + r2.w);
    f.planes[5] = vec4(r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w);
    for (int i = 0; i < 6; i++) {
        float l = sqrtf(f.planes[i].x * f.planes[i].x + f.planes[i].y * f.planes[i].y + f.planes[i].z * f.planes[i].z);
        f.planes[i] = f.planes[i] * (1.0f / l);
    }
    return f;
}

bool frustum_aabb(const Frustum& f, vec3 mn, vec3 mx) {
    for (int i = 0; i < 6; i++) {
        const vec4& p = f.planes[i];
        vec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
        if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
    }
    return true;
}

bool frustum_sphere(const Frustum& f, vec3 c, float r) {
    for (int i = 0; i < 6; i++) {
        const vec4& p = f.planes[i];
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    }
    return true;
}
