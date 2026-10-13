// FIGHTO - core gameplay state shared by every gameplay module.
#pragma once
#include "../core/vmath.h"
#include "../gfx/renderer.h"
#include "../platform/platform.h"
#include "character.h"
#include "world.h"

#define MAX_ACTORS 128
#define MAX_VEHICLES 64
#define MAX_PICKUPS 96
#define MAX_PROJECTILES 96
#define PLAYER 0

// ---------------------------------------------------------------------------
// Input actions
// ---------------------------------------------------------------------------
enum Action {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_SPRINT, ACT_JUMP, ACT_CROUCH, ACT_INTERACT,
    ACT_ATTACK, ACT_AIM, ACT_RELOAD, ACT_LOCKON, ACT_DODGE, ACT_KICK, ACT_JOURNAL, ACT_PAUSE,
    ACT_QUICKSAVE, ACT_WEAPON_NEXT, ACT_WEAPON_PREV, ACT_HORN, ACT_HANDBRAKE, ACT_MAP, ACT_CHOICE1, ACT_CHOICE2,
    ACT_COUNT
};
const char* action_name(int a);

struct Controls {
    vec2 move;          // x right, y forward (-1..1)
    vec2 look;          // camera delta (radians)
    bool down[ACT_COUNT];
    bool pressed[ACT_COUNT];
    bool released[ACT_COUNT];
    float held[ACT_COUNT];
    int weaponSlot;     // 0 = none, 1..8 direct selection
    bool anyKey;
    bool menuUp, menuDown, menuLeft, menuRight, menuAccept, menuBack;
    vec2 pointer;       // mouse / touch position in pixels
    bool pointerClick;
    bool pointerDown;
    int wheel;
};

// ---------------------------------------------------------------------------
// Factions, actors
// ---------------------------------------------------------------------------
enum Faction { FAC_PLAYER, FAC_CIVILIAN, FAC_POLICE, FAC_KINGS, FAC_VARGA, FAC_AEGIS, FAC_OPERATIVES, FAC_ALIEN, FAC_FIGHTER, FAC_COUNT };
const char* faction_name(int f);

enum ActorKind {
    AK_PLAYER, AK_CIVILIAN, AK_THUG, AK_GUNMAN, AK_FIGHTER, AK_POLICE, AK_AEGIS, AK_OPERATIVE,
    AK_STALKER, AK_BRUTE, AK_TENDRIL, AK_OVERSEER, AK_DRONE, AK_STORY, AK_COUNT
};

enum ActorState {
    AS_LOCOMOTION, AS_ATTACK, AS_BLOCK, AS_DODGE, AS_HIT, AS_STAGGER, AS_KNOCKDOWN, AS_GETUP, AS_DEAD,
    AS_VEHICLE, AS_INTERACT, AS_TAKEDOWN, AS_TAKEN_DOWN, AS_COWER, AS_SURRENDER, AS_SCRIPTED, AS_ARRESTED
};

enum AIMode {
    AI_NONE, AI_WANDER, AI_STAND, AI_TALK, AI_FLEE, AI_COWER, AI_INVESTIGATE, AI_COMBAT, AI_SEARCH,
    AI_GOTO, AI_FOLLOW, AI_GUARD, AI_ARREST, AI_SPECTATE, AI_HOLD, AI_DRIVE, AI_PATROL
};

enum Weapon {
    W_FISTS, W_PISTOL, W_SHOTGUN, W_SMG, W_RIFLE, W_MAGNUM, W_ARC, W_LANCE, W_SHARD, W_CLAWS, W_DRONE_GUN, W_COUNT
};

struct WeaponDef {
    const char* name;
    int clipSize;
    int maxReserve;
    float damage;
    float fireInterval;
    float reloadTime;
    float spread;        // radians (hip)
    float aimSpread;
    float recoil;
    int pellets;
    float range;
    bool automatic;
    float headMult;
    int price;
    int ammoPrice;
    bool alien;
    bool projectile;     // fires a visible projectile instead of hitscan
    bool twoHanded;
    vec3 tracer;
};
extern const WeaponDef g_weapons[W_COUNT];

struct Ragdoll;

