// FIGHTO - renderer implementation.
#include "renderer_internal.h"
#include "texgen.h"
#include "../platform/platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_ASSERT(x) ((void)0)
#include "../../third_party/stb/stb_image_write.h"

// Light grid: covers the whole city plus the underground areas.
#define LG_ORIGIN_X -720.0f
#define LG_ORIGIN_Z -720.0f
#define LG_CELL 16.0f
#define LG_CELLS_X 90
#define LG_CELLS_Z 90
#define LG_PER_CELL 12
#define MAX_LIGHTS 1024
#define MAX_STATIC_LIGHTS 768
#define MAX_CHUNKS 1024
#define MAX_DRAWS 1024
// Global light intensity scale: light colours are authored in convenient units.
#define LIGHT_SCALE 10.0f

struct Chunk {
    Mesh mesh;
    vec3 center;
    float radius;
    bool visible;
};

struct R {
    RenderSettings settings;
    bool mobile;
    bool floatTargets;
    float outScale;
    int texSize;
    // Shaders
    Shader world, worldSkinned, shadow, shadowSkinned, sky, particle, decal, beam, rain;
    Shader down, up, composite;
    // Textures
    u32 albedoArr, normalArr;
    u32 shadowTex, shadowFbo;
    int shadowSize;
    u32 lightGridTex, lightDataTex;
    // Render targets
    int fbW, fbH, winW, winH;
    u32 hdrFbo, hdrColor, hdrDepth;
    u32 bloomFbo[6], bloomTex[6];
    int bloomW[6], bloomH[6];
    int bloomLevels;
    // Geometry
    u32 fsVao, fsVbo;
    u32 fxVao, fxVbo, fxIbo;
    u32 rainVao, rainVbo, rainIbo;
    int rainCount;
    int rainIndexCount;
    // Lights
    Light staticLights[MAX_STATIC_LIGHTS];
    int staticLightCount;
    Light dynLights[MAX_LIGHTS];
    int dynLightCount;
    float lightData[3 * MAX_LIGHTS * 4];
    u16 grid[LG_CELLS_Z][LG_CELLS_X * 3 * 4];
    u8 gridCount[LG_CELLS_Z][LG_CELLS_X];
    // Chunks / draws
    Chunk chunks[MAX_CHUNKS];
    int chunkCount;
    DrawItem draws[MAX_DRAWS];
    int drawCount;
    Array<BeamDraw> beams;
    Array<BeamDraw> staticBeams;
    bool staticBeamsOn;
    Array<FxVertex> fxVerts;
    Array<u32> fxInds;
    // Frame
    CameraState cam;
    Atmosphere atm;
    mat4 view, proj, viewProj, invViewProj, shadowMat, shadowVP;
    Frustum frustum;
    vec3 shadowCenter;
    float shadowRadius;
    float time, dt;
    int drawCalls, tris;
};
static R* r;

const float* g_layerTilePtr = g_layerTile;

