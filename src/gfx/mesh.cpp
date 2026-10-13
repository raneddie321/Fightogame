// FIGHTO - procedural mesh building and GPU mesh management.
#include "renderer.h"

static void pack_normal(i8* out, vec3 n) {
    n = normalize(n);
    out[0] = (i8)clampf(roundf(n.x * 127.0f), -127, 127);
    out[1] = (i8)clampf(roundf(n.y * 127.0f), -127, 127);
    out[2] = (i8)clampf(roundf(n.z * 127.0f), -127, 127);
    out[3] = 0;
}

void MeshBuilder::reset() {
    verts.clear();
    inds.clear();
    layer = 0; flags = 0; rough = 128; emis = 0;
    col[0] = col[1] = col[2] = col[3] = 255;
    bone = bone2 = 0;
    boneBlend = 0;
}

void MeshBuilder::free_mem() {
    verts.free_mem();
    inds.free_mem();
}

void MeshBuilder::set_mat(int l, int f, float roughness, float emissive) {
    layer = (u8)l;
    flags = (u8)f;
    rough = (u8)clampf(roughness * 128.0f, 0, 255);
    emis = (u8)clampf(emissive * 10.0f, 0, 255);
}

void MeshBuilder::set_color(vec3 c, float a) {
    col[0] = (u8)clampf(c.x * 255.0f, 0, 255);
    col[1] = (u8)clampf(c.y * 255.0f, 0, 255);
    col[2] = (u8)clampf(c.z * 255.0f, 0, 255);
    col[3] = (u8)clampf(a * 255.0f, 0, 255);
}

void MeshBuilder::set_tintable() { col[0] = col[1] = col[2] = 255; col[3] = 0; }

u32 MeshBuilder::vert(vec3 p, vec3 n, vec2 uv) {
    Vertex v;
    v.pos = p;
    pack_normal(v.nrm, n);
    v.uv = uv;
    memcpy(v.col, col, 4);
    v.mat[0] = layer; v.mat[1] = flags; v.mat[2] = rough; v.mat[3] = emis;
    v.bones[0] = bone; v.bones[1] = bone2; v.bones[2] = 0; v.bones[3] = 0;
    int w2 = (int)(boneBlend * 255.0f + 0.5f);
    v.weights[0] = (u8)(255 - w2); v.weights[1] = (u8)w2; v.weights[2] = 0; v.weights[3] = 0;
    verts.push(v);
    return (u32)(verts.count - 1);
}

void MeshBuilder::tri(u32 a, u32 b, u32 c) {
    inds.push(a); inds.push(b); inds.push(c);
}

void MeshBuilder::quad(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud) {
    vec3 n = normalize(cross(b - a, c - a));
    if (length_sq(n) < 1e-10f) n = normalize(cross(c - a, d - a));
    u32 i0 = vert(a, n, ua), i1 = vert(b, n, ub), i2 = vert(c, n, uc), i3 = vert(d, n, ud);
    tri(i0, i1, i2);
    tri(i0, i2, i3);
}

void MeshBuilder::quad_auto(vec3 a, vec3 b, vec3 c, vec3 d) {
    vec3 n = normalize(cross(b - a, c - a));
    // Project onto the dominant plane for metre UVs
    vec2 ua, ub, uc, ud;
    if (fabsf(n.y) > 0.7f) {
        ua = vec2(a.x, a.z); ub = vec2(b.x, b.z); uc = vec2(c.x, c.z); ud = vec2(d.x, d.z);
    } else {
        vec3 t = normalize(cross(vec3(0, 1, 0), n));
        ua = vec2(dot(a, t), a.y); ub = vec2(dot(b, t), b.y); uc = vec2(dot(c, t), c.y); ud = vec2(dot(d, t), d.y);
    }
    quad(a, b, c, d, ua, ub, uc, ud);
}

