// FIGHTO - static 3D model import: glTF 2.0 (.gltf with external or data: buffers, .glb)
// and Wavefront OBJ + MTL. Parsing and texture decoding (PNG / JPEG at their original
// resolution) produce CPU-side data and need no GL context; model_upload() turns that into
// renderer meshes (one per material, existing Vertex format) and mipmapped textures.
// Skinned meshes are frozen in their default node pose; animations and morph targets are
// not imported.
#pragma once
#include "renderer.h"

enum ModelAlphaMode { MODEL_ALPHA_OPAQUE, MODEL_ALPHA_MASK, MODEL_ALPHA_BLEND };

struct ModelImage {
    int width, height;
    u8* rgba;            // width * height * 4 bytes, top row first
};

struct ModelMaterial {
    char name[64];
    vec4 baseColor;      // linear RGBA factor (glTF baseColorFactor, MTL Kd + d)
    float metallic, roughness;
    vec3 emissive;       // linear
    int image;           // base colour texture (index into ImportedModel::images), -1 = none
    int alphaMode;       // ModelAlphaMode
    float alphaCutoff;
    int wrapS, wrapT;    // GL wrap modes
};

// Every triangle that uses one material, with node transforms baked in. UVs follow the
// glTF convention (v = 0 is the top row of the image); OBJ texture coordinates are flipped.
// Vertex colour = sRGB-encoded baseColor * COLOR_0; mat = (MAT_PAINT, MF_METAL/MF_EMISSIVE,
// roughness * 128, emissive strength * 10).
struct ModelPart {
    int material;
    Array<Vertex> verts;
    Array<u32> inds;
};

struct ImportedModel {
    Array<ModelImage> images;
    Array<ModelMaterial> materials;
    Array<ModelPart> parts;
    vec3 bmin, bmax;
};

// Loads a model through plat_read_asset (path relative to the assets folder, e.g.
// "models/crate/crate.glb"); the format is chosen by the file extension. Buffers and
// textures are resolved relative to the model file. Logs the reason and returns false on
// malformed input.
bool model_import(const char* path, ImportedModel* out);
// In-memory parsers; `path` names the source file (external files, log messages).
bool model_import_gltf(const u8* data, int size, const char* path, ImportedModel* out);
bool model_import_obj(const u8* data, int size, const char* path, ImportedModel* out);
void model_free(ImportedModel* m);
int model_vertex_count(const ImportedModel& m);
int model_triangle_count(const ImportedModel& m);

struct ModelGpuPart {
    Mesh mesh;
    u32 tex;             // GL texture for DrawItem::albedoTex (1x1 white when untextured)
};

struct ModelGpu {
    Array<ModelGpuPart> parts;
    Array<u32> textures; // owned GL textures (shared between parts that use the same image)
    vec3 bmin, bmax;
};

bool model_upload(const ImportedModel& m, ModelGpu* out);
void model_gpu_destroy(ModelGpu* g);