RenderSettings& renderer_settings() { return r->settings; }

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
static u32 compile_stage(GLenum type, const char* src, const char* file) {
    u32 s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), 0, log);
        log_error("Shader compile error in %s (%s):\n%s", file, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static void append_str(Array<char>* out, const char* s) {
    int n = (int)strlen(s);
    out->reserve(out->count + n + 1);
    memcpy(out->data + out->count, s, (size_t)n);
    out->count += n;
}

bool shader_load(Shader* sh, const char* file, const char* defines) {
    memset(sh, 0, sizeof(*sh));
    Array<u8> common = {}, body = {};
    char path[256];
    fmt_str(path, sizeof(path), "shaders/%s", file);
    if (!plat_read_asset("shaders/common.glsl", &common) || !plat_read_asset(path, &body)) {
        log_error("Missing shader file %s", path);
        common.free_mem();
        body.free_mem();
        return false;
    }
    u32 stages[2] = {0, 0};
    for (int st = 0; st < 2; st++) {
        Array<char> src = {};
        if (gl_is_es()) {
            append_str(&src, "#version 300 es\nprecision highp float;\nprecision highp int;\n");
        } else {
            append_str(&src, "#version 330 core\n");
        }
        append_str(&src, st == 0 ? "#define VERTEX 1\n" : "#define FRAGMENT 1\n");
        if (r->mobile) append_str(&src, "#define LOW_QUALITY 1\n");
        if (defines) append_str(&src, defines);
        append_str(&src, "\n");
        append_str(&src, (const char*)common.data);
        append_str(&src, "\n");
        append_str(&src, (const char*)body.data);
        src.push(0);
        stages[st] = compile_stage(st == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, src.data, file);
        src.free_mem();
    }
    common.free_mem();
    body.free_mem();
    if (!stages[0] || !stages[1]) return false;
    sh->prog = glCreateProgram();
    glAttachShader(sh->prog, stages[0]);
    glAttachShader(sh->prog, stages[1]);
    glLinkProgram(sh->prog);
    glDeleteShader(stages[0]);
    glDeleteShader(stages[1]);
    GLint ok = 0;
    glGetProgramiv(sh->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(sh->prog, sizeof(log), 0, log);
        log_error("Shader link error in %s [%s]:\n%s", file, defines ? defines : "", log);
        return false;
    }
    return true;
}

int shader_loc(Shader* s, const char* name) {
    u32 h = hash_str(name);
    for (int i = 0; i < s->count; i++)
        if (s->nameHash[i] == h) return s->loc[i];
    int loc = glGetUniformLocation(s->prog, name);
    if (s->count < MAX_UNIFORMS) {
        s->nameHash[s->count] = h;
        s->loc[s->count] = loc;
        s->count++;
    }
    return loc;
}

static Shader* g_bound;
void shader_use(Shader* s) {
    if (g_bound != s) {
        glUseProgram(s->prog);
        g_bound = s;
    }
}
void set_int(Shader* s, const char* n, int v) { int l = shader_loc(s, n); if (l >= 0) glUniform1i(l, v); }
void set_float(Shader* s, const char* n, float v) { int l = shader_loc(s, n); if (l >= 0) glUniform1f(l, v); }
void set_vec2(Shader* s, const char* n, vec2 v) { int l = shader_loc(s, n); if (l >= 0) glUniform2f(l, v.x, v.y); }
void set_vec3(Shader* s, const char* n, vec3 v) { int l = shader_loc(s, n); if (l >= 0) glUniform3f(l, v.x, v.y, v.z); }
void set_vec4(Shader* s, const char* n, vec4 v) { int l = shader_loc(s, n); if (l >= 0) glUniform4f(l, v.x, v.y, v.z, v.w); }
void set_mat4(Shader* s, const char* n, const mat4& m) { int l = shader_loc(s, n); if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m.m); }

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------
static void create_material_textures() {
    int S = r->texSize;
    size_t layerBytes = (size_t)S * S * 4;
    u8* alb = (u8*)malloc(layerBytes * MAT_COUNT);
    u8* nrm = (u8*)malloc(layerBytes * MAT_COUNT);
    double t0 = plat_time_seconds();
    for (int l = 0; l < MAT_COUNT; l++) texgen_layer(l, S, alb + layerBytes * l, nrm + layerBytes * l);
    log_info("Generated %d procedural materials (%dx%d) in %.2fs", MAT_COUNT, S, S, plat_time_seconds() - t0);
    u32 tex[2];
    glGenTextures(2, tex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, S, S, MAT_COUNT, 0, GL_RGBA, GL_UNSIGNED_BYTE, i == 0 ? alb : nrm);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (!r->mobile) glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    glGetError(); // anisotropy may be unsupported; ignore that error
    r->albedoArr = tex[0];
    r->normalArr = tex[1];
    free(alb);
    free(nrm);
}

static void create_shadow_map(int size) {
    if (r->shadowTex) { glDeleteTextures(1, &r->shadowTex); glDeleteFramebuffers(1, &r->shadowFbo); }
    r->shadowSize = size;
    glGenTextures(1, &r->shadowTex);
    glBindTexture(GL_TEXTURE_2D, r->shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &r->shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->shadowTex, 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) log_error("Shadow framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static u32 make_color_tex(int w, int h) {
    u32 t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    if (r->floatTargets) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, 0);
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void destroy_targets() {
    if (r->hdrFbo) {
        glDeleteFramebuffers(1, &r->hdrFbo);
        glDeleteTextures(1, &r->hdrColor);
        glDeleteRenderbuffers(1, &r->hdrDepth);
        r->hdrFbo = 0;
    }
    for (int i = 0; i < r->bloomLevels; i++) {
        glDeleteFramebuffers(1, &r->bloomFbo[i]);
        glDeleteTextures(1, &r->bloomTex[i]);
    }
    r->bloomLevels = 0;
}

static void create_targets(int winW, int winH) {
    destroy_targets();
    r->winW = winW;
    r->winH = winH;
    float sc = clampf(r->settings.renderScale, 0.4f, 1.0f);
    r->fbW = max_t(16, (int)(winW * sc));
    r->fbH = max_t(16, (int)(winH * sc));
    r->hdrColor = make_color_tex(r->fbW, r->fbH);
    glGenRenderbuffers(1, &r->hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, r->hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, r->fbW, r->fbH);
    glGenFramebuffers(1, &r->hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->hdrColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r->hdrDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) log_error("HDR framebuffer incomplete");
    int w = r->fbW, h = r->fbH;
    int levels = r->mobile ? 4 : 6;
    for (int i = 0; i < levels; i++) {
        w = max_t(1, w / 2);
        h = max_t(1, h / 2);
        r->bloomW[i] = w;
        r->bloomH[i] = h;
        r->bloomTex[i] = make_color_tex(w, h);
        glGenFramebuffers(1, &r->bloomFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, r->bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->bloomTex[i], 0);
        r->bloomLevels++;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void create_light_textures() {
    glGenTextures(1, &r->lightDataTex);
    glBindTexture(GL_TEXTURE_2D, r->lightDataTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, MAX_LIGHTS, 3, 0, GL_RGBA, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, &r->lightGridTex);
    glBindTexture(GL_TEXTURE_2D, r->lightGridTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16UI, LG_CELLS_X * 3, LG_CELLS_Z, 0, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void create_rain(int count) {
    if (r->rainVao) {
        glDeleteVertexArrays(1, &r->rainVao);
        glDeleteBuffers(1, &r->rainVbo);
        glDeleteBuffers(1, &r->rainIbo);
        r->rainVao = 0;
    }
    r->rainCount = count;
    if (count <= 0) return;
    Array<FxVertex> v = {};
    Array<u32> idx = {};
    Rng rng;
    rng.seed(1234);
    for (int i = 0; i < count; i++) {
        vec3 p(rng.f01(), rng.f01(), rng.f01());
        u32 b = (u32)v.count;
        for (int k = 0; k < 4; k++) {
            FxVertex fv;
            memset(&fv, 0, sizeof(fv));
            fv.pos = p;
            fv.uv = vec2((k & 2) ? 1.0f : 0.0f, (k & 1) ? 1.0f : -1.0f);
            fv.col[0] = fv.col[1] = fv.col[2] = fv.col[3] = 255;
            v.push(fv);
        }
        idx.push(b); idx.push(b + 1); idx.push(b + 3);
        idx.push(b); idx.push(b + 3); idx.push(b + 2);
    }
    glGenVertexArrays(1, &r->rainVao);
    glGenBuffers(1, &r->rainVbo);
    glGenBuffers(1, &r->rainIbo);
    glBindVertexArray(r->rainVao);
    glBindBuffer(GL_ARRAY_BUFFER, r->rainVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(FxVertex) * (size_t)v.count), v.data, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->rainIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)idx.count), idx.data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FxVertex), (void*)offsetof(FxVertex, col));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, params));
    glBindVertexArray(0);
    r->rainIndexCount = idx.count;
    v.free_mem();
    idx.free_mem();
}

