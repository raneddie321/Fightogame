// FIGHTO - renderer internals shared between renderer.cpp and fx.cpp.
#pragma once
#include "renderer.h"

struct FxVertex {
    vec3 pos;
    vec2 uv;
    u8 col[4];
    float params[4];
};

void fx_build_particles(Array<FxVertex>* verts, Array<u32>* inds, vec3 camPos, vec3 camRight, vec3 camUp, const Frustum& fr);
void fx_build_decals(Array<FxVertex>* verts, Array<u32>* inds, vec3 camPos, float maxDist, const Frustum& fr);
void fx_push_quad(Array<FxVertex>* verts, Array<u32>* inds, vec3 a, vec3 b, vec3 c, vec3 d, vec3 color, float alpha, float p0, float p1, float p2, float p3);
