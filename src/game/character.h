// FIGHTO - skeletal characters: skeleton, procedural body meshes, poses and animation clips.
#pragma once
#include "../core/vmath.h"
#include "../gfx/renderer.h"

enum Bone {
    B_PELVIS, B_SPINE, B_CHEST, B_NECK, B_HEAD,
    B_L_UPPERARM, B_L_FOREARM, B_L_HAND,
    B_R_UPPERARM, B_R_FOREARM, B_R_HAND,
    B_L_THIGH, B_L_SHIN, B_L_FOOT,
    B_R_THIGH, B_R_SHIN, B_R_FOOT,
    B_COUNT
};

struct Skeleton {
    vec3 restPos[B_COUNT];     // joint positions in model space (bind pose)
    int parent[B_COUNT];
    float scale;
};

struct Pose {
    quat rot[B_COUNT];
    vec3 root;                 // pelvis offset from rest (model space)
};

// Body types share the skeleton topology with different proportions.
enum BodyType { BODY_HUMAN, BODY_STALKER, BODY_BRUTE, BODY_TENDRIL, BODY_OVERSEER, BODY_TYPE_COUNT };

enum HairStyle { HAIR_BALD, HAIR_SHORT, HAIR_LONG, HAIR_CAP, HAIR_HELMET, HAIR_BANDANA, HAIR_BUZZ, HAIR_POLICECAP };
enum TopStyle { TOP_TSHIRT, TOP_JACKET, TOP_HOODIE, TOP_SUIT, TOP_UNIFORM, TOP_TACTICAL, TOP_SHIRTLESS, TOP_COAT, TOP_LABCOAT };

struct Appearance {
    u8 body;           // BodyType
    u8 hair;
    u8 top;
    u8 beard;
    float height;      // metres
    float build;       // width scale 0.85..1.3
    vec3 skin;
    vec3 hairColor;
    vec3 topColor;
    vec3 shirtColor;
    vec3 pantsColor;
    vec3 shoeColor;
    vec3 accent;       // glowing details (aliens, tactical visors)
};

struct CharacterModel {
    Mesh mesh;
    Skeleton skel;
    u32 hash;
    bool used;
};

// Animation clips (keyframed euler rotations per bone) loaded from assets/animations/*.anim
#define MAX_CLIP_KEYS 12
struct ClipKey {
    float t;
    vec3 euler[B_COUNT];   // degrees
    vec3 root;
};
struct AnimClip {
    char name[32];
    float length;
    bool loop;
    int keyCount;
    ClipKey keys[MAX_CLIP_KEYS];
    // Gameplay markers
    float hitStart, hitEnd;     // active window for attacks
};

enum ClipId {
    CLIP_IDLE, CLIP_FIGHT_IDLE, CLIP_JAB, CLIP_CROSS, CLIP_HOOK, CLIP_UPPERCUT, CLIP_KICK, CLIP_HAYMAKER,
    CLIP_BLOCK, CLIP_DODGE, CLIP_HIT_FRONT, CLIP_HIT_HEAD, CLIP_STAGGER, CLIP_KNOCKDOWN, CLIP_GETUP,
    CLIP_DEATH_BACK, CLIP_DEATH_FORWARD, CLIP_JUMP, CLIP_FALL, CLIP_AIM_PISTOL, CLIP_AIM_RIFLE, CLIP_RELOAD,
    CLIP_DRIVE, CLIP_INTERACT, CLIP_TAKEDOWN, CLIP_POINT, CLIP_COWER, CLIP_SURRENDER, CLIP_CLAW, CLIP_LEAP,
    CLIP_SCREECH, CLIP_TALK, CLIP_STOMP, CLIP_DEAD_BACK, CLIP_DEAD_FRONT, CLIP_ELBOW, CLIP_SWEEP, CLIP_THROW,
    CLIP_COUNT
};

bool anim_load_clips();
const AnimClip* anim_clip(int id);
int anim_find(const char* name);

void skeleton_build(Skeleton* s, int body, float height, float build);
void pose_identity(Pose* p);
void pose_sample_clip(Pose* out, const AnimClip* c, float t);
void pose_blend(Pose* out, const Pose& a, const Pose& b, float w);
// Blend b over a only for the upper body (spine and above, arms).
void pose_blend_upper(Pose* out, const Pose& a, const Pose& b, float w);
void pose_locomotion(Pose* out, float phase, float speed, float crouch, float lean, int body);
// Applies an additive rotation (euler degrees) to one bone.
void pose_add(Pose* p, int bone, vec3 eulerDeg);
// Computes model-space joint matrices and skinning matrices.
void pose_compute(const Skeleton& s, const Pose& p, mat4* globals, mat4* skin);
vec3 joint_world(const mat4& model, const mat4* globals, int bone, vec3 localOffset = vec3(0, 0, 0));

CharacterModel* character_model_get(const Appearance& a);
void character_models_clear();
Appearance appearance_preset(const char* name, u32 seed);
// Generates a mesh for any appearance (used by the model cache and glTF fallback tests).
void character_build_mesh(MeshBuilder* mb, const Skeleton& s, const Appearance& a);