static void create_geometry() {
    // Fullscreen triangle
    float tri[] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
    glGenVertexArrays(1, &r->fsVao);
    glGenBuffers(1, &r->fsVbo);
    glBindVertexArray(r->fsVao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0);
    // Dynamic effects buffer
    glGenVertexArrays(1, &r->fxVao);
    glGenBuffers(1, &r->fxVbo);
    glGenBuffers(1, &r->fxIbo);
    glBindVertexArray(r->fxVao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fxVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->fxIbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FxVertex), (void*)offsetof(FxVertex, col));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(FxVertex), (void*)offsetof(FxVertex, params));
    glBindVertexArray(0);
}

static bool load_shaders() {
    bool ok = true;
    ok &= shader_load(&r->world, "world.glsl", "");
    ok &= shader_load(&r->worldSkinned, "world.glsl", "#define SKINNED 1\n");
    ok &= shader_load(&r->shadow, "world.glsl", "#define SHADOW_PASS 1\n");
    ok &= shader_load(&r->shadowSkinned, "world.glsl", "#define SHADOW_PASS 1\n#define SKINNED 1\n");
    ok &= shader_load(&r->sky, "sky.glsl", "");
    ok &= shader_load(&r->particle, "fx.glsl", "#define PARTICLE 1\n");
    ok &= shader_load(&r->decal, "fx.glsl", "#define DECAL 1\n");
    ok &= shader_load(&r->beam, "fx.glsl", "#define BEAM 1\n");
    ok &= shader_load(&r->rain, "fx.glsl", "#define RAIN 1\n");
    ok &= shader_load(&r->down, "post.glsl", "#define DOWNSAMPLE 1\n");
    ok &= shader_load(&r->up, "post.glsl", "#define UPSAMPLE 1\n");
    ok &= shader_load(&r->composite, "post.glsl", "#define COMPOSITE 1\n");
    return ok;
}

void renderer_init(int quality) {
    r = (R*)calloc(1, sizeof(R));
    r->mobile = plat_is_mobile();
    r->settings.quality = quality;
    r->settings.renderScale = r->mobile ? 0.75f : 1.0f;
    r->settings.bloom = true;
    r->settings.shadows = true;
    r->settings.fxaa = !r->mobile;
    r->settings.drawDistance = r->mobile ? 380.0f : 900.0f;
    r->settings.rainDensity = r->mobile ? 1500 : 6000;
    r->settings.brightness = 1.0f;
    r->settings.fov = 62.0f;
    r->settings.filmGrain = true;
    r->settings.chromatic = !r->mobile;
    r->settings.vsync = true;
    r->staticBeamsOn = true;
    r->texSize = r->mobile ? 256 : 512;
    r->floatTargets = true;
    if (gl_is_es()) {
        r->floatTargets = gl_has_extension("GL_EXT_color_buffer_float") || gl_has_extension("GL_EXT_color_buffer_half_float");
    }
    r->outScale = r->floatTargets ? 1.0f : 0.25f;
    log_info("Renderer: %s targets, material size %d", r->floatTargets ? "HDR float" : "packed RGBA8", r->texSize);
    if (!load_shaders()) log_error("One or more shaders failed to compile");
    create_material_textures();
    create_shadow_map(r->mobile ? 1024 : (quality >= GQ_HIGH ? 4096 : 2048));
    create_light_textures();
    create_geometry();
    create_rain(r->settings.rainDensity);
}

void renderer_apply_settings(const RenderSettings& s) {
    bool rebuildTargets = s.renderScale != r->settings.renderScale;
    bool rebuildRain = s.rainDensity != r->settings.rainDensity;
    int oldQ = r->settings.quality;
    r->settings = s;
    if (rebuildTargets && r->winW > 0) create_targets(r->winW, r->winH);
    if (rebuildRain) create_rain(s.rainDensity);
    if (oldQ != s.quality && !r->mobile) create_shadow_map(s.quality >= GQ_HIGH ? 4096 : (s.quality == GQ_LOW ? 1024 : 2048));
    plat_set_vsync(s.vsync);
}