void MeshBuilder::box(vec3 mn, vec3 mx, bool top, bool bottom, bool sides) {
    float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
    if (sides) {
        // +Z face
        quad(vec3(x0, y0, z1), vec3(x1, y0, z1), vec3(x1, y1, z1), vec3(x0, y1, z1), vec2(x0, y0), vec2(x1, y0), vec2(x1, y1), vec2(x0, y1));
        // -Z face
        quad(vec3(x1, y0, z0), vec3(x0, y0, z0), vec3(x0, y1, z0), vec3(x1, y1, z0), vec2(-x1, y0), vec2(-x0, y0), vec2(-x0, y1), vec2(-x1, y1));
        // +X face
        quad(vec3(x1, y0, z1), vec3(x1, y0, z0), vec3(x1, y1, z0), vec3(x1, y1, z1), vec2(-z1, y0), vec2(-z0, y0), vec2(-z0, y1), vec2(-z1, y1));
        // -X face
        quad(vec3(x0, y0, z0), vec3(x0, y0, z1), vec3(x0, y1, z1), vec3(x0, y1, z0), vec2(z0, y0), vec2(z1, y0), vec2(z1, y1), vec2(z0, y1));
    }
    if (top) quad(vec3(x0, y1, z1), vec3(x1, y1, z1), vec3(x1, y1, z0), vec3(x0, y1, z0), vec2(x0, z1), vec2(x1, z1), vec2(x1, z0), vec2(x0, z0));
    if (bottom) quad(vec3(x0, y0, z0), vec3(x1, y0, z0), vec3(x1, y0, z1), vec3(x0, y0, z1), vec2(x0, z0), vec2(x1, z0), vec2(x1, z1), vec2(x0, z1));
}

void MeshBuilder::box_xf(const mat4& xf, vec3 s) {
    int start = verts.count;
    vec3 h = s * 0.5f;
    float x0 = -h.x, y0 = -h.y, z0 = -h.z, x1 = h.x, y1 = h.y, z1 = h.z;
    quad(vec3(x0, y0, z1), vec3(x1, y0, z1), vec3(x1, y1, z1), vec3(x0, y1, z1), vec2(x0, y0), vec2(x1, y0), vec2(x1, y1), vec2(x0, y1));
    quad(vec3(x1, y0, z0), vec3(x0, y0, z0), vec3(x0, y1, z0), vec3(x1, y1, z0), vec2(-x1, y0), vec2(-x0, y0), vec2(-x0, y1), vec2(-x1, y1));
    quad(vec3(x1, y0, z1), vec3(x1, y0, z0), vec3(x1, y1, z0), vec3(x1, y1, z1), vec2(-z1, y0), vec2(-z0, y0), vec2(-z0, y1), vec2(-z1, y1));
    quad(vec3(x0, y0, z0), vec3(x0, y0, z1), vec3(x0, y1, z1), vec3(x0, y1, z0), vec2(z0, y0), vec2(z1, y0), vec2(z1, y1), vec2(z0, y1));
    quad(vec3(x0, y1, z1), vec3(x1, y1, z1), vec3(x1, y1, z0), vec3(x0, y1, z0), vec2(x0, z1), vec2(x1, z1), vec2(x1, z0), vec2(x0, z0));
    quad(vec3(x0, y0, z0), vec3(x1, y0, z0), vec3(x1, y0, z1), vec3(x0, y0, z1), vec2(x0, z0), vec2(x1, z0), vec2(x1, z1), vec2(x0, z1));
    transform(start, xf);
}

void MeshBuilder::cylinder(vec3 base, float r, float h, int seg, bool caps, float uvScale) {
    cone_frustum(base, r, r, h, seg, caps);
    FIGHTO_UNUSED(uvScale);
}

