// FIGHTO - Blackridge: city layout, districts, landmarks, collision and navigation.
#pragma once
#include "../core/vmath.h"
#include "../gfx/renderer.h"

#define CITY_BLOCKS 9
#define BLOCK_PITCH 104.0f
#define ROAD_W 14.0f
#define SIDEWALK_W 4.0f
#define CURB_H 0.15f
#define CITY_HALF (CITY_BLOCKS * BLOCK_PITCH * 0.5f)
#define WORLD_LIMIT 520.0f

// Underground areas (outside the surface city footprint)
#define AEGIS_ORIGIN_X 600.0f
#define AEGIS_ORIGIN_Y -80.0f
#define AEGIS_ORIGIN_Z -600.0f
#define DRAIN_ORIGIN_X 600.0f
#define DRAIN_ORIGIN_Y -40.0f
#define DRAIN_ORIGIN_Z 590.0f

enum District { D_DOWNTOWN, D_BLOCKS, D_IRONWORKS, D_WESTHILLS, D_MIDTOWN, D_AEGIS, D_DRAIN, D_OUTSKIRTS, D_COUNT };

inline float road_x(int k) { return -CITY_HALF + k * BLOCK_PITCH; }
inline float block_min(int i) { return road_x(i) + ROAD_W * 0.5f; }
inline float block_max(int i) { return road_x(i + 1) - ROAD_W * 0.5f; }
inline vec3 block_center(int i, int j) { return vec3((block_min(i) + block_max(i)) * 0.5f, 0, (block_min(j) + block_max(j)) * 0.5f); }

enum SurfaceKind { SURF_CONCRETE, SURF_METAL, SURF_WOOD, SURF_GLASS, SURF_DIRT, SURF_FLESH, SURF_ALIEN };

struct StaticBox {
    AABB box;
    u8 surface;
    u8 flags;      // 1 = blocks camera, 2 = vehicle passable (low props), 4 = climbable/vault
};

struct RayHit {
    float t;
    vec3 pos;
    vec3 normal;
    int box;
    u8 surface;
};

// Landmarks / points of interest
enum LandmarkId {
    LM_HOME, LM_DINER, LM_LAUNDROMAT, LM_PAWN, LM_SAFEHOUSE, LM_BUS_STOP, LM_POLICE, LM_HOSPITAL,
    LM_PLAZA, LM_CROSS_TOWER, LM_FIGHT_PIT, LM_CHOP_SHOP, LM_WAREHOUSE, LM_SUBSTATION, LM_NEXUS,
    LM_DRAIN_HATCH, LM_MANSION, LM_CAR_LOT, LM_AEGIS_LOBBY, LM_AEGIS_BRIEFING, LM_AEGIS_LAB,
    LM_AEGIS_RANGE, LM_AEGIS_ARCHIVE, LM_AEGIS_REACTOR, LM_DRAIN_NEST, LM_COUNT
};

struct Landmark {
    const char* name;
    vec3 pos;
    float radius;
    int district;
    bool discovered;
    bool showOnMap;
};

struct NavNode {
    vec3 pos;
    int links[6];
    int linkCount;
    u8 district;
};

struct Intersection {
    vec3 pos;
    int k, l;           // road indices
};

struct IndoorVolume {
    AABB box;
    float ambient;      // extra ambient for interiors
};

struct World {
    Array<StaticBox> boxes;
    // Spatial hash over XZ
    int gridW, gridH;
    float gridCell, gridOX, gridOZ;
    Array<int> cellStart;   // gridW*gridH + 1
    Array<int> cellItems;
    Landmark landmarks[LM_COUNT];
    Array<NavNode> walkNodes;
    Array<IndoorVolume> indoors;
    Array<vec4> parkingSpots;     // xyz + heading
    Array<vec3> cacheSpots;       // hidden caches
    Array<vec3> evidenceSpots;    // collectible evidence
    Array<int> streetLightIds;    // renderer static light ids (for blackout)
    Array<Light> streetLightOrig;
    Array<vec3> steamVents;
    Array<vec3> firePoints;       // burning barrels
    Array<vec3> relayPoints;      // substation relay consoles
    Array<vec4> neonLights;       // pos + light index
    int neonLightStart, neonLightEnd;
    u32 seed;
    int chunkCount;
    int burnedHomeChunk;
};

extern World g_world;

void world_generate(u32 seed);
void world_destroy();
int district_at(vec3 p);
const char* district_name(int d);
int block_district(int i, int j);
bool block_of(vec3 p, int* i, int* j);

// Collision queries
void world_add_box(vec3 mn, vec3 mx, int surface = SURF_CONCRETE, int flags = 1);
void world_build_grid();
float world_ground_height(vec3 p, float radius, float stepUp);
bool world_raycast(vec3 origin, vec3 dir, float maxT, RayHit* hit, bool cameraOnly = false);
// Moves a vertical cylinder through the world with sliding; returns true if grounded.
bool world_move_cylinder(vec3* pos, vec3 delta, float radius, float height, float stepUp, float* groundY);
bool world_overlap_cylinder(vec3 pos, float radius, float height);
bool world_is_indoors(vec3 p, float* ambient = 0);
int world_nearest_walk_node(vec3 p);
bool world_is_road(vec3 p);
vec3 world_nearest_road_point(vec3 p);
// Ground height callback for particles
float world_particle_ground(vec3 p);
// Blackout: switch street lights and windows
void world_set_power(bool on);
// Burned house variants etc.
void world_set_phase_visuals(int phase);
// Helpers for interiors / landmarks
vec3 landmark_pos(int id);