void renderer_shutdown() {
    if (!r) return;
    destroy_targets();
    for (int i = 0; i < r->chunkCount; i++) mesh_destroy(&r->chunks[i].mesh);
    r->beams.free_mem();
    r->staticBeams.free_mem();
    r->fxVerts.free_mem();
    r->fxInds.free_mem();
    free(r);
    r = 0;
}

// ---------------------------------------------------------------------------
// Lights & chunks
// ---------------------------------------------------------------------------
void renderer_clear_static_lights() { r->staticLightCount = 0; }
int renderer_add_static_light(const Light& l) {
    if (r->staticLightCount >= MAX_STATIC_LIGHTS) return -1;
    r->staticLights[r->staticLightCount] = l;
    return r->staticLightCount++;
}
void renderer_set_static_light(int idx, const Light& l) {
    if (idx >= 0 && idx < r->staticLightCount) r->staticLights[idx] = l;
}
void renderer_add_light(const Light& l) {
    if (r->dynLightCount < MAX_LIGHTS - MAX_STATIC_LIGHTS) r->dynLights[r->dynLightCount++] = l;
}
int renderer_add_chunk(const Mesh& m) {
    if (r->chunkCount >= MAX_CHUNKS) return -1;
    Chunk& c = r->chunks[r->chunkCount];
    c.mesh = m;
    c.center = (m.bmin + m.bmax) * 0.5f;
    c.radius = length(m.bmax - m.bmin) * 0.5f;
    c.visible = true;
    return r->chunkCount++;
}
void renderer_clear_chunks() {
    for (int i = 0; i < r->chunkCount; i++) mesh_destroy(&r->chunks[i].mesh);
    r->chunkCount = 0;
}
void renderer_set_chunk_visible(int idx, bool v) { if (idx >= 0 && idx < r->chunkCount) r->chunks[idx].visible = v; }
void renderer_add_static_beam(const BeamDraw& b) { r->staticBeams.push(b); }
void renderer_clear_static_beams() { r->staticBeams.clear(); }
void renderer_set_static_beams_enabled(bool on) { r->staticBeamsOn = on; }
void renderer_beam(const BeamDraw& b) { r->beams.push(b); }

static void grid_insert(int lightIndex, const Light& l) {
    float rad = l.radius;
    int x0 = (int)floorf((l.pos.x - rad - LG_ORIGIN_X) / LG_CELL);
    int x1 = (int)floorf((l.pos.x + rad - LG_ORIGIN_X) / LG_CELL);
    int z0 = (int)floorf((l.pos.z - rad - LG_ORIGIN_Z) / LG_CELL);
    int z1 = (int)floorf((l.pos.z + rad - LG_ORIGIN_Z) / LG_CELL);
    x0 = max_t(x0, 0); z0 = max_t(z0, 0);
    x1 = min_t(x1, LG_CELLS_X - 1); z1 = min_t(z1, LG_CELLS_Z - 1);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            // Circle vs cell rectangle test
            float cx = LG_ORIGIN_X + (x + 0.5f) * LG_CELL, cz = LG_ORIGIN_Z + (z + 0.5f) * LG_CELL;
            float dx = fmaxf(fabsf(l.pos.x - cx) - LG_CELL * 0.5f, 0.0f);
            float dz = fmaxf(fabsf(l.pos.z - cz) - LG_CELL * 0.5f, 0.0f);
            if (dx * dx + dz * dz > rad * rad) continue;
            u8& n = r->gridCount[z][x];
            if (n >= LG_PER_CELL) continue;
            r->grid[z][x * LG_PER_CELL + n] = (u16)lightIndex;
            n++;
        }
}