void MeshBuilder::cone_frustum(vec3 base, float r0, float r1, float h, int seg, bool caps) {
    float circ = TAU_F * fmaxf(r0, r1);
    for (int i = 0; i < seg; i++) {
        float a0 = TAU_F * i / seg, a1 = TAU_F * (i + 1) / seg;
        vec3 d0(sinf(a0), 0, cosf(a0)), d1(sinf(a1), 0, cosf(a1));
        vec3 p0 = base + d0 * r0, p1 = base + d1 * r0;
        vec3 p2 = base + d1 * r1 + vec3(0, h, 0), p3 = base + d0 * r1 + vec3(0, h, 0);
        float slope = (r0 - r1) / fmaxf(h, 1e-4f);
        vec3 n0 = normalize(d0 + vec3(0, slope, 0)), n1 = normalize(d1 + vec3(0, slope, 0));
        float u0 = circ * i / seg, u1 = circ * (i + 1) / seg;
        u32 i0 = vert(p0, n0, vec2(u0, base.y));
        u32 i1 = vert(p1, n1, vec2(u1, base.y));
        u32 i2 = vert(p2, n1, vec2(u1, base.y + h));
        u32 i3 = vert(p3, n0, vec2(u0, base.y + h));
        tri(i0, i1, i2);
        tri(i0, i2, i3);
    }
    if (caps) {
        u32 ct = vert(base + vec3(0, h, 0), vec3(0, 1, 0), vec2(base.x, base.z));
        u32 cb = vert(base, vec3(0, -1, 0), vec2(base.x, base.z));
        for (int i = 0; i < seg; i++) {
            float a0 = TAU_F * i / seg, a1 = TAU_F * (i + 1) / seg;
            vec3 t0 = base + vec3(sinf(a0) * r1, h, cosf(a0) * r1), t1 = base + vec3(sinf(a1) * r1, h, cosf(a1) * r1);
            vec3 b0 = base + vec3(sinf(a0) * r0, 0, cosf(a0) * r0), b1 = base + vec3(sinf(a1) * r0, 0, cosf(a1) * r0);
            if (r1 > 0.0f) tri(ct, vert(t0, vec3(0, 1, 0), vec2(t0.x, t0.z)), vert(t1, vec3(0, 1, 0), vec2(t1.x, t1.z)));
            if (r0 > 0.0f) tri(cb, vert(b1, vec3(0, -1, 0), vec2(b1.x, b1.z)), vert(b0, vec3(0, -1, 0), vec2(b0.x, b0.z)));
        }
    }
}