struct Actor {
    bool active;
    u8 kind;
    u8 faction;
    u8 state;
    u8 aiMode;
    char tag[16];
    char name[24];
    Appearance app;
    CharacterModel* model;
    vec3 pos, vel;
    float yaw;
    float desiredYaw;
    bool grounded;
    float radius, height;
    float health, maxHealth, armor;
    float stamina, maxStamina;
    bool nonLethal;
    bool persistent;
    bool invulnerable;
    float stateTime;
    // Animation
    int clip;
    float clipTime;
    float clipSpeed;
    int fromClip;
    float fromTime;
    float blend;
    float locoPhase;
    float speed;
    float crouch;
    float aimBlend;
    Pose pose;
    mat4 world;
    mat4 globals[B_COUNT];
    mat4 skin[B_COUNT];
    // Combat
    int combo;
    bool comboQueued;
    bool queuedHeavy;
    int attackClip;
    float attackDamage;
    bool attackHeavy;
    int hitIds[6];
    int hitCount;
    float hitFlash;
    float blood;
    float bleed;
    float lastDamageTime;
    int lastAttacker;
    float stun;
    float iframes;
    float blockStart;
    vec3 knockVel;
    // Weapons
    int weapon;
    int clipAmmo;
    float fireCooldown;
    float reloadTimer;
    int burst;
    float muzzleFlash;
    // AI
    int target;
    float aiTimer;
    float aiTimer2;
    vec3 goal;
    vec3 home;
    int navNode, navPrev;
    float alert;
    vec3 lastKnown;
    float lastSeen;
    float accuracy;
    float aggression;
    float skill;
    float attackCooldown;
    float shout;
    int vehicle;
    int seat;
    bool hostileToPlayer;
    float deathTime;
    bool poolSpawned;
    Ragdoll* ragdoll;
    float scriptTimer;
    int talkLine;
    float knockedOut;
    bool counted;      // stats / mission accounting done
};

// ---------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------
enum VehicleType { VT_SEDAN, VT_MUSCLE, VT_SUV, VT_SPORTS, VT_VAN, VT_POLICE, VT_AEGIS, VT_TAXI, VT_COUNT };
enum VehicleAI { VAI_PARKED, VAI_TRAFFIC, VAI_CHASE, VAI_SCRIPT, VAI_FLEE, VAI_NONE };

struct VehicleDef {
    const char* name;
    vec3 halfExtents;    // x half width, y height, z half length
    float maxSpeed;
    float accel;
    float brake;
    float grip;
    float steerRate;
    float mass;
    float health;
    int price;
};
extern const VehicleDef g_vehicleDefs[VT_COUNT];

struct Vehicle {
    bool active;
    u8 type;
    u8 ai;
    char tag[16];
    vec3 pos;
    float yaw;
    vec3 vel;
    float angVel;
    float steer;
    float throttle;
    float brakeInput;
    bool handbrake;
    float health;
    float burnTime;
    bool destroyed;
    float destroyedTime;
    vec3 color;
    int driver;
    bool siren;
    float sirenPhase;
    float wheelRot;
    float bodyPitch, bodyRoll;
    float damage;
    float blood;
    // Traffic AI
    int fromK, fromL, toK, toL;
    float stuckTime;
    float hornTime;
    float aiTimer;
    vec3 aiTarget;
    bool persistent;
    bool locked;
    float engineRev;
    float lastCrashSound;
};

// ---------------------------------------------------------------------------
// Pickups & projectiles
// ---------------------------------------------------------------------------
enum PickupType { PK_WEAPON, PK_AMMO, PK_HEALTH, PK_ARMOR, PK_MONEY, PK_EVIDENCE, PK_CACHE, PK_ITEM };
struct Pickup {
    bool active;
    u8 type;
    u8 weapon;
    int amount;
    int evidenceId;
    vec3 pos;
    float spin;
    char tag[16];
    float life;      // <0 = permanent
};

struct Projectile {
    bool active;
    u8 weapon;
    int owner;
    vec3 pos, vel;
    float life;
    float damage;
    vec3 color;
};

// ---------------------------------------------------------------------------
// Player progression
// ---------------------------------------------------------------------------
enum Upgrade { UP_HEALTH, UP_STAMINA, UP_MELEE, UP_ARMOR, UP_HANDLING, UP_COUNT };

#define MAX_EVIDENCE 16
#define MAX_FLAGS 64

struct StoryFlag {
    char name[24];
    int value;
};

