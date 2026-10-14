// FIGHTO - glTF 2.0 / Wavefront OBJ model import (CPU side) and GPU upload.
#include "model_import.h"
#include "../platform/platform.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ASSERT(x) ((void)0)
#if defined(__aarch64__)
#define STBI_NEON
#endif
#include "../../third_party/stb/stb_image.h"

#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

#define MODEL_MAX_ELEMENTS (1 << 26)
#define MODEL_MAX_ZERO_FILL (1 << 22)

// ---------------------------------------------------------------------------
// Paths: everything is asset-relative with forward slashes
// ---------------------------------------------------------------------------
// Folds "." and ".." segments in place; false if the path would leave the asset root.
static bool normalize_path(char* p) {
    int segStart[128];
    int depth = 0, w = 0, r = 0;
    while (p[r]) {
        while (p[r] == '/') r++;
        if (!p[r]) break;
        int s = r;
        while (p[r] && p[r] != '/') r++;
        int len = r - s;
        if (len == 1 && p[s] == '.') continue;
        if (len == 2 && p[s] == '.' && p[s + 1] == '.') {
            if (depth == 0) return false;
            w = segStart[--depth];
            continue;
        }
        if (depth >= ARRAY_COUNT(segStart)) return false;
        segStart[depth++] = w;
        if (w > 0) p[w++] = '/';
        memmove(p + w, p + s, (size_t)len);
        w += len;
    }
    p[w] = 0;
    return w > 0;
}

static void path_dir(const char* path, char* out, int cap) {
    str_copy(out, cap, path);
    int n = (int)strlen(out);
    while (n > 0 && out[n - 1] != '/') n--;
    out[n] = 0;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Resolves a reference from a model file against the model's directory. Absolute paths
// (common in OBJ files exported on an artist's machine) keep only their file name.
static bool resolve_path(const char* base, const char* ref, bool percentDecode, char* out, int cap) {
    char rel[512];
    int n = 0;
    for (int i = 0; ref[i] && n < (int)sizeof(rel) - 1; i++) {
        char c = ref[i];
        if (c == '\\') c = '/';
        if (percentDecode && c == '%' && hex_digit(ref[i + 1]) >= 0 && hex_digit(ref[i + 2]) >= 0) {
            c = (char)(hex_digit(ref[i + 1]) * 16 + hex_digit(ref[i + 2]));
            i += 2;
        }
        rel[n++] = c;
    }
    rel[n] = 0;
    const char* r = rel;
    bool absolute = rel[0] == '/' || (rel[0] && rel[1] == ':');
    if (absolute) {
        const char* slash = strrchr(rel, '/');
        r = slash ? slash + 1 : rel + 2;
    }
    char dir[512];
    path_dir(base, dir, sizeof(dir));
    fmt_str(out, cap, "%s%s", dir, r);
    return normalize_path(out);
}

// Reads a file referenced by a model; falls back to the bare file name next to the model.
static bool read_model_file(const char* base, const char* ref, bool percentDecode, Array<u8>* data) {
    char full[512];
    if (resolve_path(base, ref, percentDecode, full, sizeof(full)) && plat_read_asset(full, data)) return true;
    const char* name = ref;
    for (const char* p = ref; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    if (name != ref && resolve_path(base, name, percentDecode, full, sizeof(full)) && plat_read_asset(full, data)) return true;
    return false;
}

static bool path_has_ext(const char* path, const char* ext) {
    int n = (int)strlen(path), e = (int)strlen(ext);
    if (n < e) return false;
    for (int i = 0; i < e; i++) {
        char c = path[n - e + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != ext[i]) return false;
    }
    return true;
}

static bool base64_decode(const char* s, Array<u8>* out) {
    out->reserve(out->count + (int)(strlen(s) / 4 * 3) + 3);
    u32 acc = 0;
    int bits = 0;
    for (; *s; s++) {
        char c = *s;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        else return false;
        acc = ((acc << 6) | (u32)v) & 0xFFFFFFu;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push((u8)(acc >> bits));
        }
    }
    return true;
}

// "data:<mime>;base64,<payload>"
static bool decode_data_uri(const char* uri, Array<u8>* out) {
    const char* comma = strchr(uri, ',');
    if (!comma || comma - uri < 7 || memcmp(comma - 7, ";base64", 7) != 0) return false;
    return base64_decode(comma + 1, out);
}

// ---------------------------------------------------------------------------
// JSON (DOM of index-linked nodes; strings decoded into one arena)
// ---------------------------------------------------------------------------
enum JsonType { JT_NULL, JT_FALSE, JT_TRUE, JT_NUMBER, JT_STRING, JT_ARRAY, JT_OBJECT };

struct JsonNode {
    u8 type;
    int key;      // member name (offset into Json::text), -1 outside objects
    int str;      // string value (offset into Json::text)
    int first, last, next;
    int count;
    double num;
};

struct Json {
    Array<JsonNode> nodes;
    char* text;
    int textLen;
    const char* p;
    const char* end;
};

static void js_free(Json* j) {
    j->nodes.free_mem();
    free(j->text);
    j->text = 0;
}

static void js_ws(Json& j) {
    while (j.p < j.end && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r')) j.p++;
}

static int js_new(Json& j, int type) {
    JsonNode& n = j.nodes.push_zero();
    n.type = (u8)type;
    n.key = n.str = -1;
    n.first = n.last = n.next = -1;
    return j.nodes.count - 1;
}

static bool js_hex4(Json& j, u32* out) {
    if (j.end - j.p < 4) return false;
    u32 v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(j.p[i]);
        if (d < 0) return false;
        v = v * 16 + (u32)d;
    }
    j.p += 4;
    *out = v;
    return true;
}

static void js_put(Json& j, u32 cp) {
    char* o = j.text + j.textLen;
    if (cp < 0x80) { o[0] = (char)cp; j.textLen += 1; }
    else if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); j.textLen += 2; }
    else if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); j.textLen += 3; }
    else { o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63)); j.textLen += 4; }
}

// Decoded text never exceeds the raw string including its quotes, so the arena (input
// size + 1) cannot overflow.
static int js_string(Json& j) {
    j.p++;
    int start = j.textLen;
    while (j.p < j.end) {
        u8 c = (u8)*j.p++;
        if (c == '"') { j.text[j.textLen++] = 0; return start; }
        if (c < 0x20) return -1;
        if (c != '\\') { j.text[j.textLen++] = (char)c; continue; }
        if (j.p >= j.end) return -1;
        char e = *j.p++;
        u32 cp = 0;
        switch (e) {
        case '"': case '\\': case '/': cp = (u32)e; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u':
            if (!js_hex4(j, &cp)) return -1;
            if (cp >= 0xD800 && cp < 0xDC00) {
                u32 lo;
                if (j.end - j.p < 2 || j.p[0] != '\\' || j.p[1] != 'u') return -1;
                j.p += 2;
                if (!js_hex4(j, &lo) || lo < 0xDC00 || lo > 0xDFFF) return -1;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                return -1;
            }
            if (cp == 0) cp = ' ';
            break;
        default: return -1;
        }
        js_put(j, cp);
    }
    return -1;
}

static bool js_digit(const char* p, const char* end) { return p < end && *p >= '0' && *p <= '9'; }

static bool js_number(Json& j, double* out) {
    const char* p = j.p;
    bool neg = false;
    if (p < j.end && *p == '-') { neg = true; p++; }
    if (!js_digit(p, j.end)) return false;
    u64 mant = 0;
    int digits = 0, exp10 = 0;
    while (js_digit(p, j.end)) {
        if (digits < 19) { mant = mant * 10 + (u64)(*p - '0'); if (mant) digits++; }
        else exp10++;
        p++;
    }
    if (p < j.end && *p == '.') {
        p++;
        if (!js_digit(p, j.end)) return false;
        while (js_digit(p, j.end)) {
            if (digits < 19) { mant = mant * 10 + (u64)(*p - '0'); exp10--; if (mant) digits++; }
            p++;
        }
    }
    if (p < j.end && (*p == 'e' || *p == 'E')) {
        p++;
        bool eneg = false;
        if (p < j.end && (*p == '+' || *p == '-')) { eneg = *p == '-'; p++; }
        if (!js_digit(p, j.end)) return false;
        int e = 0;
        while (js_digit(p, j.end)) { if (e < 100000) e = e * 10 + (*p - '0'); p++; }
        exp10 += eneg ? -e : e;
    }
    double v = (double)mant;
    if (mant && exp10) {
        int ae = exp10 < 0 ? -exp10 : exp10;
        if (ae > 400) {
            v = exp10 < 0 ? 0.0 : 1e300;
        } else {
            double scale = 1.0;
            for (int i = 0; i < ae; i++) scale *= 10.0;
            v = exp10 < 0 ? v / scale : v * scale;
        }
    }
    *out = neg ? -v : v;
    j.p = p;
    return true;
}