void MeshBuilder::ellipsoid(vec3 c, vec3 r, int seg, int rings) {
    u32 base = (u32)verts.count;
    for (int j = 0; j <= rings; j++) {
        float v = (float)j / rings;
        float phi = v * PI_F;
        for (int i = 0; i <= seg; i++) {
            float u = (float)i / seg;
            float th = u * TAU_F;
            vec3 d(sinf(phi) * sinf(th), cosf(phi), sinf(phi) * cosf(th));
            vec3 p = c + vec3(d.x * r.x, d.y * r.y, d.z * r.z);
            vec3 n = normalize(vec3(d.x / r.x, d.y / r.y, d.z / r.z));
            vert(p, n, vec2(u * TAU_F * fmaxf(r.x, r.z), (1 - v) * PI_F * r.y));
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            u32 a = base + (u32)(j * (seg + 1) + i);
            u32 b = a + (u32)(seg + 1);
            tri(a, b, a + 1);
            tri(a + 1, b, b + 1);
        }
}

void MeshBuilder::tube(vec3 a, vec3 b, float ra, float rb, int seg, bool capA, bool capB) {
    vec3 axis = b - a;
    float len = length(axis);
    if (len < 1e-5f) return;
    vec3 dir = axis / len;
    vec3 ref = fabsf(dir.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 s = normalize(cross(dir, ref));
    vec3 t = cross(s, dir);
    u32 base = (u32)verts.count;
    float slope = (ra - rb) / len;
    for (int ring = 0; ring < 2; ring++) {
        vec3 c = ring == 0 ? a : b;
        float r = ring == 0 ? ra : rb;
        for (int i = 0; i <= seg; i++) {
            float th = TAU_F * i / seg;
            vec3 d = s * cosf(th) + t * sinf(th);
            vec3 n = normalize(d + dir * slope);
            vert(c + d * r, n, vec2((float)i / seg * TAU_F * r, ring * len));
        }
    }
    for (int i = 0; i < seg; i++) {
        u32 i0 = base + (u32)i, i1 = base + (u32)i + 1;
        u32 j0 = i0 + (u32)(seg + 1), j1 = i1 + (u32)(seg + 1);
        tri(i0, i1, j1);
        tri(i0, j1, j0);
    }
    if (capA) {
        u32 c = vert(a - dir * ra * 0.6f, -dir, vec2(0, 0));
        u32 rbase = (u32)verts.count;
        for (int i = 0; i <= seg; i++) {
            float th = TAU_F * i / seg;
            vec3 d = s * cosf(th) + t * sinf(th);
            vert(a + d * ra, normalize(d - dir), vec2(cosf(th) * ra, sinf(th) * ra));
        }
        for (int i = 0; i < seg; i++) tri(c, rbase + (u32)i + 1, rbase + (u32)i);
    }
    if (capB) {
        u32 c = vert(b + dir * rb * 0.6f, dir, vec2(0, 0));
        u32 rbase = (u32)verts.count;
        for (int i = 0; i <= seg; i++) {
            float th = TAU_F * i / seg;
            vec3 d = s * cosf(th) + t * sinf(th);
            vert(b + d * rb, normalize(d + dir), vec2(cosf(th) * rb, sinf(th) * rb));
        }
        for (int i = 0; i < seg; i++) tri(c, rbase + (u32)i, rbase + (u32)i + 1);
    }
}

void MeshBuilder::transform(int first, const mat4& m) {
    for (int i = first; i < verts.count; i++) {
        Vertex& v = verts[i];
        v.pos = transform_point(m, v.pos);
        vec3 n(v.nrm[0] / 127.0f, v.nrm[1] / 127.0f, v.nrm[2] / 127.0f);
        pack_normal(v.nrm, transform_dir(m, n));
    }
}

void MeshBuilder::append(const MeshBuilder& o, const mat4& m) {
    u32 base = (u32)verts.count;
    int first = verts.count;
    verts.reserve(verts.count + o.verts.count);
    for (int i = 0; i < o.verts.count; i++) verts.push(o.verts.data[i]);
    transform(first, m);
    inds.reserve(inds.count + o.inds.count);
    for (int i = 0; i < o.inds.count; i++) inds.push(o.inds.data[i] + base);
}

static void setup_vao(Mesh* m) {
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    const GLsizei st = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, st, (void*)offsetof(Vertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(Vertex, col));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_FALSE, st, (void*)offsetof(Vertex, mat));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, st, (void*)offsetof(Vertex, bones));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(Vertex, weights));
    glBindVertexArray(0);
}

static void compute_bounds(Mesh* m, const MeshBuilder& mb) {
    if (mb.verts.count == 0) { m->bmin = m->bmax = vec3(0, 0, 0); return; }
    vec3 mn = mb.verts.data[0].pos, mx = mn;
    for (int i = 1; i < mb.verts.count; i++) { mn = vmin(mn, mb.verts.data[i].pos); mx = vmax(mx, mb.verts.data[i].pos); }
    m->bmin = mn;
    m->bmax = mx;
}

Mesh mesh_create(const MeshBuilder& mb, bool dynamic) {
    Mesh m;
    memset(&m, 0, sizeof(m));
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ibo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * (size_t)mb.verts.count), mb.verts.data, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)mb.inds.count), mb.inds.data, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    m.indexCount = mb.inds.count;
    setup_vao(&m);
    compute_bounds(&m, mb);
    return m;
}

void mesh_update(Mesh* m, const MeshBuilder& mb) {
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * (size_t)mb.verts.count), mb.verts.data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)mb.inds.count), mb.inds.data, GL_DYNAMIC_DRAW);
    m->indexCount = mb.inds.count;
    glBindVertexArray(0);
    compute_bounds(m, mb);
}

void mesh_destroy(Mesh* m) {
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->ibo) glDeleteBuffers(1, &m->ibo);
    memset(m, 0, sizeof(*m));
}
