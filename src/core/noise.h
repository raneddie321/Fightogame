// FIGHTO - procedural noise used for textures, terrain variation and audio.
#pragma once
#include "vmath.h"

float noise_hash2(int x, int y, u32 seed);
// Tileable value noise: lattice wraps every `period` units.
float noise_value_tiled(float x, float y, int period, u32 seed);
float noise_fbm_tiled(float x, float y, int period, int octaves, u32 seed, float gain = 0.5f);
// Tileable cellular (Worley) noise: returns F1 distance and F2-F1 (edge) values.
void noise_worley_tiled(float x, float y, int period, u32 seed, float* f1, float* edge, u32* cellId);
// Non tiled value noise for gameplay variation
float noise_value(float x, float y, u32 seed);
float noise_fbm(float x, float y, int octaves, u32 seed);