static int js_value(Json& j, int depth) {
    js_ws(j);
    if (j.p >= j.end || depth > 64) return -1;
    char c = *j.p;
    if (c == '{' || c == '[') {
        bool obj = c == '{';
        char close = obj ? '}' : ']';
        int node = js_new(j, obj ? JT_OBJECT : JT_ARRAY);
        j.p++;
        js_ws(j);
        if (j.p < j.end && *j.p == close) { j.p++; return node; }
        for (;;) {
            int key = -1;
            if (obj) {
                js_ws(j);
                if (j.p >= j.end || *j.p != '"') return -1;
                key = js_string(j);
                if (key < 0) return -1;
                js_ws(j);
                if (j.p >= j.end || *j.p != ':') return -1;
                j.p++;
            }
            int child = js_value(j, depth + 1);
            if (child < 0) return -1;
            j.nodes[child].key = key;
            JsonNode& parent = j.nodes[node];
            if (parent.last >= 0) j.nodes[parent.last].next = child;
            else parent.first = child;
            parent.last = child;
            parent.count++;
            js_ws(j);
            if (j.p >= j.end) return -1;
            if (*j.p == ',') { j.p++; continue; }
            if (*j.p == close) { j.p++; return node; }
            return -1;
        }
    }
    if (c == '"') {
        int node = js_new(j, JT_STRING);
        int s = js_string(j);
        if (s < 0) return -1;
        j.nodes[node].str = s;
        return node;
    }
    static const char* lits[3] = {"null", "false", "true"};
    for (int i = 0; i < 3; i++) {
        int n = (int)strlen(lits[i]);
        if (j.end - j.p >= n && memcmp(j.p, lits[i], (size_t)n) == 0) {
            j.p += n;
            return js_new(j, JT_NULL + i);
        }
    }
    double v;
    if (!js_number(j, &v)) return -1;
    int node = js_new(j, JT_NUMBER);
    j.nodes[node].num = v;
    return node;
}

static bool js_parse(Json* j, const char* data, int size) {
    memset(j, 0, sizeof(*j));
    j->text = (char*)malloc((size_t)size + 1);
    j->p = data;
    j->end = data + size;
    if (size >= 3 && (u8)data[0] == 0xEF && (u8)data[1] == 0xBB && (u8)data[2] == 0xBF) j->p += 3;
    int root = js_value(*j, 0);
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r' || *j->p == 0)) j->p++;
    return root == 0 && j->p == j->end && j->nodes[0].type == JT_OBJECT;
}

static int js_get(const Json& j, int obj, const char* key) {
    if (obj < 0 || j.nodes[obj].type != JT_OBJECT) return -1;
    for (int c = j.nodes[obj].first; c >= 0; c = j.nodes[c].next)
        if (str_eq(j.text + j.nodes[c].key, key)) return c;
    return -1;
}

static int js_count(const Json& j, int arr) { return arr >= 0 && j.nodes[arr].type == JT_ARRAY ? j.nodes[arr].count : 0; }

static int js_at(const Json& j, int arr, int i) {
    if (i < 0 || i >= js_count(j, arr)) return -1;
    int c = j.nodes[arr].first;
    while (i-- > 0) c = j.nodes[c].next;
    return c;
}

static void js_collect(const Json& j, int arr, Array<int>* out) {
    out->clear();
    if (js_count(j, arr) == 0) return;
    for (int c = j.nodes[arr].first; c >= 0; c = j.nodes[c].next) out->push(c);
}

static double js_num(const Json& j, int node, double def) { return node >= 0 && j.nodes[node].type == JT_NUMBER ? j.nodes[node].num : def; }
static float js_getf(const Json& j, int obj, const char* key, float def) { return (float)js_num(j, js_get(j, obj, key), def); }

static int js_geti(const Json& j, int obj, const char* key, int def) {
    int n = js_get(j, obj, key);
    if (n < 0 || j.nodes[n].type != JT_NUMBER) return def;
    double v = j.nodes[n].num;
    if (v < -2147483647.0) return -2147483647;
    if (v > 2147483647.0) return 2147483647;
    return (int)v;
}

static const char* js_str(const Json& j, int node) { return node >= 0 && j.nodes[node].type == JT_STRING ? j.text + j.nodes[node].str : 0; }
static const char* js_gets(const Json& j, int obj, const char* key) { return js_str(j, js_get(j, obj, key)); }

static bool js_getb(const Json& j, int obj, const char* key, bool def) {
    int n = js_get(j, obj, key);
    if (n < 0) return def;
    return j.nodes[n].type == JT_TRUE ? true : (j.nodes[n].type == JT_FALSE ? false : def);
}