static void upload_lights() {
    for (int z = 0; z < LG_CELLS_Z; z++)
        for (int i = 0; i < LG_CELLS_X * LG_PER_CELL; i++) r->grid[z][i] = 0xFFFF;
    memset(r->gridCount, 0, sizeof(r->gridCount));
    int idx = 0;
    vec3 cam = r->cam.pos;
    // Dynamic lights first so gameplay lights are never dropped
    for (int i = 0; i < r->dynLightCount && idx < MAX_LIGHTS; i++) {
        const Light& l = r->dynLights[i];
        if (dist_xz(l.pos, cam) > r->settings.drawDistance * 0.6f + l.radius) continue;
        float* d0 = &r->lightData[(0 * MAX_LIGHTS + idx) * 4];
        float* d1 = &r->lightData[(1 * MAX_LIGHTS + idx) * 4];
        float* d2 = &r->lightData[(2 * MAX_LIGHTS + idx) * 4];
        d0[0] = l.pos.x; d0[1] = l.pos.y; d0[2] = l.pos.z; d0[3] = l.radius;
        d1[0] = l.color.x * LIGHT_SCALE; d1[1] = l.color.y * LIGHT_SCALE; d1[2] = l.color.z * LIGHT_SCALE; d1[3] = 0;
        d2[0] = l.spotDir.x; d2[1] = l.spotDir.y; d2[2] = l.spotDir.z; d2[3] = l.spotCos;
        grid_insert(idx, l);
        idx++;
    }
    for (int i = 0; i < r->staticLightCount && idx < MAX_LIGHTS; i++) {
        const Light& l = r->staticLights[i];
        if (l.radius <= 0.0f) continue;
        float* d0 = &r->lightData[(0 * MAX_LIGHTS + idx) * 4];
        float* d1 = &r->lightData[(1 * MAX_LIGHTS + idx) * 4];
        float* d2 = &r->lightData[(2 * MAX_LIGHTS + idx) * 4];
        d0[0] = l.pos.x; d0[1] = l.pos.y; d0[2] = l.pos.z; d0[3] = l.radius;
        d1[0] = l.color.x * LIGHT_SCALE; d1[1] = l.color.y * LIGHT_SCALE; d1[2] = l.color.z * LIGHT_SCALE; d1[3] = 0;
        d2[0] = l.spotDir.x; d2[1] = l.spotDir.y; d2[2] = l.spotDir.z; d2[3] = l.spotCos;
        grid_insert(idx, l);
        idx++;
    }
    glBindTexture(GL_TEXTURE_2D, r->lightDataTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int row = 0; row < 3; row++)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, max_t(idx, 1), 1, GL_RGBA, GL_FLOAT, &r->lightData[row * MAX_LIGHTS * 4]);
    glBindTexture(GL_TEXTURE_2D, r->lightGridTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LG_CELLS_X * 3, LG_CELLS_Z, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, r->grid);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void renderer_begin_frame(int width, int height, const CameraState& cam, const Atmosphere& atm, float time, float dt) {
    if (width != r->winW || height != r->winH || !r->hdrFbo) create_targets(width, height);
    r->cam = cam;
    r->atm = atm;
    r->time = time;
    r->dt = dt;
    r->drawCount = 0;
    r->dynLightCount = 0;
    r->beams.clear();
    r->drawCalls = 0;
    r->tris = 0;
    float aspect = (float)r->fbW / (float)r->fbH;
    r->view = mat4_look_at(cam.pos, cam.target, cam.up);
    r->proj = mat4_perspective(cam.fov * DEG2RAD, aspect, cam.zNear, cam.zFar);
    r->viewProj = r->proj * r->view;
    r->invViewProj = mat4_inverse(r->viewProj);
    r->frustum = frustum_from_matrix(r->viewProj);

    // Shadow frustum centred ahead of the camera
    vec3 fwd = normalize(cam.target - cam.pos);
    float rad = r->mobile ? 45.0f : (r->settings.quality >= GQ_HIGH ? 75.0f : 60.0f);
    vec3 center = cam.pos + flat(fwd) * (rad * 0.55f);
    center.y = cam.pos.y < -10.0f ? cam.pos.y : 0.0f;
    vec3 ld = normalize(atm.sunDir);
    mat4 lview = mat4_look_at(center + ld * 250.0f, center, fabsf(ld.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0));
    // Snap to texels to avoid shimmering
    float texel = 2.0f * rad / r->shadowSize;
    vec3 lc = transform_point(lview, center);
    float sx = floorf(lc.x / texel) * texel - lc.x, sy = floorf(lc.y / texel) * texel - lc.y;
    mat4 snap = mat4_translate(vec3(sx, sy, 0));
    mat4 lproj = mat4_ortho(-rad, rad, -rad, rad, 1.0f, 600.0f);
    r->shadowVP = lproj * snap * lview;
    mat4 bias = mat4_identity();
    bias.m[0] = 0.5f; bias.m[5] = 0.5f; bias.m[10] = 0.5f;
    bias.m[12] = 0.5f; bias.m[13] = 0.5f; bias.m[14] = 0.5f;
    r->shadowMat = bias * r->shadowVP;
    r->shadowCenter = center;
    r->shadowRadius = rad;
}

void renderer_submit(const DrawItem& d) {
    if (r->drawCount >= MAX_DRAWS || !d.mesh || d.mesh->indexCount == 0) return;
    r->draws[r->drawCount++] = d;
}

const mat4& renderer_view_proj() { return r->viewProj; }
const Frustum& renderer_frustum() { return r->frustum; }
vec3 renderer_cam_pos() { return r->cam.pos; }
int renderer_draw_calls() { return r->drawCalls; }
int renderer_triangles() { return r->tris; }

bool renderer_project(vec3 w, vec2* out) {
    vec4 c = r->viewProj * vec4(w, 1.0f);
    if (c.w <= 0.01f) return false;
    float x = c.x / c.w, y = c.y / c.w;
    out->x = (x * 0.5f + 0.5f) * r->winW;
    out->y = (1.0f - (y * 0.5f + 0.5f)) * r->winH;
    return x > -1.2f && x < 1.2f && y > -1.2f && y < 1.2f;
}

