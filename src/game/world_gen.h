// FIGHTO - internal helpers for world generation.
#pragma once
#include "world.h"
#include "../platform/platform.h"

#define GEN_CHUNK 64.0f
#define GEN_CHUNKS 24
#define GEN_ORIGIN -768.0f

MeshBuilder& gen_chunk(vec3 p);
void gen_flush_chunks();

// Geometry helpers. Colours are sRGB 0..1.
void gen_box(vec3 mn, vec3 mx, int layer, vec3 color, int flags = 0, float rough = 1.0f, float emissive = 0.0f, bool top = true, bool bottom = false);
void gen_solid(vec3 mn, vec3 mx, int layer, vec3 color, int surface = SURF_CONCRETE, int flags = 0, float rough = 1.0f);
void gen_cyl(vec3 base, float r, float h, int seg, int layer, vec3 color, int flags = 0, float rough = 1.0f, float emissive = 0.0f, bool solid = false);
void gen_tube(vec3 a, vec3 b, float r, int seg, int layer, vec3 color, int flags = 0, float emissive = 0.0f);
void gen_ellipsoid(vec3 c, vec3 r, int layer, vec3 color, int flags = 0, float rough = 1.0f, float emissive = 0.0f);
void gen_quad(vec3 a, vec3 b, vec3 c, vec3 d, int layer, vec3 color, int flags = 0, float rough = 1.0f, float emissive = 0.0f);
void gen_tree(vec3 base, float scale, u32 seed);
void gen_streetlight(vec3 base, vec3 armDir, bool led);
void gen_neon_text(vec3 origin, vec3 right, vec3 up, float letterH, const char* text, vec3 color, bool addLight = true, bool backing = true);
void gen_light(vec3 pos, float radius, vec3 color, float intensity);
void gen_spot(vec3 pos, vec3 dir, float radius, vec3 color, float intensity, float cosAngle);
void gen_beam(vec3 a, vec3 b, float r0, float r1, vec3 color, float intensity);
void gen_indoor(vec3 mn, vec3 mx, float ambient);
// Hollow room: walls with optional door gaps. doorMask bits: 1 -Z, 2 +Z, 4 -X, 8 +X. Door centred.
void gen_room(vec3 mn, vec3 mx, float wall, int layerWall, vec3 wallColor, int layerFloor, vec3 floorColor, int doorMask, float doorW, float doorH, bool ceiling, int layerOutside = -1, vec3 outsideColor = vec3(1, 1, 1), int outsideFlags = 0);
void gen_crate(vec3 base, vec3 size, u32 seed);
void gen_container(vec3 base, float yaw90, vec3 color);
void gen_car_wreck(vec3 base, float yaw, vec3 color);

void storefront(vec3 mn, vec3 mx, int face, Rng& rng, bool sign);
void building(vec3 mn, vec3 mx, int layer, vec3 color, Rng& rng, bool windows, bool waterTower);
void tower(vec3 mn, vec3 mx, float height, Rng& rng);
void house(vec3 mn, vec3 mx, Rng& rng, bool mansion);
void warehouse(vec3 mn, vec3 mx, Rng& rng, float h);
void chainlink(vec3 a, vec3 b, float h);
void gen_blocks(int i, int j, Rng& rng, int sideMask);

// Landmarks & interiors (world_landmarks.cpp)
void gen_landmarks(Rng& rng);
void gen_aegis_facility();
void gen_drain_tunnel();
bool gen_is_landmark_block(int i, int j);
