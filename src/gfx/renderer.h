// FIGHTO - custom forward renderer with clustered (grid) lighting, moonlight
// shadows, HDR bloom, cinematic post-processing, particles and decals.
#pragma once
#include "../core/vmath.h"
#include "gl.h"

// ---------------------------------------------------------------------------
// Materials (texture array layers) and flags
// ---------------------------------------------------------------------------
enum MaterialLayer {
    MAT_ASPHALT, MAT_SIDEWALK, MAT_BRICK, MAT_CONCRETE, MAT_GLASS_FACADE, MAT_CORRUGATED,
    MAT_GRASS, MAT_DIRT, MAT_ROOF, MAT_WOOD, MAT_TILES, MAT_METAL_PANEL, MAT_ALIEN,
    MAT_FABRIC, MAT_SKIN, MAT_RUST, MAT_PAINT, MAT_PLASTER, MAT_CARPAINT, MAT_LEATHER,
    MAT_STEEL, MAT_RUBBER, MAT_HAIR, MAT_FLOOR_CONCRETE,
    MAT_COUNT
};

enum MaterialFlags {
    MF_WINDOWS = 1,      // procedural lit windows (facades)
    MF_EMISSIVE = 2,     // vertex colour emission (mat.w * 0.1 intensity)
    MF_METAL = 4,
    MF_GROUND = 8,       // receives puddles / wet road highlights
    MF_FOLIAGE = 16,     // wind sway
    MF_CHARACTER = 32,   // blood stains, soft wrap lighting
    MF_GLASS = 64,
    MF_PULSE = 128,      // pulsing emission (alien tech, neon flicker)
};

struct Vertex {
    vec3 pos;
    i8 nrm[4];
    vec2 uv;
    u8 col[4];
    u8 mat[4];      // layer, flags, roughness scale (128 = 1.0), emissive*10 / facade seed
    u8 bones[4];
    u8 weights[4];
};

struct Mesh {
    u32 vao, vbo, ibo;
    int indexCount;
    vec3 bmin, bmax;
};

struct MeshBuilder {
    Array<Vertex> verts;
    Array<u32> inds;
    u8 layer, flags, rough, emis;
    u8 col[4];
    u8 bone;
    u8 bone2;
    float boneBlend;   // weight of bone2 (0 = all bone)

    void reset();
    void free_mem();
    void set_mat(int layer, int flags = 0, float roughness = 1.0f, float emissive = 0.0f);
    void set_color(vec3 srgb, float alpha = 1.0f);
    void set_tintable();   // vertex colour alpha 0 -> per-draw tint colour
    void set_bone(int b) { bone = (u8)b; bone2 = (u8)b; boneBlend = 0; }
    u32 vert(vec3 p, vec3 n, vec2 uv);
    void tri(u32 a, u32 b, u32 c);
    // Quad a,b,c,d counter-clockwise when seen from the front.
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud);
    void quad_auto(vec3 a, vec3 b, vec3 c, vec3 d);       // metre-based UVs
    // Axis aligned box with metre UVs (u along face, v = height).
    void box(vec3 mn, vec3 mx, bool top = true, bool bottom = false, bool sides = true);
    // Oriented box from a transform (unit cube -0.5..0.5 scaled by size).
    void box_xf(const mat4& xf, vec3 size);
    void cylinder(vec3 base, float radius, float height, int segments, bool caps = true, float uvScale = 1.0f);
    void cone_frustum(vec3 base, float r0, float r1, float height, int segments, bool caps = true);
    void ellipsoid(vec3 center, vec3 radii, int seg, int rings);
    // Tapered tube between two points (limbs, pipes, tendrils).
    void tube(vec3 a, vec3 b, float ra, float rb, int segments, bool capA = true, bool capB = true);
    void transform(int firstVertex, const mat4& m);
    int vcount() const { return verts.count; }
    void append(const MeshBuilder& other, const mat4& m);
};

Mesh mesh_create(const MeshBuilder& mb, bool dynamic = false);
void mesh_update(Mesh* m, const MeshBuilder& mb);
void mesh_destroy(Mesh* m);

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
#define MAX_UNIFORMS 64
struct Shader {
    u32 prog;
    u32 nameHash[MAX_UNIFORMS];
    int loc[MAX_UNIFORMS];
    int count;
};
bool shader_load(Shader* s, const char* file, const char* defines);
int shader_loc(Shader* s, const char* name);
void shader_use(Shader* s);
void set_int(Shader* s, const char* n, int v);
void set_float(Shader* s, const char* n, float v);
void set_vec2(Shader* s, const char* n, vec2 v);
void set_vec3(Shader* s, const char* n, vec3 v);
void set_vec4(Shader* s, const char* n, vec4 v);
void set_mat4(Shader* s, const char* n, const mat4& m);

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------
struct Light {
    vec3 pos;
    float radius;
    vec3 color;       // linear colour * intensity
    vec3 spotDir;
    float spotCos;    // 0 = point light
};

