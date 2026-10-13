// FIGHTO - procedural material textures.
#pragma once
#include "renderer.h"

// Generates one material layer: RGBA8 albedo+roughness and RGBA8 normal(xy)+AO+height.
void texgen_layer(int layer, int size, u8* albedoOut, u8* normalOut);