static void set_frame_uniforms(Shader* s) {
    shader_use(s);
    const Atmosphere& a = r->atm;
    set_mat4(s, "uViewProj", r->viewProj);
    set_vec3(s, "uCamPos", r->cam.pos);
    set_float(s, "uTime", r->time);
    set_vec3(s, "uSunDir", normalize(a.sunDir));
    set_vec3(s, "uSunColor", a.sunColor);
    set_vec3(s, "uSkyTop", a.skyTop);
    set_vec3(s, "uSkyHorizon", a.skyHorizon);
    set_vec3(s, "uAmbientUp", a.ambientUp);
    set_vec3(s, "uAmbientDown", a.ambientDown);
    set_vec3(s, "uFogColor", a.fogColor);
    set_vec4(s, "uFogParams", vec4(a.fogDensity, a.fogHeightFalloff, a.fogBase, a.fogMax));
    set_float(s, "uRain", a.rain);
    set_float(s, "uFlash", a.flash);
    set_float(s, "uOutScale", r->outScale);
    set_vec4(s, "uLightGridParams", vec4(LG_ORIGIN_X, LG_ORIGIN_Z, 1.0f / LG_CELL, (float)LG_CELLS_X));
    set_vec4(s, "uLightGridParams2", vec4((float)LG_CELLS_Z, 1.0f / r->shadowSize, r->settings.shadows ? 0.92f : 0.0f, 0));
    set_int(s, "uAlbedoArr", 0);
    set_int(s, "uNormalArr", 1);
    set_int(s, "uShadowMap", 2);
    set_int(s, "uLightGrid", 3);
    set_int(s, "uLightData", 4);
}

static void set_world_uniforms(Shader* s) {
    set_frame_uniforms(s);
    set_mat4(s, "uShadowMat", r->shadowMat);
    int l = shader_loc(s, "uLayerTile");
    if (l >= 0) glUniform1fv(l, MAT_COUNT, g_layerTile);
    set_float(s, "uWindowsOn", r->atm.windowsOn);
    set_float(s, "uEmissiveScale", r->atm.emissiveScale);
    set_float(s, "uAlienPulse", r->atm.alienPulse);
    set_float(s, "uWind", r->atm.wind);
    set_vec4(s, "uTint", vec4(1, 1, 1, 1));
    set_float(s, "uBlood", 0);
    set_float(s, "uHitFlash", 0);
}

static void draw_mesh(const Mesh* m) {
    glBindVertexArray(m->vao);
    glDrawElements(GL_TRIANGLES, m->indexCount, GL_UNSIGNED_INT, 0);
    r->drawCalls++;
    r->tris += m->indexCount / 3;
}

static bool chunk_in_shadow(const Chunk& c) {
    float d = dist_xz(c.center, r->shadowCenter);
    return d < r->shadowRadius * 1.5f + c.radius && fabsf(c.center.y - r->shadowCenter.y) < 120.0f + c.radius;
}

static void draw_items(bool shadowPass) {
    mat4 ident = mat4_identity();
    // Static (non skinned) items
    Shader* s = shadowPass ? &r->shadow : &r->world;
    shader_use(s);
    for (int i = 0; i < r->drawCount; i++) {
        const DrawItem& d = r->draws[i];
        if (d.bones) continue;
        if (shadowPass && !d.castShadow) continue;
        vec3 c = transform_point(d.model, (d.mesh->bmin + d.mesh->bmax) * 0.5f);
        float rad = length(d.mesh->bmax - d.mesh->bmin) * 0.5f;
        if (shadowPass) { if (dist_xz(c, r->shadowCenter) > r->shadowRadius * 1.5f + rad) continue; }
        else if (!frustum_sphere(r->frustum, c, rad)) continue;
        set_mat4(s, "uModel", d.model);
        if (!shadowPass) {
            set_vec4(s, "uTint", d.tint);
            set_float(s, "uBlood", d.blood);
            set_vec3(s, "uBloodColor", d.bloodColor);
            set_float(s, "uHitFlash", d.hitFlash);
        }
        draw_mesh(d.mesh);
    }
    // Skinned characters
    s = shadowPass ? &r->shadowSkinned : &r->worldSkinned;
    shader_use(s);
    for (int i = 0; i < r->drawCount; i++) {
        const DrawItem& d = r->draws[i];
        if (!d.bones) continue;
        if (shadowPass && !d.castShadow) continue;
        vec3 c(d.model.m[12], d.model.m[13] + 1.0f, d.model.m[14]);
        if (shadowPass) { if (dist_xz(c, r->shadowCenter) > r->shadowRadius * 1.5f + 2.0f) continue; }
        else if (!frustum_sphere(r->frustum, c, 2.5f)) continue;
        set_mat4(s, "uModel", d.model);
        int l = shader_loc(s, "uBones");
        if (l >= 0) glUniformMatrix4fv(l, min_t(d.boneCount, 24), GL_FALSE, d.bones[0].m);
        if (!shadowPass) {
            set_vec4(s, "uTint", d.tint);
            set_float(s, "uBlood", d.blood);
            set_vec3(s, "uBloodColor", d.bloodColor);
            set_float(s, "uHitFlash", d.hitFlash);
        }
        draw_mesh(d.mesh);
    }
    FIGHTO_UNUSED(ident);
}

static void render_shadows() {
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadowFbo);
    glViewport(0, 0, r->shadowSize, r->shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (!r->settings.shadows) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glDisable(GL_CULL_FACE);
    Shader* s = &r->shadow;
    shader_use(s);
    // The shadow pass reuses uViewProj with the light matrix
    set_mat4(s, "uViewProj", r->shadowVP);
    set_mat4(s, "uModel", mat4_identity());
    set_float(s, "uTime", r->time);
    set_float(s, "uWind", r->atm.wind);
    for (int i = 0; i < r->chunkCount; i++) {
        const Chunk& c = r->chunks[i];
        if (!c.visible || !chunk_in_shadow(c)) continue;
        draw_mesh(&c.mesh);
    }
    shader_use(&r->shadowSkinned);
    set_mat4(&r->shadowSkinned, "uViewProj", r->shadowVP);
    set_float(&r->shadowSkinned, "uTime", r->time);
    shader_use(s);
    draw_items(true);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

static void bind_world_textures() {
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, r->albedoArr);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, r->normalArr);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, r->shadowTex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, r->lightGridTex);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, r->lightDataTex);
    glActiveTexture(GL_TEXTURE0);
}