// ---------------------------------------------------------------------------
// Atmosphere / environment
// ---------------------------------------------------------------------------
struct Atmosphere {
    vec3 sunDir;
    vec3 sunColor;
    vec3 skyTop, skyHorizon;
    vec3 ambientUp, ambientDown;
    vec3 fogColor;
    float fogDensity, fogHeightFalloff, fogBase, fogMax;
    float rain;          // wetness / rain intensity 0..1
    float flash;         // lightning
    float invasion;      // alien sky 0..1
    float windowsOn;     // building windows lit (0 during blackout)
    float emissiveScale;
    float exposure;
    float cloudCover;
    float wind;
    float alienPulse;
};

// ---------------------------------------------------------------------------
// Draw submission
// ---------------------------------------------------------------------------
struct DrawItem {
    const Mesh* mesh;
    mat4 model;
    vec4 tint;
    const mat4* bones;
    int boneCount;
    float blood;
    vec3 bloodColor;
    float hitFlash;
    bool castShadow;
    float sortDist;
    vec3 cullCenter;    // skinned items: world-space bounds centre (bones may be in world space)
    float cullRadius;   // > 0 enables cullCenter
};

enum ParticleType { PT_SMOKE = 0, PT_BLOOD = 1, PT_SPARK = 2, PT_FIRE = 3, PT_MUZZLE = 4, PT_STREAK = 5, PT_RING = 6, PT_ICHOR = 7 };
enum DecalType { DT_BLOOD_SPLAT = 0, DT_BLOOD_POOL = 1, DT_BULLET_HOLE = 2, DT_SCORCH = 3, DT_ICHOR = 4, DT_SMEAR = 5, DT_TYRE = 6 };

struct Particle {
    vec3 pos, vel;
    vec3 color;
    float alpha;
    float size, sizeGrow;
    float life, maxLife;
    float gravity, drag;
    float stretch;       // velocity stretch factor (0 = billboard)
    float emissive;
    u8 type;
    u8 spawnDecal;       // 1: leaves a blood splat on ground impact, 2: ichor
    float seed;
    float rot;
};

struct Decal {
    vec3 pos, normal, tangent;
    float size;
    float sizeX;         // aspect scale along tangent
    vec3 color;
    float alpha;
    float age, grow, growTarget, growRate;
    float seed;
    u8 type;
    bool active;
};

struct BeamDraw {
    vec3 a, b;
    float r0, r1;
    vec3 color;
    float intensity;
    float alpha;
};

enum GraphicsQuality { GQ_LOW, GQ_MEDIUM, GQ_HIGH, GQ_ULTRA };

struct RenderSettings {
    int quality;          // GraphicsQuality
    float renderScale;    // 0.5 .. 1.0
    bool bloom;
    bool shadows;
    bool fxaa;
    float drawDistance;
    int rainDensity;      // number of rain streaks
    float brightness;
    float fov;
    bool filmGrain;
    bool chromatic;
    bool vsync;
};

struct PostState {
    float damage, lowHealth, bloodScreen, bloodSeed, letterbox, fade, whiteFlash, alienTint;
    float saturation;
};

struct CameraState {
    vec3 pos, target, up;
    float fov;
    float zNear, zFar;
};

void renderer_init(int quality);
void renderer_shutdown();
void renderer_apply_settings(const RenderSettings& s);
RenderSettings& renderer_settings();

// Static lights (street lamps, neon) are registered once; dynamic ones per frame.
void renderer_clear_static_lights();
int renderer_add_static_light(const Light& l);
void renderer_set_static_light(int idx, const Light& l);
void renderer_add_light(const Light& l);
// Static world chunks
int renderer_add_chunk(const Mesh& m);
void renderer_clear_chunks();
void renderer_set_chunk_visible(int idx, bool v);
void renderer_add_static_beam(const BeamDraw& b);
void renderer_clear_static_beams();
void renderer_set_static_beams_enabled(bool on);

void renderer_begin_frame(int width, int height, const CameraState& cam, const Atmosphere& atm, float time, float dt);
void renderer_submit(const DrawItem& d);
void renderer_beam(const BeamDraw& b);
void renderer_end_frame(const PostState& post);     // renders everything into the back buffer
const mat4& renderer_view_proj();
const Frustum& renderer_frustum();
vec3 renderer_cam_pos();
bool renderer_project(vec3 world, vec2* screen);    // to pixels; false if behind
int renderer_draw_calls();
int renderer_triangles();
bool renderer_screenshot(const char* path);

// Particles & decals
Particle* fx_spawn(ParticleType type);
void fx_update(float dt, float groundHeightFn(vec3 p));
Decal* fx_decal(DecalType type, vec3 pos, vec3 normal, float size, vec3 color, float seed);
void fx_clear();
int fx_particle_count();
int fx_decal_count();
void fx_set_gore_level(int level);   // 0 off, 1 normal, 2 extreme
int fx_gore_level();

extern const float g_layerTile[MAT_COUNT];
