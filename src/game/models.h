// FIGHTO - externally made 3D models (glTF / GLB / OBJ) placed in Blackridge through
// assets/models/manifest.txt. See assets/models/README.md for the manifest syntax.
#pragma once

void models_init();       // after world_generate(): loads, uploads and places the models
void models_render();     // submits the visible placements (between renderer_begin_frame / end_frame)
void models_shutdown();