static void upload_fx_and_draw(GLenum mode) {
    if (r->fxInds.count == 0) return;
    glBindVertexArray(r->fxVao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fxVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(FxVertex) * (size_t)r->fxVerts.count), r->fxVerts.data, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->fxIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)r->fxInds.count), r->fxInds.data, GL_STREAM_DRAW);
    glDrawElements(mode, r->fxInds.count, GL_UNSIGNED_INT, 0);
    r->drawCalls++;
}

static void build_beam(const BeamDraw& b, vec3 camPos) {
    vec3 axis = b.b - b.a;
    float len = length(axis);
    if (len < 0.01f) return;
    vec3 dir = axis / len;
    vec3 mid = (b.a + b.b) * 0.5f;
    vec3 toCam = normalize(camPos - mid);
    vec3 side = normalize(cross(dir, toCam));
    if (length_sq(side) < 1e-6f) return;
    u32 base = (u32)r->fxVerts.count;
    vec3 pts[4] = {b.a - side * b.r0, b.a + side * b.r0, b.b + side * b.r1, b.b - side * b.r1};
    vec2 uvs[4] = {vec2(-1, 0), vec2(1, 0), vec2(1, 1), vec2(-1, 1)};
    for (int i = 0; i < 4; i++) {
        FxVertex v;
        v.pos = pts[i];
        v.uv = uvs[i];
        v.col[0] = (u8)(saturate(b.color.x) * 255);
        v.col[1] = (u8)(saturate(b.color.y) * 255);
        v.col[2] = (u8)(saturate(b.color.z) * 255);
        v.col[3] = (u8)(saturate(b.alpha) * 255);
        v.params[0] = 0; v.params[1] = b.intensity; v.params[2] = b.a.x * 0.37f + b.a.z * 0.11f; v.params[3] = 0;
        r->fxVerts.push(v);
    }
    r->fxInds.push(base); r->fxInds.push(base + 1); r->fxInds.push(base + 2);
    r->fxInds.push(base); r->fxInds.push(base + 2); r->fxInds.push(base + 3);
}

