// FIGHTO - vector / matrix / quaternion math (column-major, right-handed, Y up).
#pragma once
#include "base.h"

#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717958f
#define DEG2RAD (PI_F / 180.0f)

struct vec2 {
    float x, y;
    vec2() = default;
    constexpr vec2(float a, float b) : x(a), y(b) {}
};
struct vec3 {
    float x, y, z;
    vec3() = default;
    constexpr vec3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct vec4 {
    float x, y, z, w;
    vec4() = default;
    constexpr vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    constexpr vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};
struct quat {
    float x, y, z, w;
    quat() = default;
    constexpr quat(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
struct mat4 {
    float m[16];
};

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float signf(float v) { return v < 0 ? -1.0f : 1.0f; }
inline float smoothstepf(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline float wrap_angle(float a) {
    while (a > PI_F) a -= TAU_F;
    while (a < -PI_F) a += TAU_F;
    return a;
}
inline float approach(float cur, float target, float delta) {
    if (cur < target) return cur + delta > target ? target : cur + delta;
    return cur - delta < target ? target : cur - delta;
}
inline float angle_approach(float cur, float target, float delta) {
    float d = wrap_angle(target - cur);
    if (fabsf(d) <= delta) return target;
    return wrap_angle(cur + (d > 0 ? delta : -delta));
}
// Frame-rate independent exponential smoothing factor
inline float damp_factor(float rate, float dt) { return 1.0f - expf(-rate * dt); }

// vec2
inline vec2 operator+(vec2 a, vec2 b) { return vec2(a.x + b.x, a.y + b.y); }
inline vec2 operator-(vec2 a, vec2 b) { return vec2(a.x - b.x, a.y - b.y); }
inline vec2 operator*(vec2 a, float s) { return vec2(a.x * s, a.y * s); }
inline vec2 operator*(vec2 a, vec2 b) { return vec2(a.x * b.x, a.y * b.y); }
inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline vec2 normalize(vec2 a) { float l = length(a); return l > 1e-8f ? a * (1.0f / l) : vec2(0, 0); }

// vec3
inline vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 operator-(vec3 a, vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec3 operator*(vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator*(float s, vec3 a) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator*(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 operator/(vec3 a, float s) { return a * (1.0f / s); }
inline vec3& operator+=(vec3& a, vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline vec3& operator*=(vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float length(vec3 a) { return sqrtf(dot(a, a)); }
inline float length_sq(vec3 a) { return dot(a, a); }
inline vec3 normalize(vec3 a) { float l = length(a); return l > 1e-8f ? a * (1.0f / l) : vec3(0, 0, 0); }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline float dist_xz(vec3 a, vec3 b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }
inline float dist_xz_sq(vec3 a, vec3 b) { float dx = a.x - b.x, dz = a.z - b.z; return dx * dx + dz * dz; }
inline vec3 flat(vec3 a) { return vec3(a.x, 0, a.z); }
inline vec3 vmin(vec3 a, vec3 b) { return vec3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)); }
inline vec3 vmax(vec3 a, vec3 b) { return vec3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)); }
inline vec3 dir_from_yaw(float yaw) { return vec3(sinf(yaw), 0, cosf(yaw)); }
inline float yaw_from_dir(vec3 d) { return atan2f(d.x, d.z); }

// vec4
inline vec4 operator*(vec4 a, float s) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline vec4 operator+(vec4 a, vec4 b) { return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline vec4 lerp(vec4 a, vec4 b, float t) { return vec4(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)); }
inline vec3 xyz(vec4 v) { return vec3(v.x, v.y, v.z); }

// quaternion
inline quat quat_identity() { return quat(0, 0, 0, 1); }
inline quat quat_axis_angle(vec3 axis, float a) {
    float s = sinf(a * 0.5f);
    axis = normalize(axis);
    return quat(axis.x * s, axis.y * s, axis.z * s, cosf(a * 0.5f));
}
inline quat operator*(quat a, quat b) {
    return quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
inline quat quat_normalize(quat q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return quat_identity();
    float i = 1.0f / l;
    return quat(q.x * i, q.y * i, q.z * i, q.w * i);
}
inline quat quat_conj(quat q) { return quat(-q.x, -q.y, -q.z, q.w); }
inline vec3 quat_rotate(quat q, vec3 v) {
    vec3 u(q.x, q.y, q.z);
    vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// Euler XYZ in radians (pitch around X, yaw around Y, roll around Z) applied as Y * X * Z
inline quat quat_euler(float pitch, float yaw, float roll) {
    return quat_axis_angle(vec3(0, 1, 0), yaw) * quat_axis_angle(vec3(1, 0, 0), pitch) * quat_axis_angle(vec3(0, 0, 1), roll);
}
inline quat quat_slerp(quat a, quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = quat(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995f) {
        return quat_normalize(quat(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)));
    }
    float th = acosf(d);
    float s = sinf(th);
    float wa = sinf((1 - t) * th) / s, wb = sinf(t * th) / s;
    return quat(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
}
// Rotation that turns vector a into vector b (both normalised)
inline quat quat_from_to(vec3 a, vec3 b) {
    float d = dot(a, b);
    if (d > 0.99999f) return quat_identity();
    if (d < -0.99999f) {
        vec3 axis = cross(vec3(1, 0, 0), a);
        if (length_sq(axis) < 1e-6f) axis = cross(vec3(0, 1, 0), a);
        return quat_axis_angle(axis, PI_F);
    }
    vec3 c = cross(a, b);
    return quat_normalize(quat(c.x, c.y, c.z, 1.0f + d));
}

// mat4 (column major: m[col * 4 + row])
inline mat4 mat4_identity() {
    mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline vec4 operator*(const mat4& a, vec4 v) {
    return vec4(a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
                a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
                a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
                a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w);
}
inline vec3 transform_point(const mat4& a, vec3 p) { vec4 r = a * vec4(p, 1.0f); return vec3(r.x, r.y, r.z); }
inline vec3 transform_dir(const mat4& a, vec3 p) { vec4 r = a * vec4(p, 0.0f); return vec3(r.x, r.y, r.z); }
inline mat4 mat4_translate(vec3 t) { mat4 r = mat4_identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline mat4 mat4_scale(vec3 s) { mat4 r = mat4_identity(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline mat4 mat4_from_quat(quat q) {
    mat4 r = mat4_identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz); r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz); r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy); r.m[9] = 2 * (yz - wx); r.m[10] = 1 - 2 * (xx + yy);
    return r;
}
inline mat4 mat4_trs(vec3 t, quat q, vec3 s) {
    mat4 r = mat4_from_quat(q);
    r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
    r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
    r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
inline mat4 mat4_rotate_y(float a) { return mat4_from_quat(quat_axis_angle(vec3(0, 1, 0), a)); }
inline mat4 mat4_perspective(float fovy, float aspect, float zn, float zf) {
    mat4 r;
    memset(&r, 0, sizeof(r));
    float f = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
inline mat4 mat4_ortho(float l, float r, float b, float t, float n, float f) {
    mat4 m = mat4_identity();
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    return m;
}
inline mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r = mat4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}
mat4 mat4_inverse(const mat4& m);

// Axis aligned box
struct AABB {
    vec3 mn, mx;
};
inline bool aabb_overlap(const AABB& a, const AABB& b) {
    return a.mn.x <= b.mx.x && a.mx.x >= b.mn.x && a.mn.y <= b.mx.y && a.mx.y >= b.mn.y && a.mn.z <= b.mx.z && a.mx.z >= b.mn.z;
}
inline bool aabb_contains(const AABB& a, vec3 p) {
    return p.x >= a.mn.x && p.x <= a.mx.x && p.y >= a.mn.y && p.y <= a.mx.y && p.z >= a.mn.z && p.z <= a.mx.z;
}
// Ray vs AABB slab test; returns entry distance in tOut
bool ray_aabb(vec3 o, vec3 invDir, const AABB& b, float maxT, float* tOut, vec3* normalOut);
// Ray vs sphere
bool ray_sphere(vec3 o, vec3 d, vec3 c, float r, float maxT, float* tOut);
// Distance from point to segment
float dist_point_segment(vec3 p, vec3 a, vec3 b, vec3* closest);

// Frustum culling
struct Frustum {
    vec4 planes[6];
};
Frustum frustum_from_matrix(const mat4& vp);
bool frustum_aabb(const Frustum& f, vec3 mn, vec3 mx);
bool frustum_sphere(const Frustum& f, vec3 c, float r);