// Reads exactly n numbers from the array member `key`.
static bool js_getv(const Json& j, int obj, const char* key, float* out, int n) {
    int arr = js_get(j, obj, key);
    if (js_count(j, arr) != n) return false;
    int c = j.nodes[arr].first;
    for (int i = 0; i < n; i++, c = j.nodes[c].next) {
        if (j.nodes[c].type != JT_NUMBER) return false;
        out[i] = (float)j.nodes[c].num;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shared output helpers
// ---------------------------------------------------------------------------
static float finite_or_zero(float f) { return (f == f && f < 3.0e38f && f > -3.0e38f) ? f : 0.0f; }

static void pack_normal(i8* out, vec3 n) {
    n = normalize(n);
    if (length_sq(n) < 0.5f) n = vec3(0, 1, 0);
    out[0] = (i8)clampf(roundf(n.x * 127.0f), -127, 127);
    out[1] = (i8)clampf(roundf(n.y * 127.0f), -127, 127);
    out[2] = (i8)clampf(roundf(n.z * 127.0f), -127, 127);
    out[3] = 0;
}

// The world shader decodes vertex colours with c * c.
static u8 encode_srgb(float lin) { return (u8)(sqrtf(clampf(lin, 0.0f, 1.0f)) * 255.0f + 0.5f); }

static void default_material(ModelMaterial* mt, const char* name) {
    memset(mt, 0, sizeof(*mt));
    str_copy(mt->name, sizeof(mt->name), name);
    mt->baseColor = vec4(1, 1, 1, 1);
    mt->metallic = 0.0f;
    mt->roughness = 1.0f;
    mt->image = -1;
    mt->alphaMode = MODEL_ALPHA_OPAQUE;
    mt->alphaCutoff = 0.5f;
    mt->wrapS = mt->wrapT = GL_REPEAT;
}

static Vertex material_vertex(const ModelMaterial& mt) {
    Vertex v;
    memset(&v, 0, sizeof(v));
    float e = fmaxf(mt.emissive.x, fmaxf(mt.emissive.y, mt.emissive.z));
    v.mat[0] = MAT_PAINT;
    v.mat[1] = (u8)((mt.metallic >= 0.5f ? MF_METAL : 0) | (e > 0.004f ? MF_EMISSIVE : 0));
    v.mat[2] = (u8)clampf(mt.roughness * 128.0f + 0.5f, 0.0f, 255.0f);
    v.mat[3] = (u8)clampf(e * 10.0f + 0.5f, 0.0f, 255.0f);
    v.col[0] = encode_srgb(mt.baseColor.x);
    v.col[1] = encode_srgb(mt.baseColor.y);
    v.col[2] = encode_srgb(mt.baseColor.z);
    v.col[3] = 255;
    v.weights[0] = 255;
    return v;
}

static int part_for_material(ImportedModel* m, int material) {
    for (int i = 0; i < m->parts.count; i++)
        if (m->parts[i].material == material) return i;
    ModelPart& p = m->parts.push_zero();
    p.material = material;
    return m->parts.count - 1;
}

static int decode_image(ImportedModel* m, const u8* data, int size, const char* path, const char* what) {
    int w = 0, h = 0, comp = 0;
    u8* px = size > 0 ? stbi_load_from_memory(data, size, &w, &h, &comp, 4) : 0;
    if (!px) {
        log_error("Model %s: cannot decode texture %s (%s)", path, what, size > 0 ? stbi_failure_reason() : "empty");
        return -1;
    }
    ModelImage& im = m->images.push_zero();
    im.width = w;
    im.height = h;
    im.rgba = px;
    return m->images.count - 1;
}

static bool finish_model(ImportedModel* m, const char* path) {
    bool any = false;
    vec3 mn(0, 0, 0), mx(0, 0, 0);
    for (int i = 0; i < m->parts.count; i++) {
        const ModelPart& p = m->parts[i];
        for (int k = 0; k < p.verts.count; k++) {
            vec3 v = p.verts.data[k].pos;
            if (!any) { mn = mx = v; any = true; }
            mn = vmin(mn, v);
            mx = vmax(mx, v);
        }
    }
    m->bmin = mn;
    m->bmax = mx;
    if (!any || model_triangle_count(*m) == 0) {
        log_error("Model %s: no triangle geometry", path);
        return false;
    }
    return true;
}

// Normal transform: cofactor of the upper 3x3 (inverse transpose up to scale), robust for tiny scales.
struct NormalXf {
    vec3 c0, c1, c2;
    bool flip;   // negative determinant: mirrored, triangle winding must be reversed
};

static NormalXf normal_xf(const mat4& m) {
    vec3 a(m.m[0], m.m[1], m.m[2]), b(m.m[4], m.m[5], m.m[6]), c(m.m[8], m.m[9], m.m[10]);
    float det = dot(a, cross(b, c));
    float s = det < 0 ? -1.0f : 1.0f;
    NormalXf n;
    n.c0 = cross(b, c) * s;
    n.c1 = cross(c, a) * s;
    n.c2 = cross(a, b) * s;
    n.flip = det < 0;
    return n;
}

static vec3 apply_normal_xf(const NormalXf& x, vec3 n) { return x.c0 * n.x + x.c1 * n.y + x.c2 * n.z; }

// ---------------------------------------------------------------------------
// glTF 2.0
// ---------------------------------------------------------------------------
struct GltfBuffer {
    const u8* data;
    int size;
    Array<u8> owned;
};

struct GltfView {
    const u8* data;
    int size;
    int stride;
};

struct GltfAccessor {
    const u8* data;
    int count, comps, ctype, csize, stride;
    bool normalized;
};

struct GltfMatInfo {
    int texture;     // glTF texture index, -1 = none
    int texCoord;
    float uv[6];     // KHR_texture_transform: u' = uv0*u + uv1*v + uv2, v' = uv3*u + uv4*v + uv5
};

struct Gltf {
    const char* path;
    Json js;
    ImportedModel* out;
    const u8* glbBin;
    int glbBinSize;
    Array<int> accessors, views, buffers, meshes, nodes, materials, textures, images, samplers, skins;
    Array<GltfBuffer> bufs;
    Array<GltfView> vws;
    Array<GltfMatInfo> matInfo;
    Array<int> imageMap;     // glTF image -> model image (-2 not loaded yet, -1 failed)
    Array<int> parent;
    Array<int> state;        // world matrix: 0 pending, 1 in progress, 2 done
    Array<mat4> local, world;
    Array<u8*> zeroFills;
    int defaultMaterial;
    bool warnedSparse;
};

static bool gltf_fail(Gltf& g, const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    fmt_strv(buf, sizeof(buf), f, ap);
    va_end(ap);
    log_error("Model %s: %s", g.path, buf);
    return false;
}

static void gltf_free(Gltf& g) {
    js_free(&g.js);
    g.accessors.free_mem(); g.views.free_mem(); g.buffers.free_mem(); g.meshes.free_mem(); g.nodes.free_mem();
    g.materials.free_mem(); g.textures.free_mem(); g.images.free_mem(); g.samplers.free_mem(); g.skins.free_mem();
    for (int i = 0; i < g.bufs.count; i++) g.bufs[i].owned.free_mem();
    g.bufs.free_mem();
    g.vws.free_mem();
    g.matInfo.free_mem();
    g.imageMap.free_mem();
    g.parent.free_mem();
    g.state.free_mem();
    g.local.free_mem();
    g.world.free_mem();
    for (int i = 0; i < g.zeroFills.count; i++) free(g.zeroFills[i]);
    g.zeroFills.free_mem();
}

static bool gltf_parse_glb(Gltf& g, const u8* d, int size, const u8** json, int* jsonSize) {
    if (size < 20) return gltf_fail(g, "truncated GLB header");
    u32 version, length;
    memcpy(&version, d + 4, 4);
    memcpy(&length, d + 8, 4);
    if (version != 2) return gltf_fail(g, "unsupported GLB container version %u", version);
    if (length > (u32)size || length < 20) return gltf_fail(g, "GLB is truncated (header says %u bytes, file has %d)", length, size);
    *json = 0;
    u32 off = 12;
    while (off + 8 <= length) {
        u32 clen, ctype;
        memcpy(&clen, d + off, 4);
        memcpy(&ctype, d + off + 4, 4);
        off += 8;
        if (clen > length - off) return gltf_fail(g, "GLB chunk of %u bytes runs past the end of the file", clen);
        if (ctype == 0x4E4F534Au && !*json) { *json = d + off; *jsonSize = (int)clen; }
        else if (ctype == 0x004E4942u && !g.glbBin) { g.glbBin = d + off; g.glbBinSize = (int)clen; }
        off += clen;
    }
    if (!*json) return gltf_fail(g, "GLB has no JSON chunk");
    return true;
}

static bool gltf_load_buffers(Gltf& g) {
    const Json& j = g.js;
    g.bufs.resize(g.buffers.count);
    for (int i = 0; i < g.buffers.count; i++) {
        int n = g.buffers[i];
        GltfBuffer& b = g.bufs[i];
        int len = js_geti(j, n, "byteLength", -1);
        if (len < 1) return gltf_fail(g, "buffer %d has no valid byteLength", i);
        const char* uri = js_gets(j, n, "uri");
        if (!uri) {
            if (i != 0 || !g.glbBin) return gltf_fail(g, "buffer %d has no data", i);
            b.data = g.glbBin;
            b.size = g.glbBinSize;
        } else if (str_starts(uri, "data:")) {
            if (!decode_data_uri(uri, &b.owned)) return gltf_fail(g, "buffer %d has a malformed data URI", i);
            b.data = b.owned.data;
            b.size = b.owned.count;
        } else {
            if (!read_model_file(g.path, uri, true, &b.owned)) return gltf_fail(g, "cannot read buffer file %s", uri);
            b.data = b.owned.data;
            b.size = b.owned.count;
        }
        if (b.size < len) return gltf_fail(g, "buffer %d holds %d bytes but byteLength is %d", i, b.size, len);
        b.size = len;
    }
    g.vws.resize(g.views.count);
    for (int i = 0; i < g.views.count; i++) {
        int n = g.views[i];
        int buf = js_geti(j, n, "buffer", -1);
        int off = js_geti(j, n, "byteOffset", 0);
        int len = js_geti(j, n, "byteLength", -1);
        int stride = js_geti(j, n, "byteStride", 0);
        if (buf < 0 || buf >= g.bufs.count) return gltf_fail(g, "bufferView %d references missing buffer %d", i, buf);
        if (off < 0 || len < 1 || (i64)off + len > g.bufs[buf].size) return gltf_fail(g, "bufferView %d lies outside buffer %d", i, buf);
        if (stride != 0 && (stride < 4 || stride > 252)) return gltf_fail(g, "bufferView %d has invalid byteStride %d", i, stride);
        g.vws[i].data = g.bufs[buf].data + off;
        g.vws[i].size = len;
        g.vws[i].stride = stride;
    }
    return true;
}

static bool gltf_accessor(Gltf& g, int idx, GltfAccessor* a) {
    const Json& j = g.js;
    if (idx < 0 || idx >= g.accessors.count) return gltf_fail(g, "accessor %d does not exist", idx);
    int n = g.accessors[idx];
    memset(a, 0, sizeof(*a));
    a->ctype = js_geti(j, n, "componentType", 0);
    switch (a->ctype) {
    case 5120: case 5121: a->csize = 1; break;
    case 5122: case 5123: a->csize = 2; break;
    case 5125: case 5126: a->csize = 4; break;
    default: return gltf_fail(g, "accessor %d has invalid componentType %d", idx, a->ctype);
    }
    const char* type = js_gets(j, n, "type");
    static const char* types[7] = {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
    static const int comps[7] = {1, 2, 3, 4, 4, 9, 16};
    for (int i = 0; i < 7; i++) if (str_eq(type, types[i])) a->comps = comps[i];
    if (!a->comps) return gltf_fail(g, "accessor %d has invalid type %s", idx, type ? type : "(none)");
    // MAT2 / MAT3 with 1 or 2 byte components carry column padding; never used for geometry.
    if ((a->comps == 4 || a->comps == 9) && !str_starts(type, "VEC") && a->csize < 4)
        return gltf_fail(g, "accessor %d uses an unsupported padded matrix layout", idx);
    a->count = js_geti(j, n, "count", -1);
    if (a->count < 1 || a->count > MODEL_MAX_ELEMENTS) return gltf_fail(g, "accessor %d has invalid count %d", idx, a->count);
    a->normalized = js_getb(j, n, "normalized", false);
    int elem = a->comps * a->csize;
    int view = js_geti(j, n, "bufferView", -1);
    if (js_get(j, n, "sparse") >= 0 && !g.warnedSparse) {
        log_info("Model %s: sparse accessors are not supported; using their base values", g.path);
        g.warnedSparse = true;
    }
    if (view < 0 && js_get(j, n, "bufferView") < 0) {
        if (a->count > MODEL_MAX_ZERO_FILL / elem) return gltf_fail(g, "accessor %d is too large", idx);
        u8* z = (u8*)calloc((size_t)a->count, (size_t)elem);
        g.zeroFills.push(z);
        a->data = z;
        a->stride = elem;
        return true;
    }
    if (view < 0 || view >= g.vws.count) return gltf_fail(g, "accessor %d references missing bufferView %d", idx, view);
    const GltfView& v = g.vws[view];
    int off = js_geti(j, n, "byteOffset", 0);
    a->stride = v.stride ? v.stride : elem;
    if (off < 0 || a->stride < elem) return gltf_fail(g, "accessor %d has an invalid layout", idx);
    i64 need = (i64)off + (i64)a->stride * (a->count - 1) + elem;
    if (need > v.size) return gltf_fail(g, "accessor %d (%d elements) is out of range of bufferView %d", idx, a->count, view);
    a->data = v.data + off;
    return true;
}

static float acc_f(const GltfAccessor& a, int i, int k) {
    const u8* p = a.data + (size_t)i * (size_t)a.stride + (size_t)(k * a.csize);
    switch (a.ctype) {
    case 5126: { float f; memcpy(&f, p, 4); return finite_or_zero(f); }
    case 5120: { float v = (float)(i8)p[0]; return a.normalized ? fmaxf(v / 127.0f, -1.0f) : v; }
    case 5121: return a.normalized ? p[0] / 255.0f : (float)p[0];
    case 5122: { i16 v; memcpy(&v, p, 2); return a.normalized ? fmaxf(v / 32767.0f, -1.0f) : (float)v; }
    case 5123: { u16 v; memcpy(&v, p, 2); return a.normalized ? v / 65535.0f : (float)v; }
    case 5125: { u32 v; memcpy(&v, p, 4); return a.normalized ? (float)(v / 4294967295.0) : (float)v; }
    }
    return 0.0f;
}

static u32 acc_u(const GltfAccessor& a, int i) {
    const u8* p = a.data + (size_t)i * (size_t)a.stride;
    switch (a.ctype) {
    case 5121: return p[0];
    case 5123: { u16 v; memcpy(&v, p, 2); return v; }
    case 5125: { u32 v; memcpy(&v, p, 4); return v; }
    }
    return 0xFFFFFFFFu;
}

static vec3 acc_v3(const GltfAccessor& a, int i) { return vec3(acc_f(a, i, 0), acc_f(a, i, 1), acc_f(a, i, 2)); }

// Optional vertex attribute with a matching element count.
static bool gltf_attr(Gltf& g, int attrs, const char* name, int count, int minComps, int maxComps, GltfAccessor* a, bool* present) {
    *present = false;
    if (js_get(g.js, attrs, name) < 0) return true;
    if (!gltf_accessor(g, js_geti(g.js, attrs, name, -1), a)) return false;
    if (a->comps < minComps || a->comps > maxComps || a->count != count)
        return gltf_fail(g, "attribute %s does not match POSITION (%d elements of %d components)", name, a->count, a->comps);
    *present = true;
    return true;
}

static int gltf_image(Gltf& g, int img) {
    if (img < 0 || img >= g.images.count) return -1;
    if (g.imageMap[img] != -2) return g.imageMap[img];
    const Json& j = g.js;
    int n = g.images[img];
    const char* uri = js_gets(j, n, "uri");
    int view = js_geti(j, n, "bufferView", -1);
    int result = -1;
    char what[160];
    if (view >= 0) {
        fmt_str(what, sizeof(what), "image %d (bufferView %d)", img, view);
        if (view < g.vws.count) result = decode_image(g.out, g.vws[view].data, g.vws[view].size, g.path, what);
        else log_error("Model %s: image %d references missing bufferView %d", g.path, img, view);
    } else if (uri) {
        Array<u8> data = {};
        bool dataUri = str_starts(uri, "data:");
        fmt_str(what, sizeof(what), "%s", dataUri ? "(data URI)" : uri);
        bool ok = dataUri ? decode_data_uri(uri, &data) : read_model_file(g.path, uri, true, &data);
        if (ok) result = decode_image(g.out, data.data, data.count, g.path, what);
        else log_error("Model %s: cannot read texture %s", g.path, what);
        data.free_mem();
    }
    g.imageMap[img] = result;
    return result;
}

static void gltf_texture_info(Gltf& g, int info, int matIndex) {
    const Json& j = g.js;
    GltfMatInfo& mi = g.matInfo[matIndex];
    ModelMaterial& mt = g.out->materials[matIndex];
    if (info < 0) return;
    int tex = js_geti(j, info, "index", -1);
    if (tex < 0 || tex >= g.textures.count) return;
    mi.texture = tex;
    mi.texCoord = js_geti(j, info, "texCoord", 0);
    int tt = js_get(j, js_get(j, info, "extensions"), "KHR_texture_transform");
    if (tt >= 0) {
        float off[2] = {0, 0}, sc[2] = {1, 1};
        js_getv(j, tt, "offset", off, 2);
        js_getv(j, tt, "scale", sc, 2);
        float rot = js_getf(j, tt, "rotation", 0.0f);
        float c = cosf(rot), s = sinf(rot);
        mi.uv[0] = c * sc[0]; mi.uv[1] = s * sc[1]; mi.uv[2] = off[0];
        mi.uv[3] = -s * sc[0]; mi.uv[4] = c * sc[1]; mi.uv[5] = off[1];
        mi.texCoord = js_geti(j, tt, "texCoord", mi.texCoord);
    }
    int tn = g.textures[tex];
    int src = js_geti(j, tn, "source", -1);
    if (src < 0) {
        log_error("Model %s: texture %d has no PNG/JPEG source (KTX2 / WebP textures are not supported)", g.path, tex);
        return;
    }
    mt.image = gltf_image(g, src);
    int smp = js_geti(j, tn, "sampler", -1);
    if (smp >= 0 && smp < g.samplers.count) {
        int sn = g.samplers[smp];
        int ws = js_geti(j, sn, "wrapS", 10497), wt = js_geti(j, sn, "wrapT", 10497);
        mt.wrapS = ws == 33071 ? GL_CLAMP_TO_EDGE : (ws == 33648 ? GL_MIRRORED_REPEAT : GL_REPEAT);
        mt.wrapT = wt == 33071 ? GL_CLAMP_TO_EDGE : (wt == 33648 ? GL_MIRRORED_REPEAT : GL_REPEAT);
    }
}

static void gltf_materials(Gltf& g) {
    const Json& j = g.js;
    g.out->materials.resize(g.materials.count);
    g.matInfo.resize(g.materials.count);
    for (int i = 0; i < g.materials.count; i++) {
        int n = g.materials[i];
        ModelMaterial& mt = g.out->materials[i];
        char name[64];
        fmt_str(name, sizeof(name), "material%d", i);
        default_material(&mt, js_gets(j, n, "name") ? js_gets(j, n, "name") : name);
        GltfMatInfo& mi = g.matInfo[i];
        mi.texture = -1;
        mi.uv[0] = 1; mi.uv[4] = 1;
        float f[4];
        int ext = js_get(j, n, "extensions");
        int pbr = js_get(j, n, "pbrMetallicRoughness");
        int specGloss = js_get(j, ext, "KHR_materials_pbrSpecularGlossiness");
        mt.metallic = 1.0f;
        if (pbr >= 0 || specGloss < 0) {
            if (js_getv(j, pbr, "baseColorFactor", f, 4)) mt.baseColor = vec4(f[0], f[1], f[2], f[3]);
            mt.metallic = clampf(js_getf(j, pbr, "metallicFactor", 1.0f), 0.0f, 1.0f);
            mt.roughness = clampf(js_getf(j, pbr, "roughnessFactor", 1.0f), 0.0f, 1.0f);
            gltf_texture_info(g, js_get(j, pbr, "baseColorTexture"), i);
        } else {
            if (js_getv(j, specGloss, "diffuseFactor", f, 4)) mt.baseColor = vec4(f[0], f[1], f[2], f[3]);
            mt.metallic = 0.0f;
            mt.roughness = clampf(1.0f - js_getf(j, specGloss, "glossinessFactor", 1.0f), 0.0f, 1.0f);
            gltf_texture_info(g, js_get(j, specGloss, "diffuseTexture"), i);
        }
        if (js_getv(j, n, "emissiveFactor", f, 3)) mt.emissive = vec3(f[0], f[1], f[2]);
        mt.emissive *= js_getf(j, js_get(j, ext, "KHR_materials_emissive_strength"), "emissiveStrength", 1.0f);
        if (js_get(j, ext, "KHR_materials_unlit") >= 0) {
            // Unlit: the base colour glows at full strength on top of the scene lighting
            mt.metallic = 0.0f;
            mt.emissive = vec3(1, 1, 1);
        }
        const char* am = js_gets(j, n, "alphaMode");
        mt.alphaMode = str_eq(am, "MASK") ? MODEL_ALPHA_MASK : (str_eq(am, "BLEND") ? MODEL_ALPHA_BLEND : MODEL_ALPHA_OPAQUE);
        mt.alphaCutoff = js_getf(j, n, "alphaCutoff", 0.5f);
    }
    g.defaultMaterial = -1;
}

static mat4 gltf_node_local(Gltf& g, int n) {
    const Json& j = g.js;
    float m[16];
    if (js_getv(j, n, "matrix", m, 16)) {
        mat4 r;
        for (int i = 0; i < 16; i++) r.m[i] = finite_or_zero(m[i]);
        return r;
    }
    float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    js_getv(j, n, "translation", t, 3);
    js_getv(j, n, "rotation", q, 4);
    js_getv(j, n, "scale", s, 3);
    return mat4_trs(vec3(t[0], t[1], t[2]), quat_normalize(quat(q[0], q[1], q[2], q[3])), vec3(s[0], s[1], s[2]));
}

static bool gltf_world(Gltf& g, int n, int depth) {
    if (g.state[n] == 2) return true;
    if (g.state[n] == 1 || depth > 1024) return gltf_fail(g, "node hierarchy contains a cycle");
    g.state[n] = 1;
    int p = g.parent[n];
    if (p >= 0) {
        if (!gltf_world(g, p, depth + 1)) return false;
        g.world[n] = g.world[p] * g.local[n];
    } else {
        g.world[n] = g.local[n];
    }
    g.state[n] = 2;
    return true;
}

static bool gltf_nodes(Gltf& g) {
    const Json& j = g.js;
    int count = g.nodes.count;
    g.parent.resize(count);
    g.state.resize(count);
    g.local.resize(count);
    g.world.resize(count);
    for (int i = 0; i < count; i++) {
        g.parent[i] = -1;
        g.local[i] = gltf_node_local(g, g.nodes[i]);
    }
    for (int i = 0; i < count; i++) {
        int ch = js_get(j, g.nodes[i], "children");
        for (int k = 0; k < js_count(j, ch); k++) {
            int c = (int)js_num(j, js_at(j, ch, k), -1);
            if (c < 0 || c >= count) return gltf_fail(g, "node %d has invalid child %d", i, c);
            if (g.parent[c] >= 0 || c == i) return gltf_fail(g, "node %d has more than one parent", c);
            g.parent[c] = i;
        }
    }
    for (int i = 0; i < count; i++)
        if (!gltf_world(g, i, 0)) return false;
    return true;
}

// Per-vertex skinning matrix in the default node pose.
static bool gltf_skin_matrix(Gltf& g, const Array<mat4>& joints, const GltfAccessor& ja, const GltfAccessor& wa, int v, mat4* out) {
    mat4 m;
    memset(&m, 0, sizeof(m));
    float wsum = 0.0f;
    for (int k = 0; k < 4; k++) {
        float w = acc_f(wa, v, k);
        if (w <= 0.0f) continue;
        int jt = (int)acc_f(ja, v, k);
        if (jt < 0 || jt >= joints.count) return gltf_fail(g, "vertex %d references missing joint %d", v, jt);
        for (int e = 0; e < 16; e++) m.m[e] += joints.data[jt].m[e] * w;
        wsum += w;
    }
    if (wsum <= 1e-6f) m = mat4_identity();
    else if (fabsf(wsum - 1.0f) > 1e-3f) for (int e = 0; e < 16; e++) m.m[e] /= wsum;
    *out = m;
    return true;
}

static bool gltf_skin_joints(Gltf& g, int skin, Array<mat4>* joints) {
    const Json& j = g.js;
    int sn = g.skins[skin];
    int arr = js_get(j, sn, "joints");
    int count = js_count(j, arr);
    if (count < 1) return gltf_fail(g, "skin %d has no joints", skin);
    GltfAccessor ib;
    bool hasIB = js_get(j, sn, "inverseBindMatrices") >= 0;
    if (hasIB) {
        if (!gltf_accessor(g, js_geti(j, sn, "inverseBindMatrices", -1), &ib)) return false;
        if (ib.comps != 16 || ib.ctype != 5126 || ib.count < count) return gltf_fail(g, "skin %d has invalid inverseBindMatrices", skin);
    }
    joints->resize(count);
    for (int k = 0; k < count; k++) {
        int node = (int)js_num(j, js_at(j, arr, k), -1);
        if (node < 0 || node >= g.nodes.count) return gltf_fail(g, "skin %d references missing joint node %d", skin, node);
        mat4 inv = mat4_identity();
        if (hasIB) for (int e = 0; e < 16; e++) inv.m[e] = acc_f(ib, k, e);
        joints->data[k] = g.world[node] * inv;
    }
    return true;
}

static bool gltf_primitive(Gltf& g, int prim, const mat4& xf, const Array<mat4>* joints) {
    const Json& j = g.js;
    int mode = js_geti(j, prim, "mode", 4);
    if (mode != 4 && mode != 5 && mode != 6) return true;   // points and lines are skipped
    int attrs = js_get(j, prim, "attributes");
    if (js_get(j, attrs, "POSITION") < 0) return true;
    GltfAccessor pa, na, ta, ca, ja, wa;
    if (!gltf_accessor(g, js_geti(j, attrs, "POSITION", -1), &pa)) return false;
    if (pa.comps != 3) return gltf_fail(g, "POSITION must be VEC3");
    int count = pa.count;
    bool hasN, hasT, hasC, hasJ = false, hasW = false;
    int mi = js_geti(j, prim, "material", -1);
    if (mi >= g.materials.count || mi < -1) return gltf_fail(g, "primitive references missing material %d", mi);
    if (mi < 0) {
        if (g.defaultMaterial < 0) {
            ModelMaterial& dm = g.out->materials.push_zero();
            default_material(&dm, "default");
            dm.metallic = 1.0f;
            GltfMatInfo& dmi = g.matInfo.push_zero();
            dmi.texture = -1;
            dmi.uv[0] = 1; dmi.uv[4] = 1;
            g.defaultMaterial = g.out->materials.count - 1;
        }
        mi = g.defaultMaterial;
    }
    const GltfMatInfo info = g.matInfo[mi];
    char uvName[16];
    fmt_str(uvName, sizeof(uvName), "TEXCOORD_%d", info.texCoord);
    if (!gltf_attr(g, attrs, "NORMAL", count, 3, 3, &na, &hasN)) return false;
    if (!gltf_attr(g, attrs, uvName, count, 2, 2, &ta, &hasT)) return false;
    if (!gltf_attr(g, attrs, "COLOR_0", count, 3, 4, &ca, &hasC)) return false;
    if (joints) {
        if (!gltf_attr(g, attrs, "JOINTS_0", count, 4, 4, &ja, &hasJ)) return false;
        if (!gltf_attr(g, attrs, "WEIGHTS_0", count, 4, 4, &wa, &hasW)) return false;
    }
    bool skinned = hasJ && hasW;

    // Triangle list
    Array<u32> tri = {};
    int idxCount = count;
    GltfAccessor ia;
    bool indexed = js_get(j, prim, "indices") >= 0;
    if (indexed) {
        if (!gltf_accessor(g, js_geti(j, prim, "indices", -1), &ia)) return false;
        if (ia.comps != 1 || (ia.ctype != 5121 && ia.ctype != 5123 && ia.ctype != 5125)) return gltf_fail(g, "indices must be unsigned integer scalars");
        idxCount = ia.count;
    }
    Array<u32> src = {};
    src.resize(idxCount);
    for (int i = 0; i < idxCount; i++) {
        u32 v = indexed ? acc_u(ia, i) : (u32)i;
        if (v >= (u32)count) {
            src.free_mem();
            return gltf_fail(g, "index %u is out of range (%d vertices)", v, count);
        }
        src[i] = v;
    }
    if (mode == 4) {
        for (int i = 0; i + 2 < idxCount; i += 3) { tri.push(src[i]); tri.push(src[i + 1]); tri.push(src[i + 2]); }
    } else if (mode == 5) {
        for (int i = 0; i + 2 < idxCount; i++) {
            bool odd = (i & 1) != 0;
            tri.push(src[i + (odd ? 1 : 0)]); tri.push(src[i + (odd ? 0 : 1)]); tri.push(src[i + 2]);
        }
    } else {
        for (int i = 1; i + 1 < idxCount; i++) { tri.push(src[0]); tri.push(src[i]); tri.push(src[i + 1]); }
    }
    src.free_mem();

    // Transformed vertices
    Array<Vertex> vs = {};
    vs.resize(count);
    Vertex tmpl = material_vertex(g.out->materials[mi]);
    vec4 bc = g.out->materials[mi].baseColor;
    NormalXf nx = normal_xf(xf);
    bool flip = nx.flip;
    for (int i = 0; i < count; i++) {
        Vertex v = tmpl;
        vec3 p = acc_v3(pa, i), nrm = hasN ? acc_v3(na, i) : vec3(0, 1, 0);
        if (skinned) {
            mat4 sm;
            if (!gltf_skin_matrix(g, *joints, ja, wa, i, &sm)) { vs.free_mem(); tri.free_mem(); return false; }
            v.pos = transform_point(sm, p);
            pack_normal(v.nrm, apply_normal_xf(normal_xf(sm), nrm));
        } else {
            v.pos = transform_point(xf, p);
            pack_normal(v.nrm, apply_normal_xf(nx, nrm));
        }
        if (hasT) {
            float u = acc_f(ta, i, 0), w = acc_f(ta, i, 1);
            v.uv = vec2(info.uv[0] * u + info.uv[1] * w + info.uv[2], info.uv[3] * u + info.uv[4] * w + info.uv[5]);
        }
        if (hasC) {
            v.col[0] = encode_srgb(bc.x * acc_f(ca, i, 0));
            v.col[1] = encode_srgb(bc.y * acc_f(ca, i, 1));
            v.col[2] = encode_srgb(bc.z * acc_f(ca, i, 2));
        }
        vs[i] = v;
    }
    if (skinned && joints->count > 0) flip = normal_xf(joints->data[0]).flip;

    ModelPart& part = g.out->parts[part_for_material(g.out, mi)];
    if (hasN) {
        u32 base = (u32)part.verts.count;
        for (int i = 0; i < count; i++) part.verts.push(vs[i]);
        for (int i = 0; i + 2 < tri.count; i += 3) {
            part.inds.push(base + tri[i]);
            part.inds.push(base + tri[i + (flip ? 2 : 1)]);
            part.inds.push(base + tri[i + (flip ? 1 : 2)]);
        }
    } else {
        // No normals: flat shading, one vertex per triangle corner
        for (int i = 0; i + 2 < tri.count; i += 3) {
            Vertex a = vs[tri[i]], b = vs[tri[i + (flip ? 2 : 1)]], c = vs[tri[i + (flip ? 1 : 2)]];
            vec3 fn = cross(b.pos - a.pos, c.pos - a.pos);
            pack_normal(a.nrm, fn);
            memcpy(b.nrm, a.nrm, 4);
            memcpy(c.nrm, a.nrm, 4);
            u32 base = (u32)part.verts.count;
            part.verts.push(a); part.verts.push(b); part.verts.push(c);
            part.inds.push(base); part.inds.push(base + 1); part.inds.push(base + 2);
        }
    }
    vs.free_mem();
    tri.free_mem();
    return true;
}

static bool gltf_node_mesh(Gltf& g, int node) {
    const Json& j = g.js;
    int nn = g.nodes[node];
    int mesh = js_geti(j, nn, "mesh", -1);
    if (js_get(j, nn, "mesh") < 0) return true;
    if (mesh < 0 || mesh >= g.meshes.count) return gltf_fail(g, "node %d references missing mesh %d", node, mesh);
    Array<mat4> joints = {};
    int skin = js_geti(j, nn, "skin", -1);
    bool skinned = js_get(j, nn, "skin") >= 0;
    if (skinned) {
        if (skin < 0 || skin >= g.skins.count) return gltf_fail(g, "node %d references missing skin %d", node, skin);
        if (!gltf_skin_joints(g, skin, &joints)) { joints.free_mem(); return false; }
    }
    int prims = js_get(j, g.meshes[mesh], "primitives");
    bool ok = true;
    for (int k = 0; k < js_count(j, prims) && ok; k++)
        ok = gltf_primitive(g, js_at(j, prims, k), g.world[node], skinned ? &joints : 0);
    joints.free_mem();
    return ok;
}

bool model_import_gltf(const u8* data, int size, const char* path, ImportedModel* out) {
    memset(out, 0, sizeof(*out));
    Gltf g;
    memset(&g, 0, sizeof(g));
    g.path = path;
    g.out = out;
    const u8* json = data;
    int jsonSize = size;
    bool ok = data && size > 0;
    if (ok && size >= 4 && memcmp(data, "glTF", 4) == 0) ok = gltf_parse_glb(g, data, size, &json, &jsonSize);
    if (ok && !js_parse(&g.js, (const char*)json, jsonSize)) ok = gltf_fail(g, "malformed JSON near byte %d", (int)(g.js.p - (const char*)json));
    const Json& j = g.js;
    int root = 0;
    if (ok) {
        const char* ver = js_gets(j, js_get(j, root, "asset"), "version");
        if (!ver || ver[0] != '2') ok = gltf_fail(g, "not a glTF 2.0 file (asset.version %s)", ver ? ver : "missing");
    }
    if (ok) {
        static const char* supported[] = {"KHR_mesh_quantization", "KHR_texture_transform", "KHR_materials_emissive_strength",
                                          "KHR_materials_unlit", "KHR_materials_pbrSpecularGlossiness"};
        int req = js_get(j, root, "extensionsRequired");
        for (int i = 0; i < js_count(j, req) && ok; i++) {
            const char* e = js_str(j, js_at(j, req, i));
            bool known = false;
            for (int k = 0; k < ARRAY_COUNT(supported); k++) known |= str_eq(e, supported[k]);
            if (!known) ok = gltf_fail(g, "requires unsupported extension %s (re-export without compression)", e ? e : "?");
        }
    }
    if (ok) {
        js_collect(j, js_get(j, root, "accessors"), &g.accessors);
        js_collect(j, js_get(j, root, "bufferViews"), &g.views);
        js_collect(j, js_get(j, root, "buffers"), &g.buffers);
        js_collect(j, js_get(j, root, "meshes"), &g.meshes);
        js_collect(j, js_get(j, root, "nodes"), &g.nodes);
        js_collect(j, js_get(j, root, "materials"), &g.materials);
        js_collect(j, js_get(j, root, "textures"), &g.textures);
        js_collect(j, js_get(j, root, "images"), &g.images);
        js_collect(j, js_get(j, root, "samplers"), &g.samplers);
        js_collect(j, js_get(j, root, "skins"), &g.skins);
        g.imageMap.resize(g.images.count);
        for (int i = 0; i < g.images.count; i++) g.imageMap[i] = -2;
        ok = gltf_load_buffers(g);
    }
    if (ok) {
        gltf_materials(g);
        ok = gltf_nodes(g);
    }
    if (ok) {
        // Default scene (or every root node when the file defines no scenes)
        Array<int> stack = {};
        Array<u8> visited = {};
        visited.resize(g.nodes.count);
        int scenes = js_get(j, root, "scenes");
        if (js_count(j, scenes) > 0) {
            int si = js_geti(j, root, "scene", 0);
            int sn = js_at(j, scenes, si);
            if (sn < 0) ok = gltf_fail(g, "default scene %d does not exist", si);
            int rn = js_get(j, sn, "nodes");
            for (int k = js_count(j, rn) - 1; k >= 0 && ok; k--) {
                int n = (int)js_num(j, js_at(j, rn, k), -1);
                if (n < 0 || n >= g.nodes.count) ok = gltf_fail(g, "scene references missing node %d", n);
                else stack.push(n);
            }
        } else {
            for (int n = g.nodes.count - 1; n >= 0; n--) if (g.parent[n] < 0) stack.push(n);
        }
        while (ok && stack.count > 0) {
            int n = stack.last();
            stack.pop();
            if (visited[n]) continue;
            visited[n] = 1;
            ok = gltf_node_mesh(g, n);
            int ch = js_get(j, g.nodes[n], "children");
            for (int k = js_count(j, ch) - 1; k >= 0; k--) stack.push((int)js_num(j, js_at(j, ch, k), 0));
        }
        stack.free_mem();
        visited.free_mem();
    }
    if (ok) ok = finish_model(out, path);
    gltf_free(g);
    if (!ok) model_free(out);
    return ok;
}

// ---------------------------------------------------------------------------
// Wavefront OBJ + MTL
// ---------------------------------------------------------------------------
struct ObjCorner { int v, t, n; };
struct ObjSlot { int mat, v, t, n; u32 index; };

struct Obj {
    const char* path;
    ImportedModel* out;
    Array<vec3> pos, col, nrm;
    Array<vec2> uv;
    bool hasCol;
    Array<ObjSlot> slots;     // (material, v, vt, vn) -> vertex index in that material's part
    int used;
    Array<char*> imagePaths;  // decoded texture paths, parallel to out->images
    int lineNo;
};

// Splits a line in place into whitespace separated tokens.
static void split_tokens(char* s, Array<char*>* toks) {
    toks->clear();
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (!*s) return;
        toks->push(s);
        while (*s && *s != ' ' && *s != '\t' && *s != '\r') s++;
        if (!*s) return;
        *s++ = 0;
    }
}

// Copies the next line (without the newline) into buf; false at the end of the data.
static bool next_line(const u8** cur, const u8* end, Array<char>* buf) {
    if (*cur >= end) return false;
    const u8* s = *cur;
    const u8* e = s;
    while (e < end && *e != '\n') e++;
    int len = (int)(e - s);
    buf->resize(len + 1);
    memcpy(buf->data, s, (size_t)len);
    buf->data[len] = 0;
    for (int i = 0; i < len; i++) if (buf->data[i] == '#') { buf->data[i] = 0; break; }
    *cur = e < end ? e + 1 : end;
    return true;
}

static bool obj_is_number(const char* s) {
    if (*s == '-' || *s == '+') s++;
    return (*s >= '0' && *s <= '9') || (*s == '.' && s[1] >= '0' && s[1] <= '9');
}

static int obj_image(Obj& o, const char* mtlPath, const char* file) {
    char full[512];
    if (!resolve_path(mtlPath, file, false, full, sizeof(full))) full[0] = 0;
    for (int i = 0; i < o.imagePaths.count; i++)
        if (str_eq(o.imagePaths[i], full)) return i < o.out->images.count ? i : -1;
    Array<u8> data = {};
    int img = -1;
    if (read_model_file(mtlPath, file, false, &data)) img = decode_image(o.out, data.data, data.count, o.path, file);
    else log_error("Model %s: cannot read texture %s", o.path, file);
    data.free_mem();
    if (img >= 0) {
        char* p = (char*)malloc(strlen(full) + 1);
        memcpy(p, full, strlen(full) + 1);
        o.imagePaths.push(p);
    }
    return img;
}

// Texture statement: options (-o u v w, -clamp on, ...) followed by a file name that may contain spaces.
static void obj_map(Obj& o, const char* mtlPath, Array<char*>& toks, ModelMaterial* mt) {
    int i = 1;
    bool clamp = false;
    while (i < toks.count && toks[i][0] == '-') {
        const char* opt = toks[i++];
        if (str_eq(opt, "-o") || str_eq(opt, "-s") || str_eq(opt, "-t")) {
            for (int k = 0; k < 3 && i < toks.count - 1 && obj_is_number(toks[i]); k++) i++;
        } else if (str_eq(opt, "-mm")) {
            i += 2;
        } else if (str_eq(opt, "-clamp")) {
            clamp = i < toks.count && str_eq(toks[i], "on");
            i++;
        } else if (str_eq(opt, "-blendu") || str_eq(opt, "-blendv") || str_eq(opt, "-cc") || str_eq(opt, "-bm") || str_eq(opt, "-boost") ||
                   str_eq(opt, "-texres") || str_eq(opt, "-imfchan") || str_eq(opt, "-type")) {
            i++;
        }
    }
    if (i >= toks.count) return;
    char file[512];
    file[0] = 0;
    for (int k = i; k < toks.count; k++) {
        int n = (int)strlen(file);
        fmt_str(file + n, (int)sizeof(file) - n, k > i ? " %s" : "%s", toks[k]);
    }
    mt->image = obj_image(o, mtlPath, file);
    if (clamp) mt->wrapS = mt->wrapT = GL_CLAMP_TO_EDGE;
}

static void obj_load_mtl(Obj& o, const char* file) {
    char mtlPath[512];
    Array<u8> data = {};
    if (!resolve_path(o.path, file, false, mtlPath, sizeof(mtlPath)) || !read_model_file(o.path, file, false, &data)) {
        log_error("Model %s: cannot read material library %s", o.path, file);
        data.free_mem();
        return;
    }
    Array<char> line = {};
    Array<char*> toks = {};
    const u8* cur = data.data;
    int mat = -1;
    while (next_line(&cur, data.data + data.count, &line)) {
        split_tokens(line.data, &toks);
        if (toks.count == 0) continue;
        const char* k = toks[0];
        if (str_eq(k, "newmtl")) {
            ModelMaterial& mt = o.out->materials.push_zero();
            default_material(&mt, toks.count > 1 ? toks[1] : "unnamed");
            mat = o.out->materials.count - 1;
            continue;
        }
        if (mat < 0) continue;
        ModelMaterial& mt = o.out->materials[mat];
        float a = toks.count > 1 ? str_to_float(toks[1]) : 0.0f;
        vec3 c(a, toks.count > 2 ? str_to_float(toks[2]) : a, toks.count > 3 ? str_to_float(toks[3]) : a);
        if (str_eq(k, "Kd") && toks.count > 1 && obj_is_number(toks[1])) mt.baseColor = vec4(c, mt.baseColor.w);
        else if (str_eq(k, "Ke") && toks.count > 1 && obj_is_number(toks[1])) mt.emissive = c;
        else if (str_eq(k, "Ns")) mt.roughness = 1.0f - sqrtf(clampf(a, 0.0f, 900.0f)) / 30.0f;   // Blender: Ns = ((1 - r) * 30)^2
        else if (str_eq(k, "Pr")) mt.roughness = clampf(a, 0.0f, 1.0f);
        else if (str_eq(k, "Pm")) mt.metallic = clampf(a, 0.0f, 1.0f);
        else if (str_eq(k, "d")) mt.baseColor.w = clampf(a, 0.0f, 1.0f);
        else if (str_eq(k, "Tr")) mt.baseColor.w = clampf(1.0f - a, 0.0f, 1.0f);
        else if (str_eq(k, "map_Kd")) obj_map(o, mtlPath, toks, &mt);
        else if (str_eq(k, "map_d")) mt.alphaMode = MODEL_ALPHA_MASK;
    }
    for (int i = 0; i < o.out->materials.count; i++) {
        ModelMaterial& mt = o.out->materials[i];
        if (mt.alphaMode == MODEL_ALPHA_OPAQUE && mt.baseColor.w < 0.999f) mt.alphaMode = MODEL_ALPHA_BLEND;
    }
    line.free_mem();
    toks.free_mem();
    data.free_mem();
}

static int obj_material(Obj& o, const char* name) {
    for (int i = 0; i < o.out->materials.count; i++)
        if (str_eq(o.out->materials[i].name, name)) return i;
    if (!str_eq(name, "default")) log_error("Model %s: material %s is not defined; using white", o.path, name);
    ModelMaterial& mt = o.out->materials.push_zero();
    default_material(&mt, name);
    return o.out->materials.count - 1;
}

// "v", "v/vt", "v//vn" or "v/vt/vn"; 1-based, negative = relative to the end.
static bool obj_corner(Obj& o, const char* s, ObjCorner* c) {
    int vals[3] = {0, 0, 0};
    int counts[3] = {o.pos.count, o.uv.count, o.nrm.count};
    for (int f = 0; f < 3; f++) {
        bool neg = false, digits = false;
        int v = 0;
        if (*s == '-') { neg = true; s++; }
        while (*s >= '0' && *s <= '9') {
            if (v < 100000000) v = v * 10 + (*s - '0');
            digits = true;
            s++;
        }
        if (digits) {
            int idx = neg ? counts[f] - v : v - 1;
            if (v == 0 || idx < 0 || idx >= counts[f]) return false;
            vals[f] = idx + 1;
        } else if (neg || (f == 0)) {
            return false;
        }
        if (*s == '/') s++;
        else if (*s) return false;
        else break;
    }
    c->v = vals[0] - 1;
    c->t = vals[1] - 1;
    c->n = vals[2] - 1;
    return true;
}

static u32 obj_slot_hash(int mat, int v, int t, int n) {
    return hash_u32((u32)v * 73856093u ^ (u32)(t + 1) * 19349663u ^ (u32)(n + 1) * 83492791u ^ (u32)mat * 2654435761u);
}

static void obj_grow(Obj& o) {
    Array<ObjSlot> old = o.slots;
    int cap = old.count ? old.count * 2 : 4096;
    o.slots = Array<ObjSlot>();
    o.slots.resize(cap);
    for (int i = 0; i < cap; i++) o.slots[i].mat = -1;
    for (int i = 0; i < old.count; i++) {
        const ObjSlot& s = old[i];
        if (s.mat < 0) continue;
        u32 h = obj_slot_hash(s.mat, s.v, s.t, s.n) & (u32)(cap - 1);
        while (o.slots[(int)h].mat >= 0) h = (h + 1) & (u32)(cap - 1);
        o.slots[(int)h] = s;
    }
    old.free_mem();
}

static Vertex obj_vertex(Obj& o, const ModelMaterial& mt, const ObjCorner& c, vec3 faceN) {
    Vertex v = material_vertex(mt);
    v.pos = o.pos[c.v];
    pack_normal(v.nrm, c.n >= 0 ? o.nrm[c.n] : faceN);
    if (c.t >= 0) v.uv = vec2(o.uv[c.t].x, 1.0f - o.uv[c.t].y);
    if (o.hasCol) {
        vec3 vc = o.col[c.v];
        v.col[0] = encode_srgb(mt.baseColor.x * vc.x);
        v.col[1] = encode_srgb(mt.baseColor.y * vc.y);
        v.col[2] = encode_srgb(mt.baseColor.z * vc.z);
    }
    return v;
}

static void obj_triangle(Obj& o, int mat, const ObjCorner* c) {
    ModelPart& part = o.out->parts[part_for_material(o.out, mat)];
    const ModelMaterial& mt = o.out->materials[mat];
    bool smooth = c[0].n >= 0 && c[1].n >= 0 && c[2].n >= 0;
    if (!smooth) {
        vec3 fn = cross(o.pos[c[1].v] - o.pos[c[0].v], o.pos[c[2].v] - o.pos[c[0].v]);
        for (int k = 0; k < 3; k++) {
            ObjCorner flatC = c[k];
            flatC.n = -1;
            part.inds.push((u32)part.verts.count);
            part.verts.push(obj_vertex(o, mt, flatC, fn));
        }
        return;
    }
    for (int k = 0; k < 3; k++) {
        if ((o.used + 1) * 2 > o.slots.count) obj_grow(o);
        u32 mask = (u32)(o.slots.count - 1);
        u32 h = obj_slot_hash(mat, c[k].v, c[k].t, c[k].n) & mask;
        for (;;) {
            ObjSlot& s = o.slots[(int)h];
            if (s.mat < 0) {
                s.mat = mat; s.v = c[k].v; s.t = c[k].t; s.n = c[k].n;
                s.index = (u32)part.verts.count;
                part.verts.push(obj_vertex(o, mt, c[k], vec3(0, 1, 0)));
                o.used++;
                part.inds.push(s.index);
                break;
            }
            if (s.mat == mat && s.v == c[k].v && s.t == c[k].t && s.n == c[k].n) { part.inds.push(s.index); break; }
            h = (h + 1) & mask;
        }
    }
}

bool model_import_obj(const u8* data, int size, const char* path, ImportedModel* out) {
    memset(out, 0, sizeof(*out));
    Obj o;
    memset(&o, 0, sizeof(o));
    o.path = path;
    o.out = out;
    Array<char> line = {};
    Array<char*> toks = {};
    Array<ObjCorner> face = {};
    const u8* cur = data;
    int mat = -1;
    bool ok = data != 0 && size > 0;
    if (!ok) log_error("Model %s: empty file", path);
    while (ok && next_line(&cur, data + size, &line)) {
        o.lineNo++;
        split_tokens(line.data, &toks);
        if (toks.count == 0) continue;
        const char* k = toks[0];
        if (str_eq(k, "v")) {
            if (toks.count < 4) { ok = false; break; }
            o.pos.push(vec3(str_to_float(toks[1]), str_to_float(toks[2]), str_to_float(toks[3])));
            if (toks.count >= 7) {
                if (!o.hasCol) { o.col.resize(o.pos.count); for (int i = 0; i < o.pos.count; i++) o.col[i] = vec3(1, 1, 1); }
                o.hasCol = true;
            }
            if (o.hasCol) {
                vec3 c(1, 1, 1);
                if (toks.count >= 7) c = vec3(str_to_float(toks[4]), str_to_float(toks[5]), str_to_float(toks[6]));
                if (o.col.count < o.pos.count) o.col.push(c);
                else o.col[o.pos.count - 1] = c;
            }
        } else if (str_eq(k, "vt")) {
            if (toks.count < 2) { ok = false; break; }
            o.uv.push(vec2(str_to_float(toks[1]), toks.count > 2 ? str_to_float(toks[2]) : 0.0f));
        } else if (str_eq(k, "vn")) {
            if (toks.count < 4) { ok = false; break; }
            o.nrm.push(vec3(str_to_float(toks[1]), str_to_float(toks[2]), str_to_float(toks[3])));
        } else if (str_eq(k, "f")) {
            face.clear();
            for (int i = 1; i < toks.count && ok; i++) {
                ObjCorner c;
                ok = obj_corner(o, toks[i], &c);
                face.push(c);
            }
            if (!ok) break;
            if (face.count < 3) continue;
            if (mat < 0) mat = obj_material(o, "default");
            for (int i = 1; i + 1 < face.count; i++) {
                ObjCorner t[3] = {face[0], face[i], face[i + 1]};
                obj_triangle(o, mat, t);
            }
        } else if (str_eq(k, "usemtl")) {
            mat = obj_material(o, toks.count > 1 ? toks[1] : "default");
        } else if (str_eq(k, "mtllib")) {
            for (int i = 1; i < toks.count; i++) obj_load_mtl(o, toks[i]);
        }
    }
    if (!ok && data && size > 0) log_error("Model %s: malformed or out-of-range OBJ data on line %d", path, o.lineNo);
    if (ok) ok = finish_model(out, path);
    line.free_mem();
    toks.free_mem();
    face.free_mem();
    o.pos.free_mem();
    o.col.free_mem();
    o.nrm.free_mem();
    o.uv.free_mem();
    o.slots.free_mem();
    for (int i = 0; i < o.imagePaths.count; i++) free(o.imagePaths[i]);
    o.imagePaths.free_mem();
    if (!ok) model_free(out);
    return ok;
}

// ---------------------------------------------------------------------------
// Public CPU API
// ---------------------------------------------------------------------------
bool model_import(const char* path, ImportedModel* out) {
    memset(out, 0, sizeof(*out));
    char p[512];
    str_copy(p, sizeof(p), path);
    for (char* c = p; *c; c++) if (*c == '\\') *c = '/';
    if (!normalize_path(p)) { log_error("Model %s: invalid path", path); return false; }
    Array<u8> data = {};
    if (!plat_read_asset(p, &data)) {
        log_error("Model %s: file not found", p);
        return false;
    }
    bool ok;
    double t0 = plat_time_seconds();
    if (path_has_ext(p, ".obj")) ok = model_import_obj(data.data, data.count, p, out);
    else if (path_has_ext(p, ".gltf") || path_has_ext(p, ".glb") || (data.count >= 4 && memcmp(data.data, "glTF", 4) == 0)) ok = model_import_gltf(data.data, data.count, p, out);
    else { log_error("Model %s: unsupported format (expected .gltf, .glb or .obj)", p); ok = false; }
    data.free_mem();
    if (ok) log_info("Model %s: %d vertices, %d triangles, %d materials, %d textures (%.2fs)", p, model_vertex_count(*out), model_triangle_count(*out),
                     out->parts.count, out->images.count, plat_time_seconds() - t0);
    return ok;
}

void model_free(ImportedModel* m) {
    for (int i = 0; i < m->images.count; i++) stbi_image_free(m->images[i].rgba);
    for (int i = 0; i < m->parts.count; i++) {
        m->parts[i].verts.free_mem();
        m->parts[i].inds.free_mem();
    }
    m->images.free_mem();
    m->materials.free_mem();
    m->parts.free_mem();
}

int model_vertex_count(const ImportedModel& m) {
    int n = 0;
    for (int i = 0; i < m.parts.count; i++) n += m.parts[i].verts.count;
    return n;
}

int model_triangle_count(const ImportedModel& m) {
    int n = 0;
    for (int i = 0; i < m.parts.count; i++) n += m.parts[i].inds.count / 3;
    return n;
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------
static u32 create_texture(const u8* rgba, int w, int h, int wrapS, int wrapT) {
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    u8* scaled = 0;
    while (maxSize > 0 && (w > maxSize || h > maxSize)) {
        // Only when the GPU cannot hold the original size: 2x2 box filter
        int nw = max_t(1, w / 2), nh = max_t(1, h / 2);
        u8* d = (u8*)malloc((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    int x0 = min_t(x * 2, w - 1), x1 = min_t(x * 2 + 1, w - 1), y0 = min_t(y * 2, h - 1), y1 = min_t(y * 2 + 1, h - 1);
                    int s = rgba[(y0 * w + x0) * 4 + c] + rgba[(y0 * w + x1) * 4 + c] + rgba[(y1 * w + x0) * 4 + c] + rgba[(y1 * w + x1) * 4 + c];
                    d[(y * nw + x) * 4 + c] = (u8)((s + 2) / 4);
                }
        log_info("Model texture %dx%d exceeds GL_MAX_TEXTURE_SIZE %d; using %dx%d", w, h, maxSize, nw, nh);
        free(scaled);
        scaled = d;
        rgba = d;
        w = nw;
        h = nh;
    }
    u32 t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glGenerateMipmap(GL_TEXTURE_2D);
    if (!gl_is_es()) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    glGetError(); // anisotropy may be unsupported; ignore that error
    glBindTexture(GL_TEXTURE_2D, 0);
    free(scaled);
    return t;
}

struct TexKey { int image, alpha, wrapS, wrapT; u32 tex; };

// Alpha handling: opaque materials ignore texture alpha, masks are resolved against the
// cutoff here, blended materials keep their alpha (the shader cuts out below 50%).
static int alpha_key(const ModelMaterial& mt) {
    if (mt.alphaMode == MODEL_ALPHA_OPAQUE) return 0;
    if (mt.alphaMode == MODEL_ALPHA_BLEND) return 1;
    float cut = mt.baseColor.w > 1e-4f ? mt.alphaCutoff / mt.baseColor.w : 1e9f;
    return 2 + (int)clampf(cut * 255.0f + 0.5f, 0.0f, 256.0f);
}

bool model_upload(const ImportedModel& m, ModelGpu* out) {
    memset(out, 0, sizeof(*out));
    Array<TexKey> cache = {};
    for (int i = 0; i < m.parts.count; i++) {
        const ModelPart& p = m.parts[i];
        if (p.inds.count == 0) continue;
        const ModelMaterial& mt = m.materials[p.material];
        int image = mt.image >= 0 && mt.image < m.images.count ? mt.image : -1;
        int ak = alpha_key(mt);
        u32 tex = 0;
        for (int k = 0; k < cache.count && !tex; k++)
            if (cache[k].image == image && cache[k].alpha == ak && cache[k].wrapS == mt.wrapS && cache[k].wrapT == mt.wrapT) tex = cache[k].tex;
        if (!tex) {
            u8 white[4] = {255, 255, 255, 255};
            const ModelImage* im = image >= 0 ? &m.images[image] : 0;
            int w = im ? im->width : 1, h = im ? im->height : 1;
            size_t n = (size_t)w * h;
            u8* px = (u8*)malloc(n * 4);
            memcpy(px, im ? im->rgba : white, n * 4);
            for (size_t k = 0; k < n; k++) {
                u8& a = px[k * 4 + 3];
                if (ak == 0) a = 255;
                else if (ak >= 2) a = a >= ak - 2 ? 255 : 0;
            }
            tex = create_texture(px, w, h, mt.wrapS, mt.wrapT);
            free(px);
            TexKey key = {image, ak, mt.wrapS, mt.wrapT, tex};
            cache.push(key);
            out->textures.push(tex);
        }
        MeshBuilder mb;
        memset(&mb, 0, sizeof(mb));
        mb.verts = p.verts;
        mb.inds = p.inds;
        ModelGpuPart& gp = out->parts.push_zero();
        gp.mesh = mesh_create(mb);
        gp.tex = tex;
    }
    cache.free_mem();
    out->bmin = m.bmin;
    out->bmax = m.bmax;
    return out->parts.count > 0;
}

void model_gpu_destroy(ModelGpu* g) {
    for (int i = 0; i < g->parts.count; i++) mesh_destroy(&g->parts[i].mesh);
    if (g->textures.count) glDeleteTextures(g->textures.count, g->textures.data);
    g->parts.free_mem();
    g->textures.free_mem();
}