static void render_scene() {
    glBindFramebuffer(GL_FRAMEBUFFER, r->hdrFbo);
    glViewport(0, 0, r->fbW, r->fbH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    bind_world_textures();

    // Opaque world chunks
    set_world_uniforms(&r->worldSkinned);
    set_world_uniforms(&r->world);
    set_mat4(&r->world, "uModel", mat4_identity());
    float maxD = r->settings.drawDistance;
    glDisable(GL_CULL_FACE); // procedural geometry is not consistently wound; keep both faces
    for (int i = 0; i < r->chunkCount; i++) {
        const Chunk& c = r->chunks[i];
        if (!c.visible) continue;
        if (length(c.center - r->cam.pos) - c.radius > maxD) continue;
        if (!frustum_sphere(r->frustum, c.center, c.radius)) continue;
        draw_mesh(&c.mesh);
    }
    draw_items(false);

    // Sky
    Shader* sk = &r->sky;
    set_frame_uniforms(sk);
    set_mat4(sk, "uInvViewProj", r->invViewProj);
    set_float(sk, "uInvasion", r->atm.invasion);
    set_float(sk, "uCloudCover", r->atm.cloudCover);
    glDepthMask(GL_FALSE);
    glBindVertexArray(r->fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    r->drawCalls++;

    // Transparent effects: premultiplied alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    vec3 fwd = normalize(r->cam.target - r->cam.pos);
    vec3 right = normalize(cross(fwd, r->cam.up));
    vec3 up = cross(right, fwd);

    // Decals
    r->fxVerts.clear();
    r->fxInds.clear();
    fx_build_decals(&r->fxVerts, &r->fxInds, r->cam.pos, fminf(maxD, 140.0f), r->frustum);
    if (r->fxInds.count) {
        set_frame_uniforms(&r->decal);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        upload_fx_and_draw(GL_TRIANGLES);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Volumetric beams (additive through premultiplied alpha = 0)
    r->fxVerts.clear();
    r->fxInds.clear();
    if (r->staticBeamsOn) {
        for (int i = 0; i < r->staticBeams.count; i++) {
            const BeamDraw& b = r->staticBeams[i];
            if (length(b.a - r->cam.pos) > 160.0f) continue;
            if (!frustum_sphere(r->frustum, (b.a + b.b) * 0.5f, length(b.b - b.a) * 0.5f + b.r1)) continue;
            build_beam(b, r->cam.pos);
        }
    }
    for (int i = 0; i < r->beams.count; i++) build_beam(r->beams[i], r->cam.pos);
    if (r->fxInds.count) {
        set_frame_uniforms(&r->beam);
        upload_fx_and_draw(GL_TRIANGLES);
    }

    // Particles
    r->fxVerts.clear();
    r->fxInds.clear();
    fx_build_particles(&r->fxVerts, &r->fxInds, r->cam.pos, right, up, r->frustum);
    if (r->fxInds.count) {
        set_frame_uniforms(&r->particle);
        upload_fx_and_draw(GL_TRIANGLES);
    }

    // Rain
    if (r->atm.rain > 0.02f && r->rainVao && r->cam.pos.y > -20.0f) {
        Shader* rs = &r->rain;
        set_frame_uniforms(rs);
        set_vec3(rs, "uRainBox", vec3(36, 22, 36));
        set_vec3(rs, "uRainVel", vec3(1.2f + r->atm.wind * 3.0f, -16.0f, 0.6f));
        set_float(rs, "uRainLen", 0.55f);
        glBindVertexArray(r->rainVao);
        int count = (int)(r->rainIndexCount * saturate(r->atm.rain));
        count -= count % 6;
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
        r->drawCalls++;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static void fullscreen() {
    glBindVertexArray(r->fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    r->drawCalls++;
}

static void render_post(const PostState& p) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    float decode = 1.0f / r->outScale;
    bool bloom = r->settings.bloom && r->bloomLevels > 0;
    if (bloom) {
        Shader* d = &r->down;
        shader_use(d);
        set_int(d, "uTex", 0);
        glActiveTexture(GL_TEXTURE0);
        u32 src = r->hdrColor;
        int sw = r->fbW, sh = r->fbH;
        for (int i = 0; i < r->bloomLevels; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, r->bloomFbo[i]);
            glViewport(0, 0, r->bloomW[i], r->bloomH[i]);
            glBindTexture(GL_TEXTURE_2D, src);
            set_vec2(d, "uTexel", vec2(1.0f / sw, 1.0f / sh));
            set_float(d, "uThreshold", i == 0 ? 1.1f : 0.0f);
            set_float(d, "uDecode", i == 0 ? decode : 1.0f);
            fullscreen();
            src = r->bloomTex[i];
            sw = r->bloomW[i];
            sh = r->bloomH[i];
        }
        Shader* u = &r->up;
        shader_use(u);
        set_int(u, "uTex", 0);
        set_float(u, "uRadius", 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = r->bloomLevels - 1; i > 0; i--) {
            glBindFramebuffer(GL_FRAMEBUFFER, r->bloomFbo[i - 1]);
            glViewport(0, 0, r->bloomW[i - 1], r->bloomH[i - 1]);
            glBindTexture(GL_TEXTURE_2D, r->bloomTex[i]);
            set_vec2(u, "uTexel", vec2(1.0f / r->bloomW[i], 1.0f / r->bloomH[i]));
            fullscreen();
        }
        glDisable(GL_BLEND);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, r->winW, r->winH);
    Shader* c = &r->composite;
    shader_use(c);
    set_float(c, "uTime", r->time);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->hdrColor);
    set_int(c, "uTex", 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, bloom ? r->bloomTex[0] : r->hdrColor);
    set_int(c, "uBloom", 1);
    glActiveTexture(GL_TEXTURE0);
    set_vec2(c, "uTexel", vec2(1.0f / r->fbW, 1.0f / r->fbH));
    set_float(c, "uDecode", decode);
    set_float(c, "uExposure", r->atm.exposure);
    set_float(c, "uBloomStrength", bloom ? 0.08f : 0.0f);
    set_float(c, "uVignette", 0.55f);
    set_float(c, "uGrain", r->settings.filmGrain ? 0.035f : 0.0f);
    set_float(c, "uDamage", p.damage);
    set_float(c, "uLowHealth", p.lowHealth);
    set_float(c, "uBloodScreen", p.bloodScreen);
    set_float(c, "uBloodSeed", p.bloodSeed);
    set_float(c, "uLetterbox", p.letterbox);
    set_float(c, "uFade", p.fade);
    set_float(c, "uWhiteFlash", p.whiteFlash);
    set_float(c, "uSaturation", p.saturation > 0 ? p.saturation : 0.92f);
    set_float(c, "uBrightness", r->settings.brightness);
    set_float(c, "uChroma", r->settings.chromatic ? 1.0f : 0.0f);
    set_float(c, "uAlienTint", p.alienTint);
    set_float(c, "uFxaa", r->settings.fxaa ? 1.0f : 0.0f);
    fullscreen();
}

void renderer_end_frame(const PostState& post) {
    upload_lights();
    render_shadows();
    render_scene();
    render_post(post);
    glBindVertexArray(0);
    g_bound = 0;
    glUseProgram(0);
}

static void png_write_cb(void* ctx, void* data, int size) {
    Array<u8>* out = (Array<u8>*)ctx;
    out->reserve(out->count + size);
    memcpy(out->data + out->count, data, (size_t)size);
    out->count += size;
}

bool renderer_screenshot(const char* name) {
    int w = r->winW, h = r->winH;
    u8* px = (u8*)malloc((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    // Flip vertically and drop alpha
    u8* rgb = (u8*)malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8* s = px + ((h - 1 - y) * w + x) * 4;
            u8* d = rgb + (y * w + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    Array<u8> png = {};
    stbi_write_png_to_func(png_write_cb, &png, w, h, 3, rgb, w * 3);
    bool ok = png.count > 0 && plat_write_user_file(name, png.data, (size_t)png.count);
    png.free_mem();
    free(px);
    free(rgb);
    return ok;
}