struct PlayerData {
    int money;
    bool hasWeapon[W_COUNT];
    int reserve[W_COUNT];
    int clip[W_COUNT];
    int upgrades[UP_COUNT];
    int rep[FAC_COUNT];
    bool evidence[MAX_EVIDENCE];
    bool cachesFound[32];
    u32 missionsDone;       // bit per mission
    int phase;              // 0 crime, 1 aegis, 2 blackout, 3 invasion
    StoryFlag flags[MAX_FLAGS];
    int flagCount;
    int kills, knockouts, shotsFired, shotsHit, fightsWon, racesWon, civiliansRescued, timesArrested, deaths;
    float playTime;
    float distanceDriven;
    int safehouseUnlocked;
    int pitWins;
};

int flag_get(const char* name);
void flag_set(const char* name, int value);

// ---------------------------------------------------------------------------
// Wanted level
// ---------------------------------------------------------------------------
struct Wanted {
    int level;
    float heat;
    float searchTimer;
    float sinceSeen;
    vec3 lastKnown;
    bool seen;
    float spawnTimer;
    float flash;
    float crimeCooldown;
};

enum CrimeType { CRIME_ASSAULT, CRIME_MURDER, CRIME_SHOOTING, CRIME_CARJACK, CRIME_COP_ASSAULT, CRIME_COP_MURDER, CRIME_VEHICLE_HIT };

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
struct CameraRig {
    float yaw, pitch;
    float dist;
    vec3 pos, target;
    vec3 smoothFocus;
    float fov;
    float shake;
    float aimZoom;
    bool cinematic;
    vec3 cinePos, cineTarget;
    float cineFov;
    float vehicleYaw;
    float lookTimer;
    float recoilPitch;
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
struct Settings {
    int quality;
    float renderScale;
    bool fullscreen;
    bool vsync;
    float brightness;
    float fov;
    float mouseSens;
    bool invertY;
    float masterVol, musicVol, sfxVol, voiceVol;
    int gore;           // 0 off, 1 normal, 2 extreme
    bool subtitles;
    int difficulty;     // 0 easy, 1 normal, 2 hard
    bool filmGrain;
    bool motionBlur;
    float touchScale;
    int keys[ACT_COUNT][2];
};

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------
enum GameScreen {
    SCR_BOOT, SCR_MAIN_MENU, SCR_PLAYING, SCR_PAUSE, SCR_SETTINGS, SCR_CONTROLS, SCR_CREDITS, SCR_JOURNAL,
    SCR_DEAD, SCR_ARRESTED, SCR_MISSION_COMPLETE, SCR_MISSION_FAILED, SCR_SHOP, SCR_LOAD_CONFIRM, SCR_UPGRADES, SCR_ENDING
};

enum InteractKind {
    IK_NONE, IK_VEHICLE, IK_TALK, IK_TAKEDOWN, IK_SHOP, IK_SAVE, IK_ELEVATOR_DOWN, IK_ELEVATOR_UP, IK_DRAIN_ENTER,
    IK_DRAIN_EXIT, IK_MISSION, IK_PIT, IK_ARMORY, IK_PICKUP, IK_START_MISSION, IK_RACE
};
struct Interactable {
    vec3 pos;
    float radius;
    int kind;
    int index;
    char text[80];
};
#define MAX_INTERACTABLES 48

struct Notification {
    char text[128];
    float time;
    vec4 color;
};

struct Game {
    GameLaunchOptions opt;
    int screen;
    int prevScreen;
    int settingsReturn;
    float time;
    float realTime;
    float dt;
    float timeScale;
    float slowMo;
    int width, height;
    Settings settings;
    Controls ctl;
    InputState* input;
    Actor actors[MAX_ACTORS];
    Vehicle vehicles[MAX_VEHICLES];
    Pickup pickups[MAX_PICKUPS];
    Projectile projectiles[MAX_PROJECTILES];
    PlayerData pd;
    Wanted wanted;
    CameraRig cam;
    Atmosphere atm;
    PostState post;
    Rng rng;
    int lockTarget;
    Interactable interactables[MAX_INTERACTABLES];
    int interactCount;
    int interactBest;      // index into interactables or -1
    float damageFlash;
    float bloodScreen;
    float bloodSeed;
    float hitMarker;
    float killMarker;
    float spawnTimer;
    float trafficTimer;
    int district;
    float districtBanner;
    float rain, rainTarget;
    float lightning;
    float lightningTimer;
    float windowsOn;
    bool power;
    float invasion;
    bool inMission;
    bool cutscene;
    float letterbox;
    float fade;
    float fadeTarget;
    float whiteFlash;
    float shardGlow;
    float deathTimer;
    bool newGame;
    bool hasSave;
    char saveStatus[96];
    float saveStatusTime;
    Notification notes[6];
    int noteCount;
    // Menu state
    int menuIndex;
    int menuScroll;
    int rebindAction;
    int rebindSlot;
    float menuAnim;
    bool quitRequested;
    int journalTab;
    float mapZoom;
    vec2 mapCenter;
    bool mapOpen;
    // Stats
    float fps;
    int frame;
    bool touch;
    float onScreenTime;
    float idleRegen;
    float combatMusic;
    int ambientSpawned;
    float sirenVolume;
    float heartbeat;
};

extern Game g;

// ---------------------------------------------------------------------------
// Module API
// ---------------------------------------------------------------------------
// controls.cpp
void controls_update(Controls* c, InputState* in, const Settings& s, bool playing, float dt);
void settings_defaults(Settings* s);
bool settings_load(Settings* s);
bool settings_save(const Settings& s);
void settings_apply(const Settings& s);

struct TouchButton {
    vec2 c;
    float r;
    int action;
    const char* label;
};
struct TouchLayout {
    vec2 joyCenter, joyKnob;
    float joyRadius;
    bool joyActive;
    TouchButton buttons[14];
    int count;
};
void touch_layout(TouchLayout* l, int w, int h, bool vehicle, bool armed, bool menu);

// actors.cpp
int actor_spawn(int kind, vec3 pos, float yaw, const Appearance& app, int faction);
int actor_spawn_preset(int kind, const char* preset, vec3 pos, float yaw, u32 seed);
void actor_despawn(int idx);
void actor_set_clip(Actor& a, int clip, float blend = 0.12f, float speed = 1.0f);
void actors_update(float dt);
void actors_render();
void actor_update_pose(Actor& a, float dt);
vec3 actor_head(const Actor& a);
vec3 actor_chest(const Actor& a);
vec3 actor_hand(const Actor& a, bool right);
bool actor_alive(const Actor& a);
bool actor_is_enemy(const Actor& a, const Actor& b);
int actor_find_tag(const char* tag);
int actor_count_tag(const char* tag, bool aliveOnly);
float actor_dist(int a, int b);
void actor_face(Actor& a, vec3 p, float rate, float dt);
bool actor_move(Actor& a, vec3 wishVel, float dt, float accel);

// player.cpp
void player_reset(bool newGame);
void player_update(float dt);
void player_give_weapon(int w, int ammo);
int player_current_weapon();
void player_select_weapon(int w);
void player_enter_vehicle(int v);
void player_exit_vehicle();
void player_heal(float amount);
float player_max_health();
float player_max_stamina();
void player_teleport(vec3 p, float yaw);
bool player_interact(const Interactable& it);

// camera.cpp
void camera_update(float dt);
CameraState camera_state();
void camera_shake(float amount);
void camera_cinematic(vec3 pos, vec3 target, float fov);
void camera_release();
vec3 camera_forward();
vec3 camera_aim_point(float maxDist, int ignoreActor, int* hitActor);

// combat.cpp
void combat_start_attack(Actor& a, int clip, bool heavy);
void combat_update_attack(int idx, Actor& a, float dt);
void combat_damage(int victim, int attacker, float dmg, vec3 hitPos, vec3 dir, int kind, int bone);
void combat_blood_burst(vec3 pos, vec3 dir, float amount, bool alien);
void combat_blood_pool(vec3 pos, bool alien, float size);
void combat_update(float dt);
void ragdoll_start(Actor& a, vec3 impulse);
void ragdoll_update(Actor& a, float dt);
void ragdoll_free(Actor& a);
enum DamageKind { DMG_MELEE, DMG_BULLET, DMG_HEADSHOT, DMG_EXPLOSION, DMG_VEHICLE, DMG_FALL, DMG_ENERGY, DMG_CLAW, DMG_FIRE };

// weapons.cpp
void weapons_fire(int shooter, vec3 origin, vec3 dir, bool aimed);
bool weapons_try_reload(int idx);
void projectiles_update(float dt);
void projectiles_render();
void weapon_render(const Actor& a);
void explosion(vec3 pos, float radius, float damage, int owner, bool alien);
void weapons_init_meshes();
void temp_light(vec3 pos, vec3 color, float radius, float life);
void temp_beam(vec3 a, vec3 b, float r, vec3 color, float intensity, float life);

// ai.cpp
void ai_update(int idx, Actor& a, float dt);
bool ai_can_see(const Actor& a, vec3 p, float range);
void ai_alert_area(vec3 pos, float radius, int threat, int type);
void ai_population_update(float dt);
void ai_make_hostile(int idx, int target);

// vehicles.cpp
int vehicle_spawn(int type, vec3 pos, float yaw, vec3 color, int ai);
void vehicle_despawn(int v);
void vehicles_update(float dt);
void vehicles_render();
void vehicles_init_meshes();
void vehicle_damage(int v, float dmg, vec3 pos);
vec3 vehicle_seat_pos(const Vehicle& v, int seat);
vec3 vehicle_exit_pos(const Vehicle& v, bool* ok);
int vehicle_find_tag(const char* tag);
void traffic_update(float dt);
vec3 vehicle_forward(const Vehicle& v);

// police.cpp
void wanted_crime(int type, vec3 pos, bool witnessed);
void wanted_update(float dt);
void wanted_set(int level);
void wanted_clear();

// pickups.cpp
int pickup_spawn(int type, vec3 pos, int weapon, int amount, const char* tag);
void pickups_update(float dt);
void pickups_render();
void pickups_init_meshes();

// mission.cpp
void missions_init();
void missions_update(float dt);
void missions_render_markers();
bool mission_active();
const char* mission_objective();
const char* mission_title();
int mission_current_id();
void mission_on_player_death();
void mission_retry();
void mission_abort();
int mission_count();
const char* mission_name(int i);
const char* mission_desc(int i);
bool mission_available(int i);
bool mission_is_done(int i);
int mission_id(int i);
bool mission_is_side(int i);
bool mission_is_hidden(int i);
vec3 mission_start_pos(int i);
bool mission_marker(vec3* pos);
void mission_debug_complete_step();
bool mission_choice_active();
void mission_start(int id);
float mission_timer();
int mission_fail_reason_index();
const char* mission_fail_reason();
int mission_reward_last();
void mission_choose(int option);
void mission_interact(int which);
const char* mission_choice_prompt();
const char* mission_choice_option(int i);
float mission_hold_progress();
const char* mission_last_title();

// dialogue.cpp
bool dialogue_load_all();
void dialogue_start(const char* id);
void dialogue_say(const char* speaker, const char* text, float duration);
bool dialogue_active();
void dialogue_update(float dt);
void dialogue_render();
void dialogue_skip_line();
void dialogue_clear();
const char* speaker_display(const char* id, vec3* color);

// cutscene.cpp
bool cutscenes_load();
void cutscene_start(const char* id);
bool cutscene_active();
void cutscene_update(float dt);
void cutscene_skip();

// save.cpp
bool save_game(int slot, char* status, int statusCap);
bool load_game(int slot, char* status, int statusCap);
bool save_exists(int slot);
bool save_serialize(Array<u8>* out);
bool save_deserialize(const u8* data, int size, char* err, int errCap);

// interactions (game.cpp)
void interact_clear();
void interact_add(vec3 pos, float radius, int kind, int index, const char* text);
void interact_execute(const Interactable& it);
// weapons.cpp: ray test against characters (returns actor index, -1 if none)
int ray_actors(vec3 o, vec3 d, float maxT, int ignore, float* tOut, int* boneOut);

// hud.cpp / menu.cpp
void hud_render();
void menu_update_render();
void notify(const char* text, vec4 color = vec4(1, 1, 1, 1));
void map_render(float x, float y, float w, float h, bool full);

// audio (audio/audio.h)
// game.cpp helpers
void game_set_screen(int s);
void game_new_game();
void game_continue_from_menu();
void game_respawn_player(bool arrested);
void game_set_phase(int phase);
float rep_get(int f);
void rep_add(int f, int delta);
void add_money(int amount);
bool is_hostile_faction(int f);
void game_cleanup_world_entities(bool keepMission);
